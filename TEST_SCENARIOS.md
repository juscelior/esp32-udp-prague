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

## 2. Global Variables and Where to Configure Them

These are the main knobs you will vary across scenarios. Exact names may differ in code/router UI; adapt as needed.

| Variable              | Description                                                          | Configured Where                                |
| --------------------- | -------------------------------------------------------------------- | ----------------------------------------------- |
| `CC_MODE`            | Congestion control behavior: `PRAGUE` vs `BASELINE` (no ECN logic). | ESP32 client code (`AsyncUDPClient.ino`).      |
| `SEND_RATE_MBPS`     | Target sending rate of the ESP32 client.                            | ESP32 client code / configuration.             |
| `TEST_DURATION_S`    | Duration of each experiment.                                        | Test script / manual timer.                    |
| `RTT_BASE_MS`        | Approximate base RTT of the path (without queueing).                | Network topology (router, emulator, etc.).     |
| `QUEUE_TYPE`         | Queueing discipline: `DropTail`, `AQM-RED`, `AQM-PIE`, `L4S-dual`.  | Router / bottleneck configuration.             |
| `ECN_ENABLED`        | Whether ECN marking is enabled on the bottleneck queue.             | Router / OS (e.g., `sysctl`, queue settings).  |
| `LINK_CAPACITY_MBPS` | Bottleneck link capacity.                                           | Router / traffic shaper.                       |
| `BUFFER_TARGET_MS`   | Target queueing delay / buffer size (when configurable).            | Router / AQM profile.                          |

When you run a scenario, make a short note in a `README.md` inside that scenario folder documenting the actual values used (e.g., `SEND_RATE_MBPS=5`, `LINK_CAPACITY_MBPS=2`, etc.).

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