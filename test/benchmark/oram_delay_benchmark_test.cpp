#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "oram/benchmark/ORAMDelayBenchmark.h"

namespace oram::benchmark {
namespace {

TEST(ORAMDelayBenchmarkConfigTest, DefaultConfigMatchesRequestedScenario) {
    const BenchmarkConfig config = DefaultBenchmarkConfig();

    EXPECT_EQ(config.num_blocks, 1ULL << 16);
    EXPECT_EQ(config.levels, 16U);
    EXPECT_EQ(config.accesses, 480U);
    EXPECT_EQ(config.block_sizes, (std::vector<size_t>{4096, 8192, 16384}));
    EXPECT_EQ(config.path_z, 4U);
    EXPECT_EQ(config.ring_z, 33U);
    EXPECT_EQ(config.ring_s, 48U);
    EXPECT_EQ(config.ring_a, 48U);
}

TEST(ORAMDelayBenchmarkPlanTest, PathTrafficUsesOneReadAndOneWritePathPerAccess) {
    BenchmarkConfig config = DefaultBenchmarkConfig();
    config.block_sizes = {4096};

    const TrafficPlan plan = BuildPathTrafficPlan(config, config.block_sizes.front());

    EXPECT_EQ(plan.online_round_trips_per_access, 2U);
    EXPECT_EQ(plan.online_read_bytes_per_access, 16U * 4U * 4096U);
    EXPECT_EQ(plan.online_write_bytes_per_access, 16U * 4U * 4096U);
    EXPECT_EQ(plan.eviction_count, 0U);
}

TEST(ORAMDelayBenchmarkPlanTest, RingTrafficAmortizesTenEvictionsForDefaultRun) {
    const BenchmarkConfig config = DefaultBenchmarkConfig();

    const TrafficPlan plan = BuildRingTrafficPlan(config, 4096);

    EXPECT_EQ(plan.online_round_trips_per_access, 1U);
    EXPECT_EQ(plan.online_read_bytes_per_access, 4096U);
    EXPECT_EQ(plan.online_write_bytes_per_access, 0U);
    EXPECT_EQ(plan.eviction_count, 10U);
    EXPECT_EQ(plan.eviction_read_bytes, 10U * 16U * 33U * 4096U);
    EXPECT_EQ(plan.eviction_write_bytes, 10U * 16U * (33U + 48U) * 4096U);
}

TEST(ORAMDelayBenchmarkRunTest, TinyRunProducesPositivePathAndRingTimings) {
    BenchmarkConfig config;
    config.num_blocks = 16;
    config.levels = 3;
    config.accesses = 2;
    config.block_sizes = {64};
    config.path_z = 2;
    config.ring_z = 3;
    config.ring_s = 2;
    config.ring_a = 2;
    config.host = "127.0.0.1";
    config.start_port = 58700;

    const ComparisonResult result = RunComparison(config, config.block_sizes.front());

    EXPECT_EQ(result.block_size, 64U);
    EXPECT_GT(result.path.total_seconds, 0.0);
    EXPECT_GT(result.path.average_ms_per_access, 0.0);
    EXPECT_GT(result.ring.total_seconds, 0.0);
    EXPECT_GT(result.ring.average_ms_per_access, 0.0);
    EXPECT_GT(result.speedup_path_over_ring, 0.0);
}

}  // namespace
}  // namespace oram::benchmark
