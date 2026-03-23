# TODOS — ESP32 UDP Prague L4S IoT Master Project

> Generated from full code audit + upstream comparison + academic methodology assessment.
> Priority: P0 = Critical (blocks correctness), P1 = High (affects results), P2 = Medium (quality).

---

## Phase 1: Critical Bug Fixes (P0)

### 1.1 — Fix `m_ts_remote` timestamp protocol (prague_cc.cpp)
- **File:** `esp32dev/src/prague_cc.cpp` line ~282
- **Bug:** `m_ts_remote = timestamp;` stores the raw peer timestamp instead of the clock offset.
- **Upstream:** `m_ts_remote = ts - timestamp;` (stores `local_now - peer_timestamp`).
- **Impact:** All internal RTT calculations, `echoed_timestamp` in `GetTimeInfo()`, and Prague's AIMD decisions are affected. The CC algorithm cannot adapt correctly.
- **Fix:** Change to `m_ts_remote = ts - timestamp;`

### 1.2 — Fix RTT CSTATS calculation (UDPPragueClient.ino)
- **File:** `esp32dev/src/UDPPragueClient.ino` line ~217
- **Bug:** `time_tp rtt = now - ack.echoed_timestamp;` uses `micros()` subtracted from server's `echoed_timestamp`. The `echoed_timestamp` from the server's `GetTimeInfo()` is a *delta* (offset-adjusted), not an absolute send timestamp. This only produces correct RTT values if the server's Prague CC is also correctly implemented.
- **Impact:** CSTATS log RTT/jitter values are unreliable for academic analysis. Prague's internal `m_rtt` (from `PacketReceived`) may also be affected.
- **Fix:** Use `prague.Now()` instead of `micros()` for consistency. After Bug 1.1 is fixed, the RTT from `PacketReceived` will be correct; the CSTATS RTT should match.

### 1.3 — Apply `snd_ecn` to socket (UDPPragueClient.ino)
- **File:** `esp32dev/src/UDPPragueClient.ino` `sendDataPacket()` function
- **Bug:** `prague.GetTimeInfo(msg->timestamp, msg->echoed_timestamp, snd_ecn)` computes `snd_ecn` but it is **never applied** via `setsockopt(IP_TOS)`. The socket TOS is set once in `setup()` and never updated.
- **Impact:** When Prague detects `error_L4S`, it sets `snd_ecn = ecn_not_ect` (fallback), but packets still go out with ECT(1). The L4S error-recovery mechanism is completely broken.
- **Fix:** After `GetTimeInfo()`, call `setsockopt(sockfd, IPPROTO_IP, IP_TOS, &snd_ecn, sizeof(snd_ecn))` before `sendto()`.

---

## Phase 2: Upstream Divergences (P1)

### 2.1 — Port upstream UDPSocket rewrite
- **Files:** `udp_prague_base/udpsocket.h` (current: monolithic header, no error handling)
- **Upstream:** Split into `udpsocket.h` + `udpsocket.cpp` with `UDPSocket` class, `Endpoint` struct, IPv6 support, `std::system_error` exceptions, proper RAII.
- **Priority:** P1 — Current server may silently fail (e.g., bind errors, ECN cmsg failures). Causes the "server not showing logs on new machine" issue.

### 2.2 — Add virtual destructor to PragueCC
- **Upstream** added `virtual ~PragueCC() = default;` to the header.
- **Impact:** Low for ESP32 (no polymorphic usage), but good practice.

### 2.3 — Upstream removed `cca_cubic` mode
- ESP32 local copy still has the full Cubic implementation (~100 lines).
- **Decision needed:** Keep Cubic for comparison experiments, or remove to stay in sync?

### 2.4 — Port `pkt_format.h` updates
- Upstream added `framemessage_t`, `rfc8888ack_t`, `BULK_DATA_TYPE = 1`, `PKT_ACK_TYPE = 17`.
- ESP32 uses inline `#pragma pack` structs. Should align naming/types.

---

## Phase 3: Statistical Rigor (P1)

### 3.1 — Add confidence intervals (95% CI)
- All reported means (RTT, jitter, throughput, pacing rate) must include 95% CI.
- Use `scipy.stats.t.interval()` or bootstrap.

### 3.2 — Add hypothesis testing
- Shapiro-Wilk normality test on key metrics.
- Mann-Whitney U or Welch's t-test for cross-scenario comparisons.

### 3.3 — Add box plots
- Replace or supplement time-series plots with box plots for cross-scenario comparison.

### 3.4 — Add steady-state segmentation
- Exclude the first 10% of samples (slow-start / warmup phase).
- Report metrics separately for warmup vs. steady-state.

### 3.5 — Add coefficient of variation (CV)
- Report CV for key metrics to assess measurement stability.

---

## Phase 4: Notebook Updates (P1)

### 4.1 — Fix server log parser for verbose format
- **Current:** Cell 6 only parses `[RECVER]` bucket format.
- **New data:** Server logs now use verbose per-packet `r:/s:` format.
- **Fix:** Auto-detect format and aggregate verbose packets into 200ms buckets.

### 4.2 — Add server coverage warning
- If server data covers < 90% of client duration, warn and annotate in outputs.
- T06 example: server captured 1,154 of 39,071 packets (2.9% coverage).

### 4.3 — Update transmission analysis cell
- Handle empty `df_server` gracefully with client-only analysis.

### 4.4 — Update CSV export with CI columns
- Add `rtt_ci_lower`, `rtt_ci_upper`, `cv_rtt`, etc. to summary CSV.

---

## Phase 5: Lab / Methodology Improvements (P2)

### 5.1 — Run server WITHOUT `-v` flag
- Verbose mode causes I/O backpressure → server drops 97% of packets.
- Use default `[RECVER]` bucket format for full 600s coverage.

### 5.2 — Multiple repetitions per scenario
- Current: 1 run per scenario → no statistical power.
- Target: ≥ 3 runs per scenario (5 ideal for publication).

### 5.3 — Automate experiment execution
- Script to iterate over all 36 scenarios with proper naming.
- Include cooldown period between experiments.

### 5.4 — Validate struct alignment end-to-end
- Ensure `datamessage_t` (13 bytes) and `ackmessage_t` (26 bytes) match exactly between ESP32 and server.
- Write a unit test or handshake validation.

### 5.5 — Add network path characterization
- Measure baseline RTT (no load) before each experiment.
- Record WiFi RSSI at start and end.

---

## Status Legend
- [ ] Not started
- [~] In progress
- [x] Completed
