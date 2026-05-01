#include "oram/benchmark/ORAMDelayBenchmark.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>

#include "oram/crypto/AES_CTR.h"
#include "oram/network/NetIO.h"

namespace oram::benchmark {
namespace {

constexpr char kCommandReadPath = 'P';
constexpr char kCommandWritePath = 'W';
constexpr char kCommandRingOnlineRead = 'R';
constexpr char kCommandRingEvict = 'E';
constexpr char kCommandQuit = 'Q';
constexpr char kAck = 'K';

using Clock = std::chrono::steady_clock;

std::vector<uint8_t> FixedKey() {
    return std::vector<uint8_t>(crypto::AES_CTR::kKeySize128, 0x9B);
}

std::vector<uint8_t> FixedIV(uint64_t domain) {
    std::vector<uint8_t> iv(crypto::AES_CTR::kIVSize, 0);
    for (size_t i = 0; i < sizeof(domain); ++i) {
        iv[iv.size() - 1 - i] = static_cast<uint8_t>((domain >> (8 * i)) & 0xFFU);
    }
    return iv;
}

void RequirePositive(const char* name, size_t value) {
    if (value == 0) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
}

uint64_t CheckedMul(uint64_t lhs, uint64_t rhs) {
    if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs) {
        throw std::overflow_error("benchmark byte count overflow");
    }
    return lhs * rhs;
}

uint64_t PathBytesPerTransfer(const BenchmarkConfig& config, size_t block_size) {
    return CheckedMul(CheckedMul(config.levels, config.path_z), block_size);
}

uint64_t RingOnlineBytesPerAccess(size_t block_size) { return block_size; }

uint64_t RingEvictionReadBytes(const BenchmarkConfig& config, size_t block_size,
                               size_t evictions) {
    return CheckedMul(CheckedMul(CheckedMul(evictions, config.levels), config.ring_z),
                      block_size);
}

uint64_t RingEvictionWriteBytes(const BenchmarkConfig& config, size_t block_size,
                                size_t evictions) {
    return CheckedMul(
        CheckedMul(CheckedMul(evictions, config.levels), config.ring_z + config.ring_s),
        block_size);
}

size_t EvictionCount(size_t accesses, size_t ring_a) { return accesses / ring_a; }

void SendUInt64(network::NetIO* net_io, uint64_t value) {
    net_io->SendData(&value, sizeof(value));
}

uint64_t RecvUInt64(network::NetIO* net_io) {
    uint64_t value = 0;
    net_io->RecvData(&value, sizeof(value));
    return value;
}

std::vector<uint8_t> MakePlainBuffer(size_t size, uint64_t seed) {
    std::vector<uint8_t> buffer(size, 0);
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<uint8_t>((seed + i * 17U + (i >> 8U)) & 0xFFU);
    }
    return buffer;
}

uint64_t AccumulateBuffer(const std::vector<uint8_t>& buffer) {
    uint64_t value = 0;
    for (uint8_t byte : buffer) {
        value = (value * 131U) ^ byte;
    }
    return value;
}

void XorInto(std::vector<uint8_t>* target, const uint8_t* source, size_t size) {
    if (target == nullptr || target->size() != size) {
        throw std::invalid_argument("XOR target size mismatch");
    }
    for (size_t i = 0; i < size; ++i) {
        (*target)[i] ^= source[i];
    }
}

class FixedCipher {
   public:
    FixedCipher() : cipher_(FixedKey()) {}

    std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plaintext, uint64_t domain) {
        return cipher_.Encrypt(plaintext, FixedIV(domain));
    }

    std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& ciphertext, uint64_t domain) {
        return cipher_.Decrypt(ciphertext, FixedIV(domain));
    }

   private:
    crypto::AES_CTR cipher_;
};

class PathBenchmarkServer {
   public:
    PathBenchmarkServer(const BenchmarkConfig& config, size_t block_size)
        : config_(config),
          block_size_(block_size),
          bucket_bytes_(CheckedMul(config.path_z, block_size)),
          path_(config.levels) {
        for (size_t level = 0; level < path_.size(); ++level) {
            path_[level] = MakePlainBuffer(bucket_bytes_, 0x1000U + level);
        }
    }

