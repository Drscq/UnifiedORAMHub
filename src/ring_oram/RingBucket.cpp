#include "oram/ring_oram/RingBucket.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace oram::ring_oram {

namespace {

std::vector<size_t> MakePermutation(size_t size, std::mt19937_64* prng) {
    std::vector<size_t> permutation(size, 0);
    std::iota(permutation.begin(), permutation.end(), 0);
    if (prng != nullptr) {
        std::shuffle(permutation.begin(), permutation.end(), *prng);
    }
    return permutation;
}

RingBlock NormalizeBlock(RingBlock block, size_t block_size) {
    block.data.resize(block_size, 0);
    return block;
}

RingBlock MakeDummyBlock(size_t block_size, std::mt19937_64* prng) {
    RingBlock block = RingBlock::Dummy(block_size);
    (void)prng;
    return block;
}

void AppendUint64(std::vector<uint8_t>* out, uint64_t value) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&value);
    out->insert(out->end(), begin, begin + sizeof(value));
}

uint64_t ReadUint64(const std::vector<uint8_t>& bytes, size_t* offset) {
    if (*offset + sizeof(uint64_t) > bytes.size()) {
        throw std::invalid_argument("Ring ORAM serialized payload is truncated");
    }
    uint64_t value = 0;
    std::memcpy(&value, bytes.data() + *offset, sizeof(value));
    *offset += sizeof(value);
    return value;
}

void AppendBytes(std::vector<uint8_t>* out, const std::vector<uint8_t>& bytes) {
    AppendUint64(out, bytes.size());
    out->insert(out->end(), bytes.begin(), bytes.end());
}

std::vector<uint8_t> ReadBytes(const std::vector<uint8_t>& bytes, size_t* offset) {
    const uint64_t size = ReadUint64(bytes, offset);
    if (*offset + size > bytes.size()) {
        throw std::invalid_argument("Ring ORAM serialized byte vector is truncated");
    }
    std::vector<uint8_t> result(bytes.begin() + static_cast<std::ptrdiff_t>(*offset),
                                bytes.begin() + static_cast<std::ptrdiff_t>(*offset + size));
    *offset += size;
    return result;
}

void AppendBoolVector(std::vector<uint8_t>* out, const std::vector<bool>& values) {
    AppendUint64(out, values.size());
    for (bool value : values) {
        out->push_back(value ? 1U : 0U);
    }
}

std::vector<bool> ReadBoolVector(const std::vector<uint8_t>& bytes, size_t* offset) {
    const uint64_t size = ReadUint64(bytes, offset);
    if (*offset + size > bytes.size()) {
        throw std::invalid_argument("Ring ORAM serialized bool vector is truncated");
    }
    std::vector<bool> result(size, false);
    for (uint64_t i = 0; i < size; ++i) {
        result[i] = bytes[*offset + i] != 0;
    }
    *offset += size;
    return result;
}

void AppendField(std::vector<uint8_t>* out, const EncryptedField& field) {
    AppendBytes(out, field.iv);
    AppendBytes(out, field.ciphertext);
}

EncryptedField ReadField(const std::vector<uint8_t>& bytes, size_t* offset) {
    EncryptedField field;
    field.iv = ReadBytes(bytes, offset);
    field.ciphertext = ReadBytes(bytes, offset);
    return field;
}

void AppendFields(std::vector<uint8_t>* out, const std::vector<EncryptedField>& fields) {
    AppendUint64(out, fields.size());
    for (const auto& field : fields) {
        AppendField(out, field);
    }
}

std::vector<EncryptedField> ReadFields(const std::vector<uint8_t>& bytes, size_t* offset) {
    const uint64_t size = ReadUint64(bytes, offset);
    std::vector<EncryptedField> fields;
    fields.reserve(size);
    for (uint64_t i = 0; i < size; ++i) {
        fields.push_back(ReadField(bytes, offset));
    }
    return fields;
}

}  // namespace

RingBlock RingBlock::Dummy(size_t block_size) {
    return RingBlock{kDummyAddress, 0, std::vector<uint8_t>(block_size, 0)};
}

