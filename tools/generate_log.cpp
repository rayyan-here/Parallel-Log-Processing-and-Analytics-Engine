#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: generate_log OUTPUT LINE_COUNT\n";
        return 1;
    }
    std::string_view arg = argv[2];
    std::uint64_t n = 0;
    auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if (ec != std::errc{} || end != arg.data() + arg.size()) {
        std::cerr << "LINE_COUNT must be a nonnegative integer.\n";
        return 1;
    }
    std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
    if (!out) { std::cerr << "Cannot open output file.\n"; return 1; }
    for (std::uint64_t i = 0; i < n; ++i) {
        out << "event=" << i + 1 << ' ';
        switch (i % 10) {
            case 0: out << "[ERROR] FAILED_LOGIN user=admin ip=192.0.2.10"; break;
            case 1: out << "[WARNING] response time above threshold"; break;
            case 2: out << "[ERROR] storage timeout"; break;
            case 3: out << "[WARNING] FAILED_LOGIN ip=198.51.100.7"; break;
            case 4: out << "[DEBUG] scheduler heartbeat"; break;
            default: out << "[INFO] request completed"; break;
        }
        out << '\n';
    }
    out.close();
    if (!out) { std::cerr << "Write failed.\n"; return 1; }
    std::cout << "Generated " << n << " lines in " << argv[1] << '\n';
}
