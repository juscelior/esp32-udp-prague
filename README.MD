# ESP32 UDP Prague Client

## Overview

This repository contains an ESP32 UDP client that uses the Prague congestion control algorithm and interoperates with a reference UDP Prague server. The goal is to generate reproducible experiments for L4S/ECN evaluation using an ESP32 microcontroller as the sender and a Linux host as the receiver.

The UDP server component is based on a dedicated fork to ensure version stability and compatibility across different environments.

## Server implementation

The UDP Prague server used by this project is available at:

- **Repository**: [juscelior/udp_prague](https://github.com/juscelior/udp_prague)

The fork is used to:
- Preserve a known working version of the UDP Prague server.
- Avoid breaking changes if the original L4S repository evolves.
- Provide a stable reference point for all experiments.

### Local copy in `udp_prague_base/`

This repository also contains a local snapshot of the server sources under the folder `udp_prague_base/`.

- The files in `udp_prague_base/` are a copy of the code from the repository [juscelior/udp_prague](https://github.com/juscelior/udp_prague).
- The repository `juscelior/udp_prague` itself is a fork of [L4STeam/udp_prague](https://github.com/L4STeam/udp_prague).
- This copy is kept only to record exactly which version of the server was used when this work was developed.

## Project structure

```text
esp32dev/                    # PlatformIO project for the ESP32 client
  ├── platformio.ini         # PlatformIO configuration (board, monitor speed, etc.)
  └── src/
      └── UDPPragueClient.ino  # Main ESP32 Prague client code

metrics.ipynb                # Jupyter notebook for log analysis
server.log                   # Example server log
```

The recommended and maintained path for new experiments is the PlatformIO project under `esp32dev/`.

## ESP32 client (PlatformIO)

### Prerequisites

- **Hardware**: ESP32 development board with Wi‑Fi capability.
- **Software**:
  - Visual Studio Code.
  - [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode).
  - USB driver for the ESP32 so that a serial port such as `/dev/tty.usbserial-*` is available.

### PlatformIO configuration

The `esp32dev/platformio.ini` file should contain a configuration similar to:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

monitor_speed = 115200
```

### Building and uploading

From VS Code with PlatformIO:

1. Open the folder `esp32dev/` as the workspace.
2. Connect the ESP32 board via USB.
3. Use the PlatformIO side bar:
   - **Build** to compile the firmware.
   - **Upload** to flash the firmware to the board.

Alternatively, from a terminal inside `esp32dev/`:

```bash
pio run           # build
pio run -t upload # build and upload
```

### Wi‑Fi and server configuration

In `esp32dev/src/UDPPragueClient.ino`, configure your Wi‑Fi network and the target server:

```cpp
// Wi-Fi credentials
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Server configuration
const char* server_ip   = "YOUR_SERVER_IP"; // e.g., AWS public IP
const int   server_port = 5005;             // UDP Prague server port
```

The sketch also exposes several **test scenarios** (baseline, medium, high load, burst, jumbo) that define payload size and duration. You can enable one scenario at a time by defining the corresponding macro near the top of `UDPPragueClient.ino`.

## Capturing ESP32 logs (CSTATS)

The ESP32 client emits compact, structured log lines for analysis. Each acknowledgment from the server produces a line with the following format:

```text
CSTATS;ms_since_start;recv_pkts;inflight;rtt_us;jitter_us;win;burst;rate_Bps
```

These lines can be captured from the serial port and later processed in Python or a Jupyter notebook.

### Using PlatformIO serial monitor

From the `esp32dev/` directory, run:

```bash
pio device monitor
```

To capture and save all output to a file for later analysis:

```bash
pio device monitor | tee ../esp_client.log
```

The resulting `esp_client.log` file can then be parsed by the notebook `metrics.ipynb`, filtering only the lines that start with `CSTATS;`.

## Server-side capture example

On the Linux server running the UDP Prague receiver you can use `tcpdump` to observe incoming traffic:

```bash
sudo tcpdump -i any -vvv -n 'udp and port 5005'
```

## References

- UDP Prague server fork: https://github.com/juscelior/udp_prague
- ESP32 Arduino core: https://github.com/espressif/arduino-esp32

---

*Last updated: December 2025*
