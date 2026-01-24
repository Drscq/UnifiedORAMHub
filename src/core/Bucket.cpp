#include "oram/core/Bucket.h"

namespace oram::core {

Bucket::Bucket(size_t capacity) : capacity_(capacity) { blocks_.reserve(capacity); }

bool Bucket::AddBlock(const Block& block) {
    if (blocks_.size() >= capacity_) {
        return false;
    }
    blocks_.push_back(block);
    return true;
}

size_t Bucket::GetBlockCount() const { return blocks_.size(); }

size_t Bucket::GetCapacity() const { return capacity_; }

const std::vector<Block>& Bucket::GetBlocks() const { return blocks_; }

}  // namespace oram::core
