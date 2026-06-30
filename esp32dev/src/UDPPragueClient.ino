#include <WiFi.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "prague_cc.h"
#include <errno.h>
#include "esp_timer.h"

// ===== CONGESTION CONTROL MODE =====
// Default: adaptive Prague CC. Uncomment to use a fixed baseline mode.
// #define CC_MODE_BASELINE

// ===== ECN SENDER MARKING =====
// 1 = mark packets as ECN-capable (ECT(1)); 0 = Not-ECT.
#define ECN_SENDER_ENABLE 1

// ===== IOT NODE IDENTIFIER =====
#define IOT_NODE_ID 1

// ===== GATEWAY NETWORK CONFIGURATION =====
// Set these per experiment to document gateway bottleneck settings.
const char* GW_CC_ALGO = "prague";
const char* GW_QDISC   = "dualpi2";

// ===== WIFI CONFIG =====
const char* ssid     = "l4siotmaster";
const char* password = "masterl4s";

// ===== RECEIVER CONFIG =====
const char* server_ip   = "20.62.8.146";
const int   server_port = 5005;

// ===== TEST SCENARIO CONFIGURATION =====
//#define SCENARIO_BASELINE
//#define SCENARIO_MEDIUM
#define SCENARIO_HIGH
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
    #define TEST_DURATION_SEC 300
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

// ===== ESP32 SAFETY LIMITS =====
// MAX_WINDOW_ESP32 > 0 applies a local inflight cap; -1 keeps Prague-only control.
static const int MAX_WINDOW_ESP32 = -1;
static const int MAX_BURST_ESP32  = 5;     // safe burst size

// ===== ADAPTIVE BURST =====
static count_tp MAX_SAFE_BURST = 3;        // conservative starting burst
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

unsigned long error_count = 0;     // total sendto() errors
unsigned long enobufs_count = 0;   // ENOBUFS only
unsigned long other_errors = 0;    // all other sendto() errors

// ===== ACK-SILENCE WATCHDOG (FIX #2) =====
// If sending is blocked and no valid ACK arrives within ACK_TIMEOUT_MS,
// treat inflight packets as lost, reset Prague CC, and send a probe.
unsigned long last_ack_ms = 0;
const unsigned long ACK_TIMEOUT_MS = 2000;   // > pior RTT observado (~330 ms)
unsigned long watchdog_count = 0;            // number of watchdog triggers

// ===== CLIENT RTT/JITTER METRICS =====
time_tp last_rtt = 0;
bool    has_last_rtt = false;
time_tp rtt_min = 0, rtt_max = 0;
int64_t rtt_sum = 0;
uint32_t rtt_count = 0;

// ===== [INSTR] CONTROLLER CPU COST (per ACK) =====
// Tracks Prague/baseline update time in microseconds using esp_timer.
int64_t  cc_us_sum   = 0;   // total CC update time (us)
int64_t  cc_us_max   = 0;   // worst observed CC update time (us)
uint32_t cc_us_count = 0;   // number of measured updates

// ===== [INSTR] RAM FOOTPRINT (runtime) =====
uint32_t heap_at_start = 0;            // free heap at run start (after Wi-Fi)
uint32_t heap_free_min = 0xFFFFFFFF;   // minimum free heap seen (peak usage)

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