    void Run(const std::string& host, int port) {
        network::NetIO net_io(host, port, true, true);
        while (true) {
            char command = 0;
            net_io.RecvData(&command, 1);
            if (command == kCommandQuit) {
                break;
            }
            if (command == kCommandReadPath) {
                HandleReadPath(&net_io);
            } else if (command == kCommandWritePath) {
                HandleWritePath(&net_io);
            } else {
                throw std::runtime_error("unknown Path benchmark command");
            }
        }
    }

   private:
    void HandleReadPath(network::NetIO* net_io) {
        SendUInt64(net_io, path_.size());
        for (const auto& bucket : path_) {
            auto encrypted = cipher_.Encrypt(bucket, 0xA000U);
            net_io->SendData(encrypted.data(), encrypted.size());
        }
        net_io->Flush();
    }

    void HandleWritePath(network::NetIO* net_io) {
        const uint64_t count = RecvUInt64(net_io);
        if (count != path_.size()) {
            throw std::runtime_error("Path benchmark write path length mismatch");
        }
        for (auto& bucket : path_) {
            std::vector<uint8_t> encrypted(bucket_bytes_, 0);
            net_io->RecvData(encrypted.data(), encrypted.size());
            bucket = cipher_.Decrypt(encrypted, 0xB000U);
        }
        char ack = kAck;
        net_io->SendData(&ack, 1);
        net_io->Flush();
    }

    BenchmarkConfig config_;
    size_t block_size_;
    size_t bucket_bytes_;
    std::vector<std::vector<uint8_t>> path_;
    FixedCipher cipher_;
};

class PathBenchmarkClient {
   public:
    PathBenchmarkClient(const BenchmarkConfig& config, size_t block_size, int port)
        : config_(config),
          block_size_(block_size),
          bucket_bytes_(CheckedMul(config.path_z, block_size)),
          net_io_(std::make_unique<network::NetIO>(config.host, port, false, true)) {}

    ~PathBenchmarkClient() {
        try {
            char quit = kCommandQuit;
            net_io_->SendData(&quit, 1);
            net_io_->Flush();
        } catch (...) {
        }
    }

    uint64_t Access(size_t access_index) {
        char read_command = kCommandReadPath;
        net_io_->SendData(&read_command, 1);
        net_io_->Flush();

        const uint64_t count = RecvUInt64(net_io_.get());
        if (count != config_.levels) {
            throw std::runtime_error("Path benchmark read path length mismatch");
        }

        std::vector<std::vector<uint8_t>> buckets;
        buckets.reserve(static_cast<size_t>(count));
        uint64_t checksum = access_index;
        for (size_t level = 0; level < count; ++level) {
            std::vector<uint8_t> encrypted(bucket_bytes_, 0);
            net_io_->RecvData(encrypted.data(), encrypted.size());
            auto bucket = cipher_.Decrypt(encrypted, 0xA000U);
            checksum ^= AccumulateBuffer(bucket);
            if (!bucket.empty()) {
                bucket[(access_index + level) % bucket.size()] ^=
                    static_cast<uint8_t>(access_index + level);
            }
            buckets.push_back(std::move(bucket));
        }

        char write_command = kCommandWritePath;
        net_io_->SendData(&write_command, 1);
        SendUInt64(net_io_.get(), buckets.size());
        for (const auto& bucket : buckets) {
            auto encrypted = cipher_.Encrypt(bucket, 0xB000U);
            net_io_->SendData(encrypted.data(), encrypted.size());
        }
        net_io_->Flush();

        char ack = 0;
        net_io_->RecvData(&ack, 1);
        if (ack != kAck) {
            throw std::runtime_error("Path benchmark missing write acknowledgement");
        }

        return checksum;
    }

   private:
    BenchmarkConfig config_;
    size_t block_size_;
    size_t bucket_bytes_;
    std::unique_ptr<network::NetIO> net_io_;
    FixedCipher cipher_;
};

