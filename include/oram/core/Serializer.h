#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "oram/core/Block.h"
#include "oram/core/Bucket.h"

namespace oram::core {

class Serializer {
   public:
    // Serialize a Block to bytes
    static std::vector<uint8_t> SerializeBlock(const Block& block) {
        std::vector<uint8_t> bytes;

        // Serialize ID (8 bytes)
        uint64_t id = block.GetId();
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&id),
                     reinterpret_cast<uint8_t*>(&id) + sizeof(id));

        // Serialize data size (8 bytes)
        const auto& data = block.GetData();
        uint64_t size = data.size();
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&size),
                     reinterpret_cast<uint8_t*>(&size) + sizeof(size));

        // Serialize data
        bytes.insert(bytes.end(), data.begin(), data.end());

        return bytes;
    }

    // Deserialize bytes to a Block
    static Block DeserializeBlock(const uint8_t* data, size_t& offset) {
        // Read ID
        uint64_t id;
        std::memcpy(&id, data + offset, sizeof(id));
        offset += sizeof(id);

        // Read data size
        uint64_t size;
        std::memcpy(&size, data + offset, sizeof(size));
        offset += sizeof(size);

        // Read data
        std::vector<uint8_t> block_data(data + offset, data + offset + size);
        offset += size;

        return Block(id, block_data);
    }

    // Serialize a Bucket to bytes
    static std::vector<uint8_t> SerializeBucket(const Bucket& bucket) {
        std::vector<uint8_t> bytes;

        // Serialize block count (8 bytes)
        uint64_t count = bucket.GetBlockCount();
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&count),
                     reinterpret_cast<uint8_t*>(&count) + sizeof(count));

        // Serialize each block
        for (const auto& block : bucket.GetBlocks()) {
            auto block_bytes = SerializeBlock(block);
            bytes.insert(bytes.end(), block_bytes.begin(), block_bytes.end());
        }

        return bytes;
    }

    // Deserialize bytes to a Bucket
    static Bucket DeserializeBucket(const std::vector<uint8_t>& bytes, size_t capacity) {
        Bucket bucket(capacity);
        size_t offset = 0;

        // Read block count
        uint64_t count;
        std::memcpy(&count, bytes.data() + offset, sizeof(count));
        offset += sizeof(count);

        // Read each block
        for (size_t i = 0; i < count; ++i) {
            Block block = DeserializeBlock(bytes.data(), offset);
            bucket.AddBlock(block);
        }

        return bucket;
    }
};

}  // namespace oram::core