std::vector<uint8_t> SerializeEncryptedRingBucket(const EncryptedRingBucket& bucket) {
    std::vector<uint8_t> bytes;
    AppendUint64(&bytes, bucket.count);
    AppendBoolVector(&bytes, bucket.valids);
    AppendFields(&bytes, bucket.addrs);
    AppendFields(&bytes, bucket.leaves);
    AppendFields(&bytes, bucket.ptrs);
    AppendFields(&bytes, bucket.data);
    return bytes;
}

EncryptedRingBucket DeserializeEncryptedRingBucket(const std::vector<uint8_t>& bytes) {
    size_t offset = 0;
    EncryptedRingBucket bucket;
    bucket.count = ReadUint64(bytes, &offset);
    bucket.valids = ReadBoolVector(bytes, &offset);
    bucket.addrs = ReadFields(bytes, &offset);
    bucket.leaves = ReadFields(bytes, &offset);
    bucket.ptrs = ReadFields(bytes, &offset);
    bucket.data = ReadFields(bytes, &offset);
    if (offset != bytes.size()) {
        throw std::invalid_argument("Ring ORAM encrypted bucket has trailing bytes");
    }
    return bucket;
}

std::vector<uint8_t> SerializeRingBucketMetadata(const RingBucketMetadata& metadata) {
    std::vector<uint8_t> bytes;
    AppendUint64(&bytes, metadata.count);
    AppendBoolVector(&bytes, metadata.valids);
    AppendFields(&bytes, metadata.addrs);
    AppendFields(&bytes, metadata.leaves);
    AppendFields(&bytes, metadata.ptrs);
    return bytes;
}

RingBucketMetadata DeserializeRingBucketMetadata(const std::vector<uint8_t>& bytes) {
    size_t offset = 0;
    RingBucketMetadata metadata;
    metadata.count = ReadUint64(bytes, &offset);
    metadata.valids = ReadBoolVector(bytes, &offset);
    metadata.addrs = ReadFields(bytes, &offset);
    metadata.leaves = ReadFields(bytes, &offset);
    metadata.ptrs = ReadFields(bytes, &offset);
    if (offset != bytes.size()) {
        throw std::invalid_argument("Ring ORAM bucket metadata has trailing bytes");
    }
    return metadata;
}

RingBucket::RingBucket() = default;

RingBucket::RingBucket(size_t z, size_t s, size_t block_size)
    : count(0),
      valids(z + s, true),
      addrs(z, kDummyAddress),
      leaves(z, 0),
      ptrs(z, 0),
      data(z + s, RingBlock::Dummy(block_size)),
      z_(z),
      s_(s),
      block_size_(block_size) {
    for (size_t i = 0; i < ptrs.size(); ++i) {
        ptrs[i] = i;
    }
}

void RingBucket::ResetWithBlocks(const std::vector<RingBlock>& blocks, std::mt19937_64* prng) {
    if (blocks.size() > z_) {
        throw std::invalid_argument("Too many real blocks for Ring ORAM bucket");
    }
    for (const auto& block : blocks) {
        if (block.IsDummy()) {
            throw std::invalid_argument("Ring ORAM bucket real block list contains a dummy");
        }
    }

    count = 0;
    std::fill(valids.begin(), valids.end(), true);
    std::fill(addrs.begin(), addrs.end(), kDummyAddress);
    std::fill(leaves.begin(), leaves.end(), 0);
    data.clear();
    data.reserve(SlotCount());
    for (size_t i = 0; i < SlotCount(); ++i) {
        data.push_back(MakeDummyBlock(block_size_, prng));
    }

    const auto permutation = MakePermutation(SlotCount(), prng);
    for (size_t i = 0; i < ptrs.size(); ++i) {
        ptrs[i] = permutation[i];
    }

    for (size_t i = 0; i < blocks.size(); ++i) {
        const size_t offset = ptrs[i];
        RingBlock block = NormalizeBlock(blocks[i], block_size_);
        addrs[i] = block.addr;
        leaves[i] = block.leaf;
        data[offset] = std::move(block);
    }
}

}  // namespace oram::ring_oram
