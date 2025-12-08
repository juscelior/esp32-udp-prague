# ESP32 UDP Prague

## Overview

This project implements a **UDP-based communication protocol** for ESP32 microcontrollers using the Prague communication framework. The server component is based on a stable fork to ensure version consistency and compatibility across different environments and team repositories.

## Server Implementation

The UDP server implementation is based on the following fork:

**Repository**: [juscelior/udp_prague](https://github.com/juscelior/udp_prague)

### Why a Fork?

This fork was created to ensure **version stability** and **compatibility consistency**:
- Preserves a known working version of the UDP Prague server
- Protects against breaking changes if the original L4S team repository is modified
- Provides a stable reference point for this project's development
- Ensures reproducibility across team environments

## Project Structure

```
AsyncUDPClient/
├── AsyncUDPClient.ino      # Main ESP32 sketch
├── prague_cc.cpp           # Prague communication codec implementation
├── prague_cc.h             # Prague communication header
├── ci.yml                  # CI/CD configuration
└── README.MD               # This file
```

## Components

### AsyncUDPClient.ino
Main Arduino sketch for ESP32 that implements:
- Asynchronous UDP client operations
- Prague protocol message handling
- Network communication setup and management

### prague_cc.cpp / prague_cc.h
Prague communication codec implementation:
- Message encoding/decoding
- Protocol-level communication primitives
- Data serialization utilities

## Prerequisites

- **Hardware**: ESP32 microcontroller with WiFi capability
- **Arduino IDE**: Version 1.8.x or higher
- **Libraries**: 
  - AsyncUDP (for asynchronous UDP operations)
  - Core ESP32 libraries

## Getting Started

1. Clone or download this repository
2. Open `AsyncUDPClient.ino` in the Arduino IDE
3. Configure your WiFi credentials and target server address
4. Select ESP32 board and appropriate COM port
5. Upload the sketch to your device

## Configuration

Key parameters to configure in `AsyncUDPClient.ino`:

```cpp
// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Server configuration
const char* server_ip = "YOUR_SERVER_IP";
const uint16_t server_port = YOUR_PORT;
```

## Dependencies & References

- **UDP Prague Server**: [juscelior/udp_prague](https://github.com/juscelior/udp_prague)
- **Original L4S Repository**: Contact the L4S team for access
- **ESP32 Arduino Core**: https://github.com/espressif/arduino-esp32

## License

Refer to the original Prague protocol repository for licensing information.

## Support

For issues or questions related to this specific implementation, please refer to the code comments and the forked UDP Prague repository documentation.

---

Test on server
sudo tcpdump -i any -vvv -n 'udp and port 5005'

*Last Updated: November 2025*
