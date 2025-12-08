#include <WiFi.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "prague_cc.h"
#include <errno.h>

// ===== CONGESTION CONTROL MODE =====
// Default: full Prague CC (adaptive). Uncomment for a fixed baseline mode
// that does not adapt window/rate based on Prague feedback.
// #define CC_MODE_BASELINE

// ===== ECN SENDER MARKING =====
// ECN_SENDER_ENABLE = 1 -> mark packets as ECN-capable (ECT(1))
// ECN_SENDER_ENABLE = 0 -> do not request ECN (Not-ECT)
#define ECN_SENDER_ENABLE 1

// ===== WIFI CONFIG =====
const char* ssid     = "JEED-TH001";
const char* password = "Test";

// ===== RECEIVER CONFIG =====
const char* server_ip   = "3.16.158.161";
const int   server_port = 5005;

// ===== TEST SCENARIO CONFIGURATION =====
#define SCENARIO_BASELINE
//#define SCENARIO_MEDIUM
//#define SCENARIO_HIGH
//#define SCENARIO_BURST
//#define SCENARIO_JUMBO

#ifdef SCENARIO_BASELINE
    #define TEST_NAME "Baseline"
    #define EXTRA_PAYLOAD_SIZE 0
    #define TEST_DURATION_SEC 60
#elif defined(SCENARIO_MEDIUM)
    #define TEST_NAME "Medium Load"
    #define EXTRA_PAYLOAD_SIZE 500
    #define TEST_DURATION_SEC 120
#elif defined(SCENARIO_HIGH)
    #define TEST_NAME "High Load"
    #define EXTRA_PAYLOAD_SIZE 1383
    #define TEST_DURATION_SEC 180
#elif defined(SCENARIO_BURST)
    #define TEST_NAME "Burst Mode"
    #define EXTRA_PAYLOAD_SIZE 1183
    #define BURST_SIZE 50
    #define BURST_INTERVAL_MS 5
    #define BURST_PAUSE_MS 1000
    #define TEST_DURATION_SEC 120
#elif defined(SCENARIO_JUMBO)
    #define TEST_NAME "Jumbo Packets (>MTU)"
    #define EXTRA_PAYLOAD_SIZE 1983
    #define TEST_DURATION_SEC 180
    #define WARN_FRAGMENTATION
#else
    #define TEST_NAME "Default"
    #define EXTRA_PAYLOAD_SIZE 0
    #define TEST_DURATION_SEC 60
#endif

// ===== LIMITES REALISTAS PARA ESP32 =====
static const int MAX_WINDOW_ESP32 = 12;    // máximo inflight que o WiFi suporta
static const int MAX_BURST_ESP32  = 5;     // burst seguro

// ===== BURST ADAPTATIVO =====
static count_tp MAX_SAFE_BURST = 3;        // inicial seguro
static uint32_t burstSuccessCount = 0;

// ===== SOCKET & ADDRESS =====
int sockfd;
struct sockaddr_in dest_addr;

// ===== PRAGUE CC =====
PragueCC prague;
time_tp  nextSend;
count_tp seqnr;
count_tp inflight = 0;
count_tp packets_sent = 0;

rate_tp  pacing_rate;
count_tp packet_window;
count_tp packet_burst;
size_tp  packet_size;

// ===== TEST METRICS =====
unsigned long test_start_ms = 0;
unsigned long test_bytes_sent = 0;
unsigned long test_packets_sent = 0;

unsigned long error_count = 0;     // total de erros sendto()
unsigned long enobufs_count = 0;   // especificamente ENOBUFS
unsigned long other_errors = 0;    // outros erros sendto()

// ===== CLIENT RTT/JITTER METRICS =====
time_tp last_rtt = 0;
bool    has_last_rtt = false;
time_tp rtt_min = 0, rtt_max = 0;
int64_t rtt_sum = 0;
uint32_t rtt_count = 0;

#pragma pack(push, 1)

struct datamessage_t {
    uint8_t  type;
    time_tp  timestamp;
    time_tp  echoed_timestamp;
    count_tp seq_nr;

    void hton() {
        type = 1;
        timestamp = htonl(timestamp);
        echoed_timestamp = htonl(echoed_timestamp);
        seq_nr = htonl(seq_nr);
    }
};

struct ackmessage_t {
    uint8_t  type;
    count_tp ack_seq;
    time_tp  timestamp;
    time_tp  echoed_timestamp;
    count_tp packets_received;
    count_tp packets_CE;
    count_tp packets_lost;
    bool     error_L4S;

    void ntoh() {
        ack_seq = ntohl(ack_seq);
        timestamp = ntohl(timestamp);
        echoed_timestamp = ntohl(echoed_timestamp);
        packets_received = ntohl(packets_received);
        packets_CE = ntohl(packets_CE);
        packets_lost = ntohl(packets_lost);
    }
};

#pragma pack(pop)

// ===== BUFFER ESTÁTICO =====
static uint8_t tx_buffer[sizeof(datamessage_t) + EXTRA_PAYLOAD_SIZE];

