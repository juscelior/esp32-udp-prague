# TODOS — ESP32 UDP Prague L4S IoT Master Project

> Generated from full code audit + upstream comparison + academic methodology assessment.
> Priority: P0 = Critical (blocks correctness), P1 = High (affects results), P2 = Medium (quality).
>
> **Progress: 16/21 completed (76%)** — Last updated: 2026-03-23
>
> | Phase | Done | Total | Status |
> |-------|------|-------|--------|
> | 1 — Critical Bug Fixes (P0) | 3 | 3 | **Complete** |
> | 2 — Upstream Divergences (P1) | 4 | 4 | **Complete** |
> | 3 — Statistical Rigor (P1) | 5 | 5 | **Complete** |
> | 4 — Notebook Updates (P1) | 4 | 4 | **Complete** |
> | 5 — Lab / Methodology (P2) | 0 | 5 | Pending (requires lab execution) |

---

## Phase 1: Critical Bug Fixes (P0)

### 1.1 — [x] Fix `m_ts_remote` timestamp protocol (prague_cc.cpp)
- **File:** `esp32dev/src/prague_cc.cpp` line ~280
- **Bug:** `m_ts_remote = timestamp;` stores the raw peer timestamp instead of the clock offset.
- **Upstream:** `m_ts_remote = ts - timestamp;` (stores `local_now - peer_timestamp`).
- **Impact:** All internal RTT calculations, `echoed_timestamp` in `GetTimeInfo()`, and Prague's AIMD decisions are affected. The CC algorithm cannot adapt correctly.
- **Fix:** Change to `m_ts_remote = ts - timestamp;`
- **Status:** Fixed — code now uses `m_ts_remote = ts - timestamp;` at line 280.

### 1.2 — [x] Fix RTT CSTATS calculation (UDPPragueClient.ino)
- **File:** `esp32dev/src/UDPPragueClient.ino` line ~213
- **Bug:** `time_tp rtt = now - ack.echoed_timestamp;` uses `micros()` subtracted from server's `echoed_timestamp`. The `echoed_timestamp` from the server's `GetTimeInfo()` is a *delta* (offset-adjusted), not an absolute send timestamp. This only produces correct RTT values if the server's Prague CC is also correctly implemented.
- **Impact:** CSTATS log RTT/jitter values are unreliable for academic analysis. Prague's internal `m_rtt` (from `PacketReceived`) may also be affected.
- **Fix:** Use `prague.Now()` instead of `micros()` for consistency. After Bug 1.1 is fixed, the RTT from `PacketReceived` will be correct; the CSTATS RTT should match.
- **Status:** Fixed — code now uses `prague.Now()` at lines 213–214.

### 1.3 — [x] Apply `snd_ecn` to socket (UDPPragueClient.ino)
- **File:** `esp32dev/src/UDPPragueClient.ino` `sendDataPacket()` function
- **Bug:** `prague.GetTimeInfo(msg->timestamp, msg->echoed_timestamp, snd_ecn)` computes `snd_ecn` but it is **never applied** via `setsockopt(IP_TOS)`. The socket TOS is set once in `setup()` and never updated.
- **Impact:** When Prague detects `error_L4S`, it sets `snd_ecn = ecn_not_ect` (fallback), but packets still go out with ECT(1). The L4S error-recovery mechanism is completely broken.
- **Fix:** After `GetTimeInfo()`, call `setsockopt(sockfd, IPPROTO_IP, IP_TOS, &snd_ecn, sizeof(snd_ecn))` before `sendto()`.
- **Status:** Fixed — `setsockopt()` is now called after `GetTimeInfo()` at lines 314–315.

---

## Phase 2: Upstream Divergences (P1)

### 2.1 — [x] Port upstream UDPSocket rewrite
- **Files:** `udp_prague_base/udpsocket.h` (current: monolithic header, no error handling)
- **Upstream:** Split into `udpsocket.h` + `udpsocket.cpp` with `UDPSocket` class, `Endpoint` struct, IPv6 support, `std::system_error` exceptions, proper RAII.
- **Priority:** P1 — Current server may silently fail (e.g., bind errors, ECN cmsg failures). Causes the "server not showing logs on new machine" issue.
- **Status:** Done — `udp_prague_base/` atualizado com upstream L4STeam/udp_prague: udpsocket.h (declarações) + udpsocket.cpp (implementação com Endpoint, SocketHandle, error handling, IPv6, RAII).

### 2.2 — [x] Add virtual destructor to PragueCC
- **Upstream** added `virtual ~PragueCC() = default;` to the header.
- **Impact:** Low for ESP32 (no polymorphic usage), but good practice.
- **Status:** Done — destrutor marcado como `virtual`, implementação usa `= default`. Métodos `get_ref_rtt()` e `get_alpha_shift()` do upstream também adicionados.

