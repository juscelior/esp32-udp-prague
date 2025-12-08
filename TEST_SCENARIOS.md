# Prague/L4S Test Scenarios

This file tracks the experiments you need to run, which variables must be changed for each scenario, and where the logs/results are stored.

Use the checkboxes in the tables to mark what is **done** (`[x]`) and what is **pending** (`[ ]`).

---

## 1. Experiment Folder Structure

For each scenario, create a dedicated folder under `experiments/`:

```text
esp32-udp-prague/
  experiments/
    s01-baseline-no-ecn/
      esp_client.log
      server.log
      prague_performance_enhanced.png
      prague_advanced_metrics.csv
    s02-baseline-bufferbloat/
      ...
    s03-prague-no-ecn/
      ...
    ...
```

Each folder should contain, at minimum:

- `esp_client.log` – raw CSTATS from the ESP32 client.
- `server.log` – raw `[RECVER]` logs from the C++ receiver.
- `prague_performance_enhanced.png` – advanced server-only plots from `metrics.ipynb`.
- `prague_advanced_metrics.csv` – CSV with derived metrics from `metrics.ipynb`.

You can optionally save a copy of the notebook (or an HTML export) alongside the logs.

---

## 2. Knobs and Where to Configure Them

There are **two groups** of parameters:

- what you change **in this repository** (ESP32 client), and
- what you change **in the network environment** (router, `tc`, emulator, etc.).

### 2.1 ESP32 client (in the repo)

All of these live in `esp32dev/src/UDPPragueClient.ino`.

| Knob / Macro          | Description                                                                  | How to change                                                |
| --------------------- | ---------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `CC_MODE_BASELINE`   | When defined, uses a fixed baseline sender (no Prague feedback adaptation). | At the top of `UDPPragueClient.ino` – `#define CC_MODE_BASELINE` or comment it out. |
| `SCENARIO_*`         | Chooses payload/duration pattern: `SCENARIO_BASELINE`, `SCENARIO_MEDIUM`, etc. | Comment/uncomment one of the `#define SCENARIO_...` lines.   |
| `EXTRA_PAYLOAD_SIZE` | Extra bytes per packet (affects effective sending rate).                     | Set inside each `#ifdef SCENARIO_...` block.                 |
| `TEST_DURATION_SEC`  | Duration of each experiment (seconds).                                       | Set inside each `#ifdef SCENARIO_...` block.                 |
| `MAX_WINDOW_ESP32`   | Upper bound on in-flight packets that the Wi‑Fi link will allow.            | Constant near the top of `UDPPragueClient.ino`.             |
| `MAX_BURST_ESP32`    | Upper bound on burst size per send loop.                                    | Constant near the top of `UDPPragueClient.ino`.             |

The **effective send rate** (what you later see as Mbps on the server) is a result of:

- `EXTRA_PAYLOAD_SIZE` (packet size),
- congestion control mode (Prague vs baseline), and
- window/burst limits (`MAX_WINDOW_ESP32`, `MAX_BURST_ESP32`).

You do **not** set `SEND_RATE_MBPS` directly in the code; instead, you choose a scenario (and possibly tweak payload/window) and then measure the resulting rate in the logs.

### 2.2 Network / router side (outside the repo)

These are configured in your router, Linux `tc`, emulator, etc. They do not live as variables in this repository; you just record which values you used for each scenario.

| Parameter             | Description                                                          | Configured Where                                |
| --------------------- | -------------------------------------------------------------------- | ----------------------------------------------- |
| `RTT_BASE_MS`        | Approximate base RTT of the path (without queueing).                | Network topology (router, emulator, etc.).     |
| `QUEUE_TYPE`         | Queueing discipline: `DropTail`, `AQM-RED`, `AQM-PIE`, `L4S-dual`.  | Router / bottleneck configuration.             |
| `ECN_ENABLED`        | Whether ECN marking is enabled on the bottleneck queue.             | Router / OS (e.g., `sysctl`, queue settings).  |
| `LINK_CAPACITY_MBPS` | Bottleneck link capacity.                                           | Router / traffic shaper.                       |
| `BUFFER_TARGET_MS`   | Target queueing delay / buffer size (when configurable).            | Router / AQM profile.                          |

When you run a scenario, make a short note in a `README.md` inside that scenario folder documenting the actual values used (e.g., `EXTRA_PAYLOAD_SIZE=500`, `LINK_CAPACITY_MBPS=2`, etc.).

---

## 3. Scenario Matrix

### 3.1 Baseline (no Prague, no ECN)

Goal: Characterize classic bufferbloat behavior without Prague or ECN, at low and high offered loads.

| ID  | Status | Description                               | CC_MODE  | ECN_ENABLED | QUEUE_TYPE | SEND_RATE_MBPS             | Folder                                  |
| --- | ------ | ----------------------------------------- | -------- | ----------- | ---------- | --------------------------- | --------------------------------------- |
| S01 | [ ]    | Low-load baseline (no congestion)         | BASELINE | off         | DropTail   | well below `LINK_CAPACITY` | `experiments/s01-baseline-no-ecn/`     |
| S02 | [ ]    | High-load baseline (bufferbloat)          | BASELINE | off         | DropTail   | above `LINK_CAPACITY`      | `experiments/s02-baseline-bufferbloat/` |