// ===== PRINT DEVICE INFO =====
void printDeviceInfo() {
    Serial.println("Device Information:");
    Serial.println("--------------------------------------");
    Serial.printf("Chip Model: %s\n", ESP.getChipModel());
    Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
    Serial.printf("Chip Cores: %d\n", ESP.getChipCores());
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash Size: %u KB\n", ESP.getFlashChipSize() / 1024);
    Serial.printf("Flash Speed: %u Hz\n", ESP.getFlashChipSpeed());
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion());
    Serial.println("--------------------------------------\n");
}


// ===== RECEIVE ACKs =====
void receiveAcks() {
    ackmessage_t ack;
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    int len = recvfrom(
        sockfd,
        &ack,
        sizeof(ack),
        MSG_DONTWAIT,
        (struct sockaddr*)&source_addr,
        &socklen
    );

    if (len <= 0) {
        return; // nothing to read
    }

    // Basic size check (future-proof if server ever changes struct)
    if (len != (int)sizeof(ackmessage_t)) {
        Serial.printf("[ACK-IGNORED] len=%d (expected %u)\n", len, (unsigned)sizeof(ackmessage_t));
        return;
    }

    ack.ntoh();

    if (ack.type != 17) {
        Serial.printf("[ACK-IGNORED] invalid type=%u\n", (unsigned)ack.type);
        return;
    }

    // Sanity check on counters (defensive)
    if (ack.packets_received < 0 || ack.packets_CE < 0 || ack.packets_lost < 0) {
        Serial.printf("[ACK-IGNORED] negative counters: recv=%ld CE=%ld lost=%ld\n",
                      (long)ack.packets_received,
                      (long)ack.packets_CE,
                      (long)ack.packets_lost);
        return;
    }

    // ===== RTT & JITTER (client-side) =====
    time_tp rtt = ack.echoed_timestamp - ack.timestamp;
    if (rtt < 0) {
        // defensivo: se vier negativo por wrap, ignore esta amostra
        rtt = 0;
    }

    time_tp jitter = 0;
    if (has_last_rtt) {
        time_tp diff = rtt - last_rtt;
        if (diff < 0) diff = -diff;
        jitter = diff;
    } else {
        has_last_rtt = true;
    }
    last_rtt = rtt;

    // stats agregadas
    if (rtt_count == 0) {
        rtt_min = rtt_max = rtt;
    } else {
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;
    }
    rtt_sum += rtt;
    rtt_count++;

    // Feed congestion control (Prague or baseline)
#ifndef CC_MODE_BASELINE
    // Full Prague CC: update rate/window/burst based on feedback
    prague.PacketReceived(ack.timestamp, ack.echoed_timestamp);
    prague.ACKReceived(
        ack.packets_received,
        ack.packets_CE,
        ack.packets_lost,
        packets_sent,
        ack.error_L4S,
        inflight
    );

    prague.GetCCInfo(pacing_rate, packet_window, packet_burst, packet_size);

    // Limit window to what the ESP32 Wi‑Fi can handle
    packet_window = min(packet_window, (count_tp)MAX_WINDOW_ESP32);
#else
    // Baseline mode: keep a fixed window/burst, do not adapt
    packet_window = MAX_WINDOW_ESP32;
    packet_burst  = MAX_BURST_ESP32;
    // pacing_rate/packet_size are taken from the initial PragueCC config in setup()
#endif

    // ===== RECALCULAR INFLIGHT REAL =====
    if (ack.packets_received <= packets_sent) {
        inflight = packets_sent - ack.packets_received;
    } else {
        inflight = 0;
    }

    Serial.printf(
        "[ACK] recv=%lu CE=%lu lost=%lu inflight=%ld burstMax=%ld win=%ld L4Serr=%d\n",
        (unsigned long)ack.packets_received,
        (unsigned long)ack.packets_CE,
        (unsigned long)ack.packets_lost,
        (long)inflight,
        (long)MAX_SAFE_BURST,
        (long)packet_window,
        (int)ack.error_L4S
    );

    // ===== LOG ESTRUTURADO PARA ANÁLISE (CSTATS) =====
    unsigned long ms_since_start = millis() - test_start_ms;
    Serial.printf(
        "CSTATS;%lu;%ld;%ld;%ld;%ld;%ld;%ld;%llu\n",
        ms_since_start,
        (long)ack.packets_received,
        (long)inflight,
        (long)rtt,
        (long)jitter,
        (long)packet_window,
        (long)packet_burst,
        (unsigned long long)pacing_rate
    );
}