// ===== STATIC BUFFER =====
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

    // Size guard in case server struct changes in the future.
    if (len != (int)sizeof(ackmessage_t)) {
        Serial.printf("[ACK-IGNORED] len=%d (expected %u)\n", len, (unsigned)sizeof(ackmessage_t));
        return;
    }

    ack.ntoh();

    if (ack.type != 17) {
        Serial.printf("[ACK-IGNORED] invalid type=%u\n", (unsigned)ack.type);
        return;
    }

    // Defensive counter sanity check.
    if (ack.packets_received < 0 || ack.packets_CE < 0 || ack.packets_lost < 0) {
        Serial.printf("[ACK-IGNORED] negative counters: recv=%ld CE=%ld lost=%ld\n",
                      (long)ack.packets_received,
                      (long)ack.packets_CE,
                      (long)ack.packets_lost);
        return;
    }

    // ===== FIX #2: record timestamp of the last valid ACK (watchdog) =====
    last_ack_ms = millis();

    // ===== RTT & JITTER (client-side) =====
    // RTT = current time - echoed timestamp (server offset-adjusted).
    time_tp now = prague.Now();
    time_tp rtt = now - ack.echoed_timestamp;
    if (rtt < 0) {
        // Defensive: ignore wrap-around artifacts.
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

    // Aggregate stats.
    if (rtt_count == 0) {
        rtt_min = rtt_max = rtt;
    } else {
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;
    }
    rtt_sum += rtt;
    rtt_count++;

    // ===== [INSTR] start controller CPU-cost timing =====
    int64_t cc_t0 = esp_timer_get_time();

    // Feed congestion control (Prague or baseline).
#ifndef CC_MODE_BASELINE
    // Full Prague: adapt rate/window/burst from ACK feedback.
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

    // Apply optional ESP32 local window cap.
    if (MAX_WINDOW_ESP32 > 0) {
        packet_window = min(packet_window, (count_tp)MAX_WINDOW_ESP32);
    }
#else
    // Baseline mode: fixed window/burst, no adaptation.
    if (MAX_WINDOW_ESP32 > 0) {
        packet_window = MAX_WINDOW_ESP32;
    }
    packet_burst  = MAX_BURST_ESP32;
    // pacing_rate/packet_size come from initial PragueCC setup().
#endif

    // ===== [INSTR] end timing and accumulate CC update cost =====
    int64_t cc_dt = esp_timer_get_time() - cc_t0;
    cc_us_sum += cc_dt;
    if (cc_dt > cc_us_max) cc_us_max = cc_dt;
    cc_us_count++;

    // ===== RECALCULATE ACTUAL INFLIGHT (FIX #1) =====
    // Lost packets are never received; include losses to avoid inflight deadlock.
    count_tp accounted = ack.packets_received + ack.packets_lost;
    if (accounted <= packets_sent) {
        inflight = packets_sent - accounted;
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

    // ===== Structured log for analysis (CSTATS) =====
    unsigned long ms_since_start = millis() - test_start_ms;
    Serial.printf(
        "CSTATS;%lu;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%llu\n",
        ms_since_start,
        (long)packets_sent,
        (long)ack.packets_received,
        (long)inflight,
        (long)rtt,
        (long)jitter,
        (long)packet_window,
        (long)packet_burst,
        (unsigned long long)pacing_rate
    );

    // ===== [INSTR] sample free heap and keep minimum =====
    uint32_t heap_now = ESP.getFreeHeap();
    if (heap_now < heap_free_min) heap_free_min = heap_now;
}

// ===== SEND DATA PACKET =====
void sendDataPacket() {
    size_t total_size = sizeof(datamessage_t) + EXTRA_PAYLOAD_SIZE;

    datamessage_t* msg = (datamessage_t*) tx_buffer;
    ecn_tp snd_ecn;

    prague.GetTimeInfo(msg->timestamp, msg->echoed_timestamp, snd_ecn);

    #if !ECN_SENDER_ENABLE
        snd_ecn = ecn_not_ect;   // Not-ECT scenario: keep Not-ECT
    #endif

    msg->seq_nr = seqnr;
    msg->hton();

    // Apply ECN marking from Prague CC.
    int tos_val = (int)snd_ecn;
    setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos_val, sizeof(tos_val));

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

        // Adaptive burst backoff on send queue pressure.
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
    Serial.printf("IoT Node ID: %d\n", IOT_NODE_ID);

    Serial.printf("Test Scenario: %s\n", TEST_NAME);
    Serial.printf("ECN: %d\n", ECN_SENDER_ENABLE);
    Serial.printf("Payload Size: %u bytes (header: %u + extra: %u)\n",
                  (unsigned)(sizeof(datamessage_t) + EXTRA_PAYLOAD_SIZE),
                  (unsigned)sizeof(datamessage_t),
                  EXTRA_PAYLOAD_SIZE);
    Serial.printf("Initial Burst Limit: %ld packets\n", (long)MAX_SAFE_BURST);
    Serial.printf("Duration: %u seconds\n", TEST_DURATION_SEC);
    Serial.printf("Gateway CC Algorithm: %s\n", GW_CC_ALGO);
    Serial.printf("Gateway Qdisc: %s\n", GW_QDISC);
    Serial.println("======================================\n");

    printDeviceInfo();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print("*");
    }

    Serial.printf("\nWiFi SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("\nWiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("IP: %s\n\n", WiFi.localIP().toString().c_str());

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    // Configure IP TOS/ECN bits for outgoing packets.
    // Low 2 bits: 00 = Not-ECT, 01 = ECT(1).
    int tos = (ECN_SENDER_ENABLE ? 0x01 : 0x00);
    setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(server_port);
    inet_aton(server_ip, &dest_addr.sin_addr);

    seqnr = 1;
    prague.GetCCInfo(pacing_rate, packet_window, packet_burst, packet_size);

    nextSend = prague.Now() + 2000;
    test_start_ms = millis();
    last_ack_ms = millis();   // FIX #2: initialize watchdog timer

    // ===== [INSTR] capture startup free heap (after Wi-Fi + socket) =====
    heap_at_start = ESP.getFreeHeap();
    heap_free_min = heap_at_start;
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
            " - Other Errors: %lu\n"
            "Watchdog Resets: %lu\n",
            test_bytes_sent,
            (unsigned long)packets_sent,
            error_count,
            enobufs_count,
            other_errors,
            watchdog_count
        );

        // ===== [INSTR] resource summary (RAM + controller CPU cost) =====
        // Human-readable block plus compact RSTATS line for notebook parsing.
        Serial.printf(
            "=== RESOURCE FOOTPRINT ===\n"
            "CC compute per ACK: avg %.2f us, max %lld us (n=%lu)\n"
            "Heap free at start: %lu bytes\n"
            "Heap free minimum (peak usage): %lu bytes\n"
            "Heap used (peak): %ld bytes\n"
            "ESP min free heap (lifetime): %lu bytes\n"
            "Task stack high-water mark: %u bytes free\n",
            cc_us_count ? (double)cc_us_sum / (double)cc_us_count : 0.0,
            (long long)cc_us_max,
            (unsigned long)cc_us_count,
            (unsigned long)heap_at_start,
            (unsigned long)heap_free_min,
            (long)((int32_t)heap_at_start - (int32_t)heap_free_min),
            (unsigned long)ESP.getMinFreeHeap(),
            (unsigned)uxTaskGetStackHighWaterMark(NULL)
        );
        // Compact parser line (numeric fields only):
        Serial.printf(
            "RSTATS;cc_avg_us=%.2f;cc_max_us=%lld;cc_n=%lu;heap_start=%lu;heap_min=%lu;heap_peak_used=%ld;esp_min_heap=%lu;stack_hwm=%u\n",
            cc_us_count ? (double)cc_us_sum / (double)cc_us_count : 0.0,
            (long long)cc_us_max,
            (unsigned long)cc_us_count,
            (unsigned long)heap_at_start,
            (unsigned long)heap_free_min,
            (long)((int32_t)heap_at_start - (int32_t)heap_free_min),
            (unsigned long)ESP.getMinFreeHeap(),
            (unsigned)uxTaskGetStackHighWaterMark(NULL)
        );

        while (true) delay(1000);
    }

    receiveAcks();

    // ===== FIX #2: ACK-SILENCE WATCHDOG =====
    // If sending is blocked for ACK_TIMEOUT_MS without valid ACKs,
    // treat inflight as lost and reset CC for probe transmission.
    if (inflight >= packet_window && (millis() - last_ack_ms) > ACK_TIMEOUT_MS) {
        watchdog_count++;
        Serial.printf("[WATCHDOG] ACK silence %lums: assuming %ld inflight lost, resetting CC\n",
                      millis() - last_ack_ms, (long)inflight);
        prague.ResetCCInfo();      // same strategy as the Linux reference client
        prague.GetCCInfo(pacing_rate, packet_window, packet_burst, packet_size);
        if (MAX_WINDOW_ESP32 > 0 && packet_window > MAX_WINDOW_ESP32)
            packet_window = MAX_WINDOW_ESP32;
        inflight = 0;
        nextSend = prague.Now();   // allow immediate probe send
        last_ack_ms = millis();    // avoid repeated trigger before probe
    }

    time_tp now = prague.Now();
    time_tp burst_start = 0;
    count_tp burst_count = 0;

    // ===== Limited burst =====
    count_tp allowed_burst = min(packet_burst, MAX_SAFE_BURST);

    // ===== Limited window =====
    if (MAX_WINDOW_ESP32 > 0 && packet_window > MAX_WINDOW_ESP32)
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
        // Prefer Prague pacing_rate; only fall back if invalid (<= 0).
        rate_tp rate = pacing_rate > 0 ? pacing_rate : PRAGUE_MINRATE;

        time_tp burst_duration =
            (time_tp)((packet_size * (size_tp)burst_count * 1000000ULL) / rate);

        // Log only abnormal values.
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