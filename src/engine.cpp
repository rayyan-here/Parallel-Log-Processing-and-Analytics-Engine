#include "engine.hpp"
#include "parser.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <thread>

namespace logs {
using Clock = std::chrono::steady_clock;
namespace {
void ensure_unchanged(const Plan& plan) {
    if (fs::file_size(plan.path) != plan.bytes || fs::last_write_time(plan.path) != plan.modified)
        throw std::runtime_error("Input changed during analysis. Run again on a stable file.");
}
struct Boundary { Count byte, line; };

template<class OnLineEnd>
Count scan_line_ends(const Plan& plan, OnLineEnd on_end) {
    std::ifstream in(plan.path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open the input file.");
    std::array<char, 64 * 1024> buffer;
    Count offset = 0, lines = 0, last_end = 0;
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
        auto n = static_cast<std::size_t>(in.gcount());
        const char* p = buffer.data();
        const char* end = p + n;
        while (const void* found = std::memchr(p, '\n', static_cast<std::size_t>(end - p))) {
            p = static_cast<const char*>(found) + 1;
            last_end = offset + static_cast<Count>(p - buffer.data());
            on_end(Boundary{last_end, ++lines});
        }
        offset += n;
    }
    if (!in.eof() || in.bad() || offset != plan.bytes) throw std::runtime_error("Input read failed while indexing.");
    if (last_end != offset) on_end(Boundary{offset, ++lines});
    return lines;
}
} // namespace

Plan plan_file(const fs::path& path, unsigned workers) {
    const auto start = Clock::now();
    if (workers < 2 || workers > 4) throw std::invalid_argument("Use 2, 3, or 4 workers.");
    if (!fs::is_regular_file(path)) throw std::runtime_error("Input must be a regular text file.");
    Plan plan{path, fs::file_size(path), 0, fs::last_write_time(path), {}};
    std::vector<Boundary> cuts{{0, 0}};
    auto byte_target = [&](Count i) {
        return plan.bytes / workers * i + ((plan.bytes % workers) * i + workers - 1) / workers;
    };
    plan.lines = scan_line_ends(plan, [&](Boundary end) {
        while (cuts.size() < workers && end.byte >= byte_target(cuts.size()))
            cuts.push_back(end);
    });
    cuts.resize(workers + 1, {plan.bytes, plan.lines});

    unsigned active = 0;
    for (unsigned i = 0; i < workers; ++i)
        active += cuts[i].line != cuts[i + 1].line;
    if (active < std::min<Count>(workers, plan.lines)) {
        // A very long line can span several byte targets. Redistribute by whole
        // lines only when this can give useful work to an otherwise idle worker.
        const Count per_worker = plan.lines / workers, extra = plan.lines % workers;
        cuts.resize(1);
        auto seen = scan_line_ends(plan, [&](Boundary end) {
            const Count i = cuts.size();
            if (i < workers && end.line == per_worker * i + std::min(i, extra))
                cuts.push_back(end);
        });
        if (seen != plan.lines) throw std::runtime_error("Line count changed during indexing.");
        cuts.resize(workers + 1, {plan.bytes, plan.lines});
    }
    for (unsigned i = 0; i < workers; ++i)
        plan.segments.push_back({cuts[i].byte, cuts[i + 1].byte, cuts[i].line, cuts[i + 1].line});
    ensure_unchanged(plan);
    plan.indexing_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return plan;
}

namespace {
constexpr std::size_t batch_size = 256;
struct Shared {
    explicit Shared(int readers) : slots(readers) {}
    std::counting_semaphore<4> slots;
    std::atomic<int> reading{0}, peak{0};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable done;
    Totals totals;
    std::exception_ptr error;
    std::size_t finished = 0;
    Clock::time_point end;
};

// Scope exit also returns a permit if the file operation throws.
struct ReadPermit {
    Shared& state;
    explicit ReadPermit(Shared& s) : state(s) {
        state.slots.acquire();
        int active = ++state.reading;
        int old = state.peak.load();
        while (old < active && !state.peak.compare_exchange_weak(old, active)) {}
    }
    ~ReadPermit() { --state.reading; state.slots.release(); }
};

void merge_batch(Shared& shared, const Totals& batch) {
    std::lock_guard lock(shared.mutex);
    combine_counts(shared.totals.counts, batch.counts);
    for (const auto& [id, n] : batch.identities) {
        auto& previous = shared.totals.identities[id];
        if (previous < repeat_threshold && previous + n >= repeat_threshold)
            ++shared.totals.counts.patterns;
        previous += n;
    }
}

void process(const fs::path& path, Segment segment, Shared& shared, Counts& worker) {
    if (segment.size() == 0) return;
    std::ifstream input;
    {
        ReadPermit permit(shared);
        input.open(path, std::ios::binary);
        if (!input || !input.seekg(static_cast<std::streamoff>(segment.begin)))
            throw std::runtime_error("Cannot seek to a log segment.");
    }
    std::array<std::string, batch_size> lines;
    Count bytes = 0;
    while (worker.lines < segment.size() && !shared.stop.load()) {
        const auto n = static_cast<std::size_t>(std::min<Count>(batch_size, segment.size() - worker.lines));
        {
            ReadPermit permit(shared);
            for (std::size_t i = 0; i < n; ++i) {
                if (!std::getline(input, lines[i])) throw std::runtime_error("Unexpected end or read error in log segment.");
                bytes += lines[i].size() + (input.eof() ? 0 : 1);
            }
        }
        Totals batch;
        for (std::size_t i = 0; i < n; ++i) classify(lines[i], batch);
        combine_counts(worker, batch.counts);
        merge_batch(shared, batch);
    }
    if (!shared.stop.load() && bytes != segment.end - segment.begin)
        throw std::runtime_error("Log segment size changed during reading.");
}
} // namespace

Run run_pass(const Plan& plan, const std::vector<Segment>& segments, int readers, const Progress& progress) {
    if (segments.empty() || readers < 1 || readers > static_cast<int>(segments.size()) || readers > 4)
        throw std::invalid_argument("Invalid worker or reader count.");
    ensure_unchanged(plan);
    Shared shared(readers);
    Run run;
    run.workers.resize(segments.size());
    std::vector<std::jthread> workers;
    workers.reserve(segments.size());
    if (progress) progress({});
    auto start = Clock::now();
    for (std::size_t id = 0; id < segments.size(); ++id) {
        workers.emplace_back([&, id] {
            try {
                process(plan.path, segments[id], shared, run.workers[id]);
            } catch (...) {
                std::lock_guard lock(shared.mutex);
                if (!shared.error) shared.error = std::current_exception();
                shared.stop = true;
            }
            {
                std::lock_guard lock(shared.mutex);
                if (++shared.finished == segments.size()) shared.end = Clock::now();
            }
            shared.done.notify_one();
        });
    }
    for (;;) {
        std::unique_lock lock(shared.mutex);
        const bool done = shared.done.wait_for(lock, std::chrono::milliseconds(16),
                                              [&] { return shared.finished == segments.size(); });
        auto snapshot = shared.totals.counts;
        lock.unlock();
        if (progress) progress(snapshot);
        if (done) break;
    }
    for (auto& thread : workers) thread.join();
    if (shared.error) std::rethrow_exception(shared.error);
    ensure_unchanged(plan);
    if (shared.totals.counts.lines != plan.lines) throw std::runtime_error("Line coverage mismatch.");
    run.milliseconds = std::chrono::duration<double, std::milli>(shared.end - start).count();
    run.peak_readers = shared.peak.load();
    run.totals = std::move(shared.totals);
    return run;
}

Comparison compare(const Plan& plan, int readers, const Progress& single_progress,
                   const Progress& multi_progress, PassOrder order) {
    Comparison c{plan, readers, {}, {}};
    if (readers < 1 || readers > static_cast<int>(plan.segments.size()))
        throw std::invalid_argument("Reader limit must be between 1 and the worker count.");
    auto baseline = [&] {
        c.single = run_pass(plan, {{0, plan.bytes, 0, plan.lines}}, 1, single_progress);
    };
    auto parallel = [&] {
        c.multi = run_pass(plan, plan.segments, readers, multi_progress);
    };
    if (order == PassOrder::single_first) {
        baseline();
        parallel();
    } else {
        parallel();
        baseline();
    }
    if (c.single.totals != c.multi.totals)
        throw std::runtime_error("Single and parallel results disagree. No report was produced.");
    return c;
}
} // namespace logs
