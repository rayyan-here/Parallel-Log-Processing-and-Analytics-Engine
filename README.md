# Parallel Log Processing & Analytics Engine

**Operating System (CS-2006)** · Haris Ali (24K-2019), Muhib Amir (24K-2021), and Rayyan Azhar (24K-2025)

The C++20 engine reads the same local log with one worker and then with two to four workers. The Qt dashboard shows live counts, the lines assigned to each worker, and both processing times. CSV export becomes available when every count and every identity frequency matches between the two runs.

## Run on Ubuntu

Extract the whole ZIP and open a terminal inside `Parallel-Log-Engine`. On an Ubuntu 24.04 desktop, install the build tools and Qt once, then launch the dashboard.

```sh
sudo apt update
sudo apt install build-essential pkg-config qt6-base-dev
bash run.sh
```

Choose `sample.txt`, leave **4 workers** and **2 reader permits**, and select **Run comparison**. After **Verified** appears, select **Export CSV**. Choose `live-demo.txt` to watch the counts change, or **Browse** to analyze your own text file.

| Command | Result |
| --- | --- |
| `bash run.sh --demo` | Run the sample in a terminal and save `build/demo-report.csv`. |
| `make` | Build the CLI. Custom example below. |
| `make test` | Run the engine, CLI, export and benchmark tests. |
| `make test-ui` | Exercise the actual Qt window, file dialogs, analyzer process and export offscreen. |
| `bash run.sh --benchmark` | Run six timing comparisons on the 500,000-line log. |
| `make benchmark INPUT=data/sample.txt WORKERS=3 READERS=1` | Benchmark another file or configuration. |
| `make demo-log LINES=2000000` | Generate a longer, repeatable log at `build/large-demo.txt`. |

```sh
make
./build/log_analyzer data/repeated-logins.txt --threads 3 --readers 1 --report build/my-report.csv
```

The launcher builds the program from source. No precompiled executables are required.

## What to expect

| Input | Lines | INFO | WARNING | ERROR | OTHER | Failed-login lines | Flagged identities | Unattributed failures | Total alerts |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `sample.txt` | 24 | 10 | 5 | 7 | 2 | 9 | 2 | 1 | 2 |
| `repeated-logins.txt` | 9 | 3 | 2 | 4 | 0 | 6 | 1 | 3 | 2 |
| `live-demo.txt` | 500,000 | 250,000 | 100,000 | 100,000 | 50,000 | 100,000 | 2 | 0 | 2 |

Each physical line gets one severity. The first standalone INFO, WARNING or ERROR token wins, ignoring case. OTHER holds blank lines and unrecognized severities. FAILED_LOGIN counts once per matching line and can overlap any severity. The last line is included even when it has no terminating newline.

The proposal leaves “repeated” open. This project uses **three matching failed-login lines anywhere in one file**. A line is attributed to its first nonempty `user=` field or, if absent, its first nonempty `ip=` field. Values are case-sensitive. Bare values and single- or double-quoted values are supported. Inside quotes, a backslash can escape the matching quote or another backslash.

- Three failures for one identity flag it once. Further failures increase that identity’s frequency. In the 24-line sample, Ali's failures on lines 2, 10 and 19 reach workers 1, 2 and 4. The other flagged identity is `ip=198.51.100.7`.
- Three failures without a usable user or IP raise one additional **file-level alert**. The source of these events is unknown. The identity table lists only attributed alerts. In `repeated-logins.txt`, `lab user` accounts for three failures and the other three produce the file-level alert.
- In the CSV, `suspicious_patterns` counts flagged identities for compatibility. `repeated_unattributed` records the file-level alert and `detected_patterns` gives the combined alert count. The dashboard previews up to eight flagged identities. The CSV includes every qualifying identity.

## How the OS concepts appear in the code

| Code | What it does |
| --- | --- |
| `src/engine.cpp` · `plan_file` | Scans in 64 KiB blocks and records only the cuts. Each interval includes its starting byte and excludes its ending byte. A physical line belongs to exactly one worker per pass. |
| `src/engine.cpp` · `process` and `ReadPermit` | Each `std::jthread` opens its own stream. A counting semaphore limits concurrent open, seek and batch-read sections. The permit is released before parsing up to 256 lines. |
| `src/engine.cpp` · `merge_batch` and `run_pass` | A mutex protects the counters, per-identity frequencies and consistent progress snapshots. The coordinator joins all workers. A user can reach the alert threshold across different workers. |
| `src/parser.cpp` · `classify` | Recognizes severity and FAILED_LOGIN tokens and extracts a user or IP. |
| `src/engine.cpp` · `compare` | Compares both passes, including each identity frequency. File size, modification time and segment byte coverage are checked for changes during processing. Use a stable local text file. |
| `ui/dashboard.cpp` and `src/report.cpp` | Qt receives updates from the CLI through `QProcess`. A verified export is written to a temporary sibling and renamed into place. Changing a selection clears the previous result. |
| `tools/benchmark.cpp` | Reuses the shared index for six verified pairs. Three pairs start with each order. The summary divides the median one-worker time by the median parallel time. |

Indexing is measured once. The worker timers cover startup, reading, parsing and merging. The export and dashboard drawing are outside those timers. A speedup above 1 means the parallel pass was faster in that measurement. Small logs can finish faster with one worker. Hardware, system load and the file cache affect the result.

## Project structure

- `src/`: C++ log processing, parsing and CSV export.
- `ui/`: Qt dashboard.
- `data/`: sample inputs, including the large demonstration log.
- `tools/`: log generator and performance benchmark.
- `tests/`: automated correctness and dashboard tests.
- `Makefile`, `run.sh`: build and launch commands.

Build outputs are generated locally and excluded from version control.
