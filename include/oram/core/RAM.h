#pragma once

#include <cstdint>
#include <vector>

namespace oram::core {

enum class Op { READ, WRITE };

class RAM {
   public:
    virtual ~RAM() = default;

    virtual std::vector<uint8_t> Access(Op op, uint64_t addr, const std::vector<uint8_t>& data) = 0;

    virtual std::vector<uint8_t> Read(uint64_t addr) = 0;
    virtual void Write(uint64_t addr, const std::vector<uint8_t>& data) = 0;
};

}  // namespace oram::core
