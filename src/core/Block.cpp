#include "oram/core/Block.h"

namespace oram::core {

Block::Block(uint64_t id, std::vector<uint8_t> data) : id_(id), data_(std::move(data)) {}

uint64_t Block::GetId() const { return id_; }

const std::vector<uint8_t>& Block::GetData() const { return data_; }

}  // namespace oram::core