class RingBenchmarkServer {
   public:
    RingBenchmarkServer(const BenchmarkConfig& config, size_t block_size)
        : config_(config),
          block_size_(block_size),
          slot_count_(config.ring_z + config.ring_s),
          bucket_bytes_(CheckedMul(slot_count_, block_size)),
          path_(config.levels) {
        for (size_t level = 0; level < path_.size(); ++level) {
            const auto plain = MakePlainBuffer(bucket_bytes_, 0x5000U + level);
            path_[level] = cipher_.Encrypt(plain, 0xC000U);
        }
    }

    void Run(const std::string& host, int port) {
        network::NetIO net_io(host, port, true, true);
        while (true) {
            char command = 0;
            net_io.RecvData(&command, 1);
            if (command == kCommandQuit) {
                break;
            }
            if (command == kCommandRingOnlineRead) {
                HandleOnlineRead(&net_io);
            } else if (command == kCommandRingEvict) {
                HandleEvict(&net_io);
            } else {
                throw std::runtime_error("unknown Ring benchmark command");
            }
        }
    }

   private:
    void HandleOnlineRead(network::NetIO* net_io) {
        const uint64_t access_index = RecvUInt64(net_io);
        std::vector<uint8_t> aggregate(block_size_, 0);
        for (size_t level = 0; level < path_.size(); ++level) {
            const size_t slot = (static_cast<size_t>(access_index) + level) % slot_count_;
            const size_t offset = CheckedMul(slot, block_size_);
            XorInto(&aggregate, path_[level].data() + offset, block_size_);
        }
        net_io->SendData(aggregate.data(), aggregate.size());
        net_io->Flush();
    }

    void HandleEvict(network::NetIO* net_io) {
        for (const auto& bucket : path_) {
            const size_t read_bytes = CheckedMul(config_.ring_z, block_size_);
            net_io->SendData(bucket.data(), read_bytes);
        }
        net_io->Flush();

        for (auto& bucket : path_) {
            std::vector<uint8_t> replacement(bucket_bytes_, 0);
            net_io->RecvData(replacement.data(), replacement.size());
            bucket = std::move(replacement);
        }

        char ack = kAck;
        net_io->SendData(&ack, 1);
        net_io->Flush();
    }

    BenchmarkConfig config_;
    size_t block_size_;
    size_t slot_count_;
    size_t bucket_bytes_;
    std::vector<std::vector<uint8_t>> path_;
    FixedCipher cipher_;
};

class RingBenchmarkClient {
   public:
    RingBenchmarkClient(const BenchmarkConfig& config, size_t block_size, int port)
        : config_(config),
          block_size_(block_size),
          slot_count_(config.ring_z + config.ring_s),
          bucket_bytes_(CheckedMul(slot_count_, block_size)),
          net_io_(std::make_unique<network::NetIO>(config.host, port, false, true)) {}

    ~RingBenchmarkClient() {
        try {
            char quit = kCommandQuit;
            net_io_->SendData(&quit, 1);
            net_io_->Flush();
        } catch (...) {
        }
    }

    uint64_t OnlineRead(size_t access_index) {
        char command = kCommandRingOnlineRead;
        net_io_->SendData(&command, 1);
        SendUInt64(net_io_.get(), access_index);
        net_io_->Flush();

        std::vector<uint8_t> aggregate(block_size_, 0);
        net_io_->RecvData(aggregate.data(), aggregate.size());
        auto decrypted = cipher_.Decrypt(aggregate, 0xC000U);
        return AccumulateBuffer(decrypted);
    }

