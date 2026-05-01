#include "oram/benchmark/ORAMDelayBenchmark.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr size_t kBytesPerKiB = 1024;
constexpr double kBytesPerMiB = 1024.0 * 1024.0;

void PrintUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --num-blocks N          Logical database size metadata (default: 65536)\n"
        << "  --levels N              Path levels per access (default: 16)\n"
        << "  --accesses N            Accesses per block size (default: 480)\n"
        << "  --block-sizes-kib ...   Block sizes in KiB (default: 4 8 16)\n"
        << "  --path-z N              Path ORAM bucket size (default: 4)\n"
        << "  --ring-z N              Ring ORAM real slots per bucket (default: 33)\n"
        << "  --ring-s N              Ring ORAM dummy slots per bucket (default: 48)\n"
        << "  --ring-a N              Ring ORAM eviction frequency (default: 48)\n"
        << "  --host HOST             Benchmark host (default: 127.0.0.1)\n"
        << "  --start-port PORT       First local TCP port (default: 59100)\n"
        << "  --format table|csv      Output format (default: table)\n"
        << "  --help                  Show this help\n";
}

size_t ParseSize(const std::string& value, const std::string& name) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed == 0) {
        throw std::invalid_argument(name + " must be a positive integer");
    }
    return static_cast<size_t>(parsed);
}

uint64_t ParseUInt64(const std::string& value, const std::string& name) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed == 0) {
        throw std::invalid_argument(name + " must be a positive integer");
    }
    return static_cast<uint64_t>(parsed);
}

int ParseInt(const std::string& value, const std::string& name) {
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed <= 0 || parsed > 65535) {
        throw std::invalid_argument(name + " must be a TCP port");
    }
    return static_cast<int>(parsed);
}

std::string RequireValue(int argc, char** argv, int* index, const std::string& option) {
    if (*index + 1 >= argc) {
        throw std::invalid_argument(option + " requires a value");
    }
    ++(*index);
    return argv[*index];
}

oram::benchmark::BenchmarkConfig ParseArgs(int argc, char** argv, std::string* format) {
    oram::benchmark::BenchmarkConfig config = oram::benchmark::DefaultBenchmarkConfig();
    *format = "table";

    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else if (option == "--num-blocks") {
            config.num_blocks = ParseUInt64(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--levels") {
            config.levels = ParseSize(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--accesses") {
            config.accesses = ParseSize(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--block-sizes-kib") {
            config.block_sizes.clear();
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                ++i;
                config.block_sizes.push_back(ParseSize(argv[i], option) * kBytesPerKiB);
            }
            if (config.block_sizes.empty()) {
                throw std::invalid_argument("--block-sizes-kib requires at least one value");
            }
        } else if (option == "--path-z") {
            config.path_z = ParseSize(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--ring-z") {
            config.ring_z = ParseSize(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--ring-s") {
            config.ring_s = ParseSize(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--ring-a") {
            config.ring_a = ParseSize(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--host") {
            config.host = RequireValue(argc, argv, &i, option);
        } else if (option == "--start-port") {
            config.start_port = ParseInt(RequireValue(argc, argv, &i, option), option);
        } else if (option == "--format") {
            *format = RequireValue(argc, argv, &i, option);
            if (*format != "table" && *format != "csv") {
                throw std::invalid_argument("--format must be table or csv");
            }
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }

    return config;
}

uint64_t TotalReadBytes(const oram::benchmark::AlgorithmResult& result) {
    return result.online_read_bytes + result.eviction_read_bytes;
}

uint64_t TotalWriteBytes(const oram::benchmark::AlgorithmResult& result) {
    return result.online_write_bytes + result.eviction_write_bytes;
}

void PrintCsv(const std::vector<oram::benchmark::ComparisonResult>& results) {
    std::cout << "block_size_kib,algorithm,accesses,evictions,total_seconds,"
                 "average_ms_per_access,online_seconds,eviction_seconds,read_mib,write_mib,"
                 "speedup_path_over_ring\n";
    for (const auto& result : results) {
        for (const auto* algorithm : {&result.path, &result.ring}) {
            std::cout << (result.block_size / kBytesPerKiB) << ',' << algorithm->algorithm << ','
                      << algorithm->accesses << ',' << algorithm->evictions << ','
                      << algorithm->total_seconds << ',' << algorithm->average_ms_per_access << ','
                      << algorithm->online_seconds << ',' << algorithm->eviction_seconds << ','
                      << (static_cast<double>(TotalReadBytes(*algorithm)) / kBytesPerMiB) << ','
                      << (static_cast<double>(TotalWriteBytes(*algorithm)) / kBytesPerMiB) << ','
                      << result.speedup_path_over_ring << '\n';
        }
    }
}

void PrintTable(const std::vector<oram::benchmark::ComparisonResult>& results) {
    std::cout << "Real C++ ORAM delay benchmark\n";
    std::cout << "Traffic limits are external; apply tc before running this command.\n\n";
    std::cout << std::left << std::setw(10) << "Block"
              << std::setw(14) << "Algorithm"
              << std::right << std::setw(12) << "Total(s)"
              << std::setw(14) << "Avg(ms)"
              << std::setw(12) << "Online(s)"
              << std::setw(13) << "Evict(s)"
              << std::setw(12) << "ReadMiB"
              << std::setw(12) << "WriteMiB"
              << std::setw(10) << "Speedup" << '\n';

    for (const auto& result : results) {
        for (const auto* algorithm : {&result.path, &result.ring}) {
            const bool is_ring = algorithm->algorithm == "Ring ORAM";
            std::cout << std::left << std::setw(10)
                      << (std::to_string(result.block_size / kBytesPerKiB) + " KiB")
                      << std::setw(14) << algorithm->algorithm << std::right << std::fixed
                      << std::setprecision(3) << std::setw(12) << algorithm->total_seconds
                      << std::setw(14) << algorithm->average_ms_per_access << std::setw(12)
                      << algorithm->online_seconds << std::setw(13) << algorithm->eviction_seconds
                      << std::setw(12)
                      << (static_cast<double>(TotalReadBytes(*algorithm)) / kBytesPerMiB)
                      << std::setw(12)
                      << (static_cast<double>(TotalWriteBytes(*algorithm)) / kBytesPerMiB)
                      << std::setw(10)
                      << (is_ring ? result.speedup_path_over_ring : 0.0) << '\n';
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string format;
        const auto config = ParseArgs(argc, argv, &format);
        const auto results = oram::benchmark::RunAllComparisons(config);
        if (format == "csv") {
            PrintCsv(results);
        } else {
            PrintTable(results);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
