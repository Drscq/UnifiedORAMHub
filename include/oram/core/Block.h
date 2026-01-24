#pragma once

#include <cstdint>
#include <vector>

namespace oram::core {

class Block {
   public:
    Block(uint64_t id, std::vector<uint8_t> data);

    uint64_t GetId() const;
    const std::vector<uint8_t>& GetData() const;

   private:
    uint64_t id_;
    std::vector<uint8_t> data_;
};

}  // namespace oram::core
