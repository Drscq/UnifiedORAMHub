#pragma once

#include <cstddef>
#include <vector>

#include "oram/core/Block.h"

namespace oram::core {

class Bucket {
   public:
    explicit Bucket(size_t capacity);

    bool AddBlock(const Block& block);
    size_t GetBlockCount() const;
    size_t GetCapacity() const;
    const std::vector<Block>& GetBlocks() const;

   private:
    size_t capacity_;
    std::vector<Block> blocks_;
};

}  // namespace oram::core