    uint64_t Evict(size_t eviction_index) {
        char command = kCommandRingEvict;
        net_io_->SendData(&command, 1);
        net_io_->Flush();

        uint64_t checksum = eviction_index;
        std::vector<std::vector<uint8_t>> read_blocks;
        read_blocks.reserve(config_.levels * config_.ring_z);
        for (size_t level = 0; level < config_.levels; ++level) {
            for (size_t i = 0; i < config_.ring_z; ++i) {
                std::vector<uint8_t> encrypted(block_size_, 0);
                net_io_->RecvData(encrypted.data(), encrypted.size());
                auto plain = cipher_.Decrypt(encrypted, 0xC000U);
                checksum ^= AccumulateBuffer(plain);
                read_blocks.push_back(std::move(plain));
            }
        }

        for (size_t level = 0; level < config_.levels; ++level) {
            std::vector<uint8_t> bucket(bucket_bytes_, 0);
            for (size_t slot = 0; slot < slot_count_; ++slot) {
                const size_t offset = CheckedMul(slot, block_size_);
                const size_t source = (level * config_.ring_z + (slot % config_.ring_z)) %
                                      read_blocks.size();
                std::copy(read_blocks[source].begin(), read_blocks[source].end(),
                          bucket.begin() + static_cast<std::ptrdiff_t>(offset));
                bucket[offset] ^= static_cast<uint8_t>(eviction_index + level + slot);
            }
            auto encrypted = cipher_.Encrypt(bucket, 0xC000U);
            net_io_->SendData(encrypted.data(), encrypted.size());
        }
        net_io_->Flush();

        char ack = 0;
        net_io_->RecvData(&ack, 1);
        if (ack != kAck) {
            throw std::runtime_error("Ring benchmark missing eviction acknowledgement");
        }

        return checksum;
    }

   private:
    BenchmarkConfig config_;
    size_t block_size_;
    size_t slot_count_;
    size_t bucket_bytes_;
    std::unique_ptr<network::NetIO> net_io_;
    FixedCipher cipher_;
};

template <typename ServerFactory, typename ClientRunner>
AlgorithmResult RunWithServer(const BenchmarkConfig& config, int port, ServerFactory server_factory,
                              ClientRunner client_runner) {
    std::exception_ptr server_exception;
    std::thread server_thread([&]() {
        try {
            auto server = server_factory();
            server.Run(config.host, port);
        } catch (...) {
            server_exception = std::current_exception();
        }
    });

    AlgorithmResult result;
    try {
        result = client_runner(port);
    } catch (...) {
        try {
            network::NetIO stopper(config.host, port, false, true);
            char quit = kCommandQuit;
            stopper.SendData(&quit, 1);
            stopper.Flush();
        } catch (...) {
        }
        if (server_thread.joinable()) {
            server_thread.join();
        }
        throw;
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }
    if (server_exception != nullptr) {
        std::rethrow_exception(server_exception);
    }
    return result;
}

AlgorithmResult RunPath(const BenchmarkConfig& config, size_t block_size, int port) {
    return RunWithServer(
        config, port,
        [&]() { return PathBenchmarkServer(config, block_size); },
        [&](int client_port) {
            PathBenchmarkClient client(config, block_size, client_port);
            const auto plan = BuildPathTrafficPlan(config, block_size);
            uint64_t checksum = 0;

            const auto start = Clock::now();
            for (size_t i = 0; i < config.accesses; ++i) {
                checksum ^= client.Access(i);
            }
            const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();

            AlgorithmResult result;
            result.algorithm = "Path ORAM";
            result.accesses = config.accesses;
            result.evictions = 0;
            result.online_read_bytes =
                CheckedMul(config.accesses, plan.online_read_bytes_per_access);
            result.online_write_bytes =
                CheckedMul(config.accesses, plan.online_write_bytes_per_access);
            result.online_seconds = elapsed;
            result.total_seconds = elapsed;
            result.average_ms_per_access = elapsed * 1000.0 / static_cast<double>(config.accesses);
            result.checksum = checksum;
            return result;
        });
}