// ===== SEND DATA PACKET =====
void sendDataPacket() {
    size_t total_size = sizeof(datamessage_t) + EXTRA_PAYLOAD_SIZE;

    datamessage_t* msg = (datamessage_t*) tx_buffer;
    ecn_tp snd_ecn;

    prague.GetTimeInfo(msg->timestamp, msg->echoed_timestamp, snd_ecn);
    msg->seq_nr = seqnr;
    msg->hton();

    int sent = sendto(
        sockfd,
        tx_buffer,
        total_size,
        0,
        (struct sockaddr*)&dest_addr,
        sizeof(dest_addr)
    );

    if (sent == (int)total_size) {
        inflight++;
        seqnr++;
        packets_sent++;
        test_packets_sent++;
        test_bytes_sent += total_size;

        burstSuccessCount++;
        if (burstSuccessCount > 2000 && MAX_SAFE_BURST < MAX_BURST_ESP32) {
            MAX_SAFE_BURST++;
            burstSuccessCount = 0;
        }

    } else {
        int err = errno;

        error_count++;
        if (err == ENOBUFS) enobufs_count++;
        else other_errors++;

        Serial.printf(
            "[ERROR] seq=%ld errno=%d (%s) inflight=%ld burst=%ld\n",
            (long)seqnr, err, strerror(err), (long)inflight, (long)MAX_SAFE_BURST
        );

        // ADAPTIVE BURST
        if (err == ENOBUFS || err == EAGAIN) {
            MAX_SAFE_BURST = max((int)(MAX_SAFE_BURST / 2), 2);
        }
    }
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n======================================");
    Serial.printf("=== ESP32 UDP Prague Client ===\n");
    Serial.printf("Test Scenario: %s\n", TEST_NAME);
    Serial.printf("Payload Size: %u bytes (header: %u + extra: %u)\n",
                  (unsigned)(sizeof(datamessage_t) + EXTRA_PAYLOAD_SIZE),
                  (unsigned)sizeof(datamessage_t),
                  EXTRA_PAYLOAD_SIZE);
    Serial.printf("Initial Burst Limit: %ld packets\n", (long)MAX_SAFE_BURST);
    Serial.printf("Duration: %u seconds\n", TEST_DURATION_SEC);
    Serial.println("======================================\n");

    printDeviceInfo();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print("*");
    }
    Serial.printf("\nWiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("IP: %s\n\n", WiFi.localIP().toString().c_str());

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    // Configure IP TOS / ECN bits for outgoing packets
    // Low 2 bits are ECN: 00 = Not-ECT, 01 = ECT(1)
    int tos = (ECN_SENDER_ENABLE ? 0x01 : 0x00);
    setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(server_port);
    inet_aton(server_ip, &dest_addr.sin_addr);

    seqnr = 1;
    prague.GetCCInfo(pacing_rate, packet_window, packet_burst, packet_size);

    nextSend = prague.Now() + 2000;
    test_start_ms = millis();
}

// ===== MAIN LOOP =====
void loop() {
    unsigned long elapsed = millis() - test_start_ms;

    if (elapsed >= TEST_DURATION_SEC * 1000UL) {
        Serial.printf(
            "\n=== TEST COMPLETED ===\n"
            "Bytes Sent: %lu\n"
            "Packets Sent: %lu\n"
            "Total Errors: %lu\n"
            " - ENOBUFS: %lu\n"
            " - Other Errors: %lu\n",
            test_bytes_sent,
            test_packets_sent,
            error_count,
            enobufs_count,
            other_errors
        );
        while (true) delay(1000);
    }

    receiveAcks();

    time_tp now = prague.Now();
    time_tp burst_start = 0;
    count_tp burst_count = 0;

    // ===== BURST LIMITADO =====
    count_tp allowed_burst = min(packet_burst, MAX_SAFE_BURST);

    // ===== JANELA LIMITADA =====
    if (packet_window > MAX_WINDOW_ESP32)
        packet_window = MAX_WINDOW_ESP32;

    while (inflight < packet_window &&
           burst_count < allowed_burst &&
           nextSend <= now)
    {
        if (burst_count == 0) {
            Serial.printf("[BURST] starting seq=%ld inflight=%ld window=%ld burstMax=%ld\n",
                          (long)seqnr, (long)inflight,
                          (long)packet_window, (long)MAX_SAFE_BURST);
        }

        sendDataPacket();
        burst_count++;

        if ((burst_count % 3) == 0) receiveAcks();

        if (burst_start == 0) burst_start = now;
        now = prague.Now();
    }

    if (burst_count > 0) {
        // Purista: usar sempre o pacing_rate do Prague;
        // só cai para PRAGUE_MINRATE se for realmente inválido (<= 0).
        rate_tp rate = pacing_rate > 0 ? pacing_rate : PRAGUE_MINRATE;

        time_tp burst_duration =
            (time_tp)((packet_size * (size_tp)burst_count * 1000000ULL) / rate);

        // Apenas logar se algo parecer fora do normal
        if (burst_duration <= 0) {
            Serial.printf("[WARN] burst_duration=%ld us (rate=%llu, burst=%ld, pkt=%llu)\n",
                          (long)burst_duration,
                          (unsigned long long)rate,
                          (long)burst_count,
                          (unsigned long long)packet_size);
        }

        nextSend = burst_start + burst_duration;
    }

    delayMicroseconds(100);
}