#include <WiFi.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "prague_cc.h"

// ===== WIFI CONFIG =====
const char* ssid     = "JEED-TH001";
const char* password = "";

// ===== RECEIVER CONFIG =====
// The IP of the machine running udp_prague_receiver
const char* server_ip   = "3.16.158.161";
const int   server_port = 5005;

// ===== SOCKET & ADDRESS =====
int sockfd;
struct sockaddr_in dest_addr;

// ===== PRAGUE CC =====
PragueCC prague;
time_tp  nextSend;
count_tp seqnr;
count_tp inflight;
count_tp packets_sent = 0;

rate_tp  pacing_rate;
count_tp packet_window;
count_tp packet_burst;
size_tp  packet_size;

unsigned long lastAckMs = 0;

// ===== MESSAGE STRUCTS (same as original project) =====
#pragma pack(push, 1)
struct datamessage_t {
    time_tp  timestamp;
    time_tp  echoed_timestamp;
    count_tp seq_nr;
};

struct ackmessage_t {
    time_tp  timestamp;
    time_tp  echoed_timestamp;
    count_tp packets_received;
    count_tp packets_CE;
    count_tp packets_lost;
    bool     error_L4S;
};
#pragma pack(pop)

// ===== RECEIVE ACKs =====
void receiveAcks() {
    ackmessage_t ack;
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    int len = recvfrom(sockfd, &ack, sizeof(ack), MSG_DONTWAIT,
                       (struct sockaddr*)&source_addr, &socklen);

    if (len == sizeof(ackmessage_t)) {

        // ESP32 cannot read ECN bits from incoming packets,
        // so we pass a neutral ECN value.
        ecn_tp rcv_ecn = ecn_not_ect;

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

        Serial.printf("ACK received: recv=%llu  CE=%llu  lost=%llu\n",
                      (unsigned long long)ack.packets_received,
                      (unsigned long long)ack.packets_CE,
                      (unsigned long long)ack.packets_lost);

        lastAckMs = millis();
    }
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== ESP32 UDP_Prague Sender with ECN = 1 ===");

    // ---- WIFI ----
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");

    // ---- CREATE UDP SOCKET ----
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sockfd < 0) {
        Serial.println("Failed to create UDP socket!");
        while(true);
    }

    // ---- SET ECN = 1 (ECT(1)) ----
    int tos = 0x01;   // ECN bits = 01
    if (setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == 0) {
        Serial.println("ECN = 1 (ECT(1)) successfully configured!");
    } else {
        Serial.println("Failed to set ECN!");
    }

    // ---- DESTINATION ADDRESS ----
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(server_port);
    inet_aton(server_ip, &dest_addr.sin_addr);

    // ---- PRAGUE CC INIT ----
    nextSend = prague.Now();
    seqnr    = 1;
    inflight = 0;

    prague.GetCCInfo(pacing_rate, packet_window, packet_burst, packet_size);

    Serial.println("PragueCC initialized.");
}

// ===== MAIN LOOP =====
void loop() {
    time_tp now = prague.Now();

    // Process ACKs as soon as they arrive
    receiveAcks();

    count_tp inburst = 0;
    time_tp startSend = 0;

    // Try to send packets only when:
    // - pacing allows (nextSend <= now)
    // - window allows
    // - burst size allows
    while ((inflight < packet_window) &&
           (inburst < packet_burst) &&
           (nextSend <= now))
    {
        datamessage_t msg;
        ecn_tp snd_ecn;

        prague.GetTimeInfo(msg.timestamp, msg.echoed_timestamp, snd_ecn);
        msg.seq_nr = seqnr;

        sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr*)&dest_addr,
               sizeof(dest_addr));

        inflight++;
        inburst++;
        seqnr++;
        packets_sent++;

        if (startSend == 0)
            startSend = now;

        Serial.printf("Sent seq=%ld inflight=%ld rate=%llu win=%ld burst=%ld\n",
                      (long)(seqnr - 1),
                      (long)inflight,
                      (unsigned long long)pacing_rate,
                      (long)packet_window,
                      (long)packet_burst);

        now = prague.Now();
    }

    // Set the next pacing time
    if (startSend != 0 && inburst > 0) {
        nextSend = startSend +
            (packet_size * inburst * 1000000ULL / pacing_rate);
    }

    // Yield CPU instead of blocking timing
    delayMicroseconds(100); 
}

