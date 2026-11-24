CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc
BASE = -std=c++20 -pthread
CORE = src/engine.cpp src/parser.cpp src/report.cpp
HEADERS = src/engine.hpp src/parser.hpp
PKG_CONFIG ?= pkg-config
QT_FLAGS = $(shell $(PKG_CONFIG) --cflags Qt6Widgets)
QT_LIBS = $(shell $(PKG_CONFIG) --libs Qt6Widgets)
UI_FILES = ui/dashboard.cpp ui/dashboard.hpp
LINES ?= 2000000
INPUT ?= data/live-demo.txt
WORKERS ?= 4
READERS ?= 2

.PHONY: all demo gui desktop demo-log benchmark test test-ui check-qt clean
all: build/log_analyzer

build:
	mkdir -p $@

build/log_analyzer: src/main.cpp $(CORE) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BASE) src/main.cpp $(CORE) -o $@

build/generate_log: tools/generate_log.cpp | build
	$(CXX) $(CXXFLAGS) $(BASE) $< -o $@

build/test_engine: tests/test_engine.cpp $(CORE) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BASE) tests/test_engine.cpp $(CORE) -o $@

build/benchmark: tools/benchmark.cpp $(CORE) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BASE) tools/benchmark.cpp $(CORE) -o $@

check-qt:
	@$(PKG_CONFIG) --exists Qt6Widgets 2>/dev/null || { echo 'Ubuntu GUI setup: sudo apt install build-essential pkg-config qt6-base-dev' >&2; exit 1; }

build/log_dashboard: ui/main.cpp $(UI_FILES) src/report.cpp $(HEADERS) | build check-qt
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BASE) -fPIC $(QT_FLAGS) ui/main.cpp ui/dashboard.cpp src/report.cpp -o $@ $(QT_LIBS)

build/test_qt: tests/test_qt.cpp $(UI_FILES) src/report.cpp $(HEADERS) | build check-qt
	$(CXX) $(CPPFLAGS) -Iui $(CXXFLAGS) $(BASE) -fPIC $(QT_FLAGS) tests/test_qt.cpp ui/dashboard.cpp src/report.cpp -o $@ $(QT_LIBS)

desktop: all build/log_dashboard

gui: desktop
	./build/log_dashboard "$(CURDIR)"

demo: all
	./build/log_analyzer data/sample.txt --threads 4 --readers 2 --report build/demo-report.csv

benchmark: build/benchmark
	./build/benchmark "$(INPUT)" "$(WORKERS)" "$(READERS)"

demo-log: build/generate_log
	./build/generate_log build/large-demo.txt $(LINES)

test: all build/test_engine build/generate_log build/benchmark
	./build/test_engine
	sh tests/test_cli.sh ./build/log_analyzer ./build/generate_log
	sh tests/test_benchmark.sh ./build/benchmark

test-ui: desktop build/test_qt
	QT_QPA_PLATFORM=offscreen ./build/test_qt

clean:
	rm -rf build