AlgorithmResult RunRing(const BenchmarkConfig& config, size_t block_size, int port) {
    return RunWithServer(
        config, port,
        [&]() { return RingBenchmarkServer(config, block_size); },
        [&](int client_port) {
            RingBenchmarkClient client(config, block_size, client_port);
            const auto plan = BuildRingTrafficPlan(config, block_size);
            uint64_t checksum = 0;
            double online_seconds = 0.0;
            double eviction_seconds = 0.0;
            size_t evictions = 0;

            for (size_t i = 1; i <= config.accesses; ++i) {
                const auto online_start = Clock::now();
                checksum ^= client.OnlineRead(i);
                online_seconds += std::chrono::duration<double>(Clock::now() - online_start).count();

                if (i % config.ring_a == 0) {
                    const auto eviction_start = Clock::now();
                    checksum ^= client.Evict(evictions);
                    eviction_seconds +=
                        std::chrono::duration<double>(Clock::now() - eviction_start).count();
                    ++evictions;
                }
            }

            AlgorithmResult result;
            result.algorithm = "Ring ORAM";
            result.accesses = config.accesses;
            result.evictions = evictions;
            result.online_read_bytes =
                CheckedMul(config.accesses, plan.online_read_bytes_per_access);
            result.online_write_bytes =
                CheckedMul(config.accesses, plan.online_write_bytes_per_access);
            result.eviction_read_bytes = plan.eviction_read_bytes;
            result.eviction_write_bytes = plan.eviction_write_bytes;
            result.online_seconds = online_seconds;
            result.eviction_seconds = eviction_seconds;
            result.total_seconds = online_seconds + eviction_seconds;
            result.average_ms_per_access =
                result.total_seconds * 1000.0 / static_cast<double>(config.accesses);
            result.checksum = checksum;
            return result;
        });
}

}  // namespace

BenchmarkConfig DefaultBenchmarkConfig() { return BenchmarkConfig{}; }

TrafficPlan BuildPathTrafficPlan(const BenchmarkConfig& config, size_t block_size) {
    RequirePositive("levels", config.levels);
    RequirePositive("path_z", config.path_z);
    RequirePositive("block_size", block_size);

    TrafficPlan plan;
    plan.online_round_trips_per_access = 2;
    plan.online_read_bytes_per_access = PathBytesPerTransfer(config, block_size);
    plan.online_write_bytes_per_access = PathBytesPerTransfer(config, block_size);
    return plan;
}

TrafficPlan BuildRingTrafficPlan(const BenchmarkConfig& config, size_t block_size) {
    RequirePositive("levels", config.levels);
    RequirePositive("ring_z", config.ring_z);
    RequirePositive("ring_s", config.ring_s);
    RequirePositive("ring_a", config.ring_a);
    RequirePositive("block_size", block_size);

    const size_t evictions = EvictionCount(config.accesses, config.ring_a);
    TrafficPlan plan;
    plan.online_round_trips_per_access = 1;
    plan.online_read_bytes_per_access = RingOnlineBytesPerAccess(block_size);
    plan.online_write_bytes_per_access = 0;
    plan.eviction_count = evictions;
    plan.eviction_read_bytes = RingEvictionReadBytes(config, block_size, evictions);
    plan.eviction_write_bytes = RingEvictionWriteBytes(config, block_size, evictions);
    return plan;
}

ComparisonResult RunComparison(const BenchmarkConfig& config, size_t block_size) {
    RequirePositive("accesses", config.accesses);
    RequirePositive("block_size", block_size);
    RequirePositive("ring_a", config.ring_a);

    ComparisonResult result;
    result.block_size = block_size;
    result.path = RunPath(config, block_size, config.start_port);
    result.ring = RunRing(config, block_size, config.start_port + 1);
    result.speedup_path_over_ring = result.ring.average_ms_per_access > 0.0
                                        ? result.path.average_ms_per_access /
                                              result.ring.average_ms_per_access
                                        : 0.0;
    return result;
}

std::vector<ComparisonResult> RunAllComparisons(const BenchmarkConfig& config) {
    std::vector<ComparisonResult> results;
    results.reserve(config.block_sizes.size());
    for (size_t i = 0; i < config.block_sizes.size(); ++i) {
        BenchmarkConfig run_config = config;
        run_config.start_port = config.start_port + static_cast<int>(i * 10U);
        results.push_back(RunComparison(run_config, config.block_sizes[i]));
    }
    return results;
}

}  // namespace oram::benchmark
