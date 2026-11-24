#!/bin/sh
set -eu
cd "$(dirname "$0")"

case "${1:-}" in
    --demo)
        mkdir -p build
        if command -v make >/dev/null && command -v g++ >/dev/null; then
            make -s build/log_analyzer
            engine=./build/log_analyzer
        else
            echo "Ubuntu terminal setup: sudo apt install build-essential" >&2
            exit 1
        fi
        exec "$engine" data/sample.txt --threads 4 --readers 2 --report build/demo-report.csv
        ;;
    --benchmark) exec make -s benchmark ;;
    --help|-h) echo "Usage: bash run.sh [--demo | --benchmark]"; exit 0 ;;
    '') ;;
    *) echo "Usage: bash run.sh [--demo | --benchmark]" >&2; exit 2 ;;
esac
if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}${QT_QPA_PLATFORM:-}" ]; then
    echo "Open a terminal on your Ubuntu desktop for the Qt window, or run: bash run.sh --demo" >&2
    exit 1
fi
if command -v make >/dev/null && command -v g++ >/dev/null && command -v pkg-config >/dev/null && pkg-config --exists Qt6Widgets; then
    make -s desktop
    exec ./build/log_dashboard "$PWD"
fi
echo "Ubuntu GUI setup: sudo apt install build-essential pkg-config qt6-base-dev" >&2
echo "Then launch with: bash run.sh    |    Terminal demo: bash run.sh --demo" >&2
exit 1