Notes:
- Use the same `RTT_BASE_MS`, `LINK_CAPACITY_MBPS`, `BUFFER_TARGET_MS` as in the Prague tests so results are comparable.

### 3.2 Prague without ECN/AQM

Goal: See how Prague behaves when there is no ECN/AQM support in the network (only big FIFO buffer).

| ID  | Status | Description                                   | CC_MODE | ECN_ENABLED | QUEUE_TYPE | SEND_RATE_MBPS        | Folder                              |
| --- | ------ | --------------------------------------------- | ------- | ----------- | ---------- | ---------------------- | ----------------------------------- |
| S03 | [ ]    | Prague sender, no ECN, high load (current?)  | PRAGUE  | off         | DropTail   | above `LINK_CAPACITY` | `experiments/s03-prague-no-ecn/`   |

Notes:
- The experiment you have already run (very high RTT growth, zero loss and zero ECN marks) likely matches this scenario. Once you move the logs into the folder, mark S03 as done.

### 3.3 Prague with ECN/AQM enabled

Goal: Evaluate Prague in an L4S-friendly network with ECN marking and an AQM (or dual-queue) configured.

| ID  | Status | Description                                        | CC_MODE | ECN_ENABLED | QUEUE_TYPE        | SEND_RATE_MBPS             | Folder                                  |
| --- | ------ | -------------------------------------------------- | ------- | ----------- | ----------------- | --------------------------- | --------------------------------------- |
| S04 | [ ]    | Moderate load, Prague + ECN/AQM                    | PRAGUE  | on          | AQM/L4S-compatible | around `LINK_CAPACITY`     | `experiments/s04-prague-ecn-medium/`   |
| S05 | [ ]    | High load, Prague + ECN/AQM (stress test)          | PRAGUE  | on          | AQM/L4S-compatible | above `LINK_CAPACITY`      | `experiments/s05-prague-ecn-high/`     |

Expected observations:
- RTT should remain bounded (small queueing delay) compared to S02/S03.
- ECN marks should be non-zero; loss rates should remain very low.
- Goodput close to 1.0 for stable periods.

### 3.4 RTT Sensitivity

Goal: Understand how Prague + ECN behaves with different base RTTs.

| ID  | Status | Description                            | CC_MODE | ECN_ENABLED | QUEUE_TYPE        | RTT_BASE_MS                 | Folder                               |
| --- | ------ | -------------------------------------- | ------- | ----------- | ----------------- | --------------------------- | ------------------------------------ |
| S06 | [ ]    | Prague + ECN, low base RTT            | PRAGUE  | on          | AQM/L4S-compatible | e.g. 5–10 ms               | `experiments/s06-prague-ecn-rttlow/` |
| S07 | [ ]    | Prague + ECN, higher base RTT         | PRAGUE  | on          | AQM/L4S-compatible | e.g. 50–100 ms             | `experiments/s07-prague-ecn-rtthigh/`|

Keep `SEND_RATE_MBPS`, `LINK_CAPACITY_MBPS` and `QUEUE_TYPE` similar to S04 so you isolate the effect of RTT.

### 3.5 Duration / Stability

Goal: Check if Prague + ECN remains stable over longer runs.

| ID  | Status | Description                          | CC_MODE | ECN_ENABLED | QUEUE_TYPE        | TEST_DURATION_S | Folder                               |
| --- | ------ | ------------------------------------ | ------- | ----------- | ----------------- | --------------- | ------------------------------------ |
| S08 | [ ]    | Long-duration Prague + ECN scenario | PRAGUE  | on          | AQM/L4S-compatible | longer than S04 | `experiments/s08-prague-ecn-long/`  |

Use similar parameters as S04, but with at least 5–10× the duration.

---

## 4. Workflow Checklist

For each scenario:

1. **Configure network and client** according to the row in the table (set `CC_MODE`, `SEND_RATE_MBPS`, `ECN_ENABLED`, `QUEUE_TYPE`, etc.).
2. **Create the scenario folder** under `experiments/` (e.g., `experiments/s04-prague-ecn-medium/`).
3. **Run the experiment** for the target `TEST_DURATION_S` and collect `esp_client.log` and `server.log` into that folder.
4. **Run `metrics.ipynb`** pointing to those logs (or temporarily copy them to the repo root), generate `prague_performance_enhanced.png` and `prague_advanced_metrics.csv`, and move them into the same folder.
5. **Update the table** above: mark `Status` as `[x]` and, if useful, add a brief note in a local `README.md` inside the scenario folder summarizing key observations.

This file should give you a single place to see which experiments are done, what is pending, and where each scenarios artifacts are stored.