### 2.3 — [x] Upstream removed `cca_cubic` mode
- ESP32 local copy still has the full Cubic implementation (~100 lines).
- **Decision needed:** Keep Cubic for comparison experiments, or remove to stay in sync?
- **Status:** Done — Cubic removido: enum `cca_cubic`, variáveis de estado (`m_cubic_*`, `m_rtt_min`), helpers (`fls`, `fls64`, `CubicRoot`), constantes (`BETA`, `C_SCALED`, etc.) e branches de código. Alinhado com upstream.

### 2.4 — [x] Port `pkt_format.h` updates
- Upstream added `framemessage_t`, `rfc8888ack_t`, `BULK_DATA_TYPE = 1`, `PKT_ACK_TYPE = 17`.
- ESP32 uses inline `#pragma pack` structs. Should align naming/types.
- **Status:** Done — `pkt_format.h` já estava idêntico ao upstream. Base atualizada com versão upstream atual. ESP32 mantém structs inline no `.ino` (intencional para ambiente Arduino).

---

## Phase 3: Statistical Rigor (P1)

### 3.1 — [x] Add confidence intervals (95% CI)
- All reported means (RTT, jitter, throughput, pacing rate) must include 95% CI.
- Use `scipy.stats.t.interval()` or bootstrap.
- **Status:** Implemented in `metrics_academic.ipynb` Cell 18/20 using `scipy.stats.t.interval()`.

### 3.2 — [x] Add hypothesis testing
- Shapiro-Wilk normality test on key metrics.
- Mann-Whitney U or Welch's t-test for cross-scenario comparisons.
- **Status:** Shapiro-Wilk (Cell 20) + Mann-Whitney U pairwise cross-scenario comparison (Cell 22) implemented. Exports `cross_scenario_mannwhitney.csv` and heatmaps.

### 3.3 — [x] Add box plots
- Replace or supplement time-series plots with box plots for cross-scenario comparison.
- **Status:** Implemented in Cell 20 — 4-panel box plots with mean line and 95% CI shading.

### 3.4 — [x] Add steady-state segmentation
- Exclude the first 10% of samples (slow-start / warmup phase).
- Report metrics separately for warmup vs. steady-state.
- **Status:** Implemented in Cell 20 — excludes first 10% warmup samples.

### 3.5 — [x] Add coefficient of variation (CV)
- Report CV for key metrics to assess measurement stability.
- **Status:** Implemented in Cell 20 — `compute_cv()` exported as `rtt_cv_pct` in CSV.

---

## Phase 4: Notebook Updates (P1)

### 4.1 — [x] Fix server log parser for verbose format
- **Current:** Cell 6 only parses `[RECVER]` bucket format.
- **New data:** Server logs now use verbose per-packet `r:/s:` format.
- **Fix:** Auto-detect format and aggregate verbose packets into 200ms buckets.
- **Status:** Implemented in Cell 5 — auto-detects format and aggregates verbose packets into 200ms buckets.

### 4.2 — [x] Add server coverage warning
- If server data covers < 90% of client duration, warn and annotate in outputs.
- T06 example: server captured 1,154 of 39,071 packets (2.9% coverage).
- **Status:** Implemented in Cell 11 — warns if coverage < 90%, exports `server_coverage_pct` to CSV.

### 4.3 — [x] Update transmission analysis cell
- Handle empty `df_server` gracefully with client-only analysis.
- **Status:** Implemented in Cell 11 — handles empty `df_server` gracefully.

### 4.4 — [x] Update CSV export with CI columns
- Add `rtt_ci_lower`, `rtt_ci_upper`, `cv_rtt`, etc. to summary CSV.
- **Status:** Implemented in Cell 18 — full export with `rtt_ci_lower_ms`, `rtt_ci_upper_ms`, `rtt_cv_pct`, etc.

---

## Phase 5: Lab / Methodology Improvements (P2)

### 5.1 — [ ] Run server WITHOUT `-v` flag
- Verbose mode causes I/O backpressure → server drops 97% of packets.
- Use default `[RECVER]` bucket format for full 600s coverage.

### 5.2 — [ ] Multiple repetitions per scenario
- Current: 1 run per scenario → no statistical power.
- Target: ≥ 3 runs per scenario (5 ideal for publication).

### 5.3 — [ ] Automate experiment execution
- Script to iterate over all 36 scenarios with proper naming.
- Include cooldown period between experiments.

### 5.4 — [ ] Validate struct alignment end-to-end
- Ensure `datamessage_t` (13 bytes) and `ackmessage_t` (26 bytes) match exactly between ESP32 and server.
- Write a unit test or handshake validation.

### 5.5 — [ ] Add network path characterization
- Measure baseline RTT (no load) before each experiment.
- Record WiFi RSSI at start and end.

---

## Status Legend
- [ ] Not started
- [~] In progress
- [x] Completed
