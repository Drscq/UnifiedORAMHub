#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oram::benchmark {

struct BenchmarkConfig {
    uint64_t num_blocks = 1ULL << 16;
    size_t levels = 16;
    size_t accesses = 480;
    std::vector<size_t> block_sizes{4096, 8192, 16384};
    size_t path_z = 4;
    size_t ring_z = 33;
    size_t ring_s = 48;
    size_t ring_a = 48;
    std::string host = "127.0.0.1";
    int start_port = 59100;
};

struct TrafficPlan {
    size_t online_round_trips_per_access = 0;
    uint64_t online_read_bytes_per_access = 0;
    uint64_t online_write_bytes_per_access = 0;
    size_t eviction_count = 0;
    uint64_t eviction_read_bytes = 0;
    uint64_t eviction_write_bytes = 0;
};

struct AlgorithmResult {
    std::string algorithm;
    size_t accesses = 0;
    size_t evictions = 0;
    uint64_t online_read_bytes = 0;
    uint64_t online_write_bytes = 0;
    uint64_t eviction_read_bytes = 0;
    uint64_t eviction_write_bytes = 0;
    double online_seconds = 0.0;
    double eviction_seconds = 0.0;
    double total_seconds = 0.0;
    double average_ms_per_access = 0.0;
    uint64_t checksum = 0;
};

struct ComparisonResult {
    size_t block_size = 0;
    AlgorithmResult path;
    AlgorithmResult ring;
    double speedup_path_over_ring = 0.0;
};

BenchmarkConfig DefaultBenchmarkConfig();

TrafficPlan BuildPathTrafficPlan(const BenchmarkConfig& config, size_t block_size);
TrafficPlan BuildRingTrafficPlan(const BenchmarkConfig& config, size_t block_size);

ComparisonResult RunComparison(const BenchmarkConfig& config, size_t block_size);
std::vector<ComparisonResult> RunAllComparisons(const BenchmarkConfig& config);

}  // namespace oram::benchmark
