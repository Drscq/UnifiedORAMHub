#include <iostream>
#include <cstdint>
#include <cassert>
#include <string>
#include <bitset>

// ─────────────────────────────────────────────
//  Helper: print a __int128 value as decimal
// ─────────────────────────────────────────────
std::string int128_to_string(__int128 v) {
    if (v == 0) return "0";
    bool neg = (v < 0);
    unsigned __int128 u = neg ? -v : v;
    std::string s;
    while (u > 0) {
        s = char('0' + (u % 10)) + s;
        u /= 10;
    }
    return neg ? "-" + s : s;
}

// ─────────────────────────────────────────────
//  Custom 96-bit unsigned integer
//  Layout: hi (32 bits) | lo (64 bits)
// ─────────────────────────────────────────────
struct uint96_t {
    uint64_t lo;   // lower 64 bits
    uint32_t hi;   // upper 32 bits

    uint96_t(uint32_t h = 0, uint64_t l = 0) : lo(l), hi(h) {}

    // Construct from a 64-bit value (hi = 0)
    explicit uint96_t(uint64_t v) : lo(v), hi(0) {}

    // Addition
    uint96_t operator+(const uint96_t& o) const {
        uint96_t r;
        r.lo = lo + o.lo;
        uint32_t carry = (r.lo < lo) ? 1 : 0;
        r.hi = hi + o.hi + carry;
        return r;
    }

    // Left shift by 1 (for building large numbers)
    uint96_t operator<<(int n) const {
        uint96_t r = *this;
        for (int i = 0; i < n; ++i) {
            uint32_t carry = (r.lo >> 63) & 1;
            r.lo <<= 1;
            r.hi = (r.hi << 1) | carry;
        }
        return r;
    }

    bool operator==(const uint96_t& o) const {
        return lo == o.lo && hi == o.hi;
    }

    void print() const {
        // Print as hex for clarity
        printf("0x%08X_%016lX", hi, (unsigned long)lo);
    }
};

// ─────────────────────────────────────────────
//  Tests
// ─────────────────────────────────────────────
void test_int128() {
    std::cout << "\n=== __int128 (128-bit) ===\n";

    __int128 a = (__int128)1 << 96;   // 2^96
    __int128 b = (__int128)1 << 64;   // 2^64
    __int128 c = a + b + 42;

    std::cout << "2^96            = " << int128_to_string(a) << "\n";
    std::cout << "2^64            = " << int128_to_string(b) << "\n";
    std::cout << "2^96 + 2^64 +42 = " << int128_to_string(c) << "\n";

    // Verify max 96-bit value (2^96 - 1)
    __int128 max96 = ((__int128)1 << 96) - 1;
    std::cout << "Max 96-bit val  = " << int128_to_string(max96) << "\n";

    // Overflow check: max96 + 1 == 2^96
    assert(max96 + 1 == a);
    std::cout << "Overflow check  PASSED\n";

    // Bit width sanity
    assert(sizeof(__int128) == 16);  // 128 bits = 16 bytes
    std::cout << "sizeof(__int128)= " << sizeof(__int128) << " bytes (128 bits)\n";
}

void test_uint96() {
    std::cout << "\n=== Custom uint96_t (96-bit) ===\n";

    // 2^95 (highest bit of a 96-bit number)
    uint96_t two95(0x80000000u, 0ULL);  // hi=2^31, lo=0 → total = 2^(31+64) = 2^95
    std::cout << "2^95 = "; two95.print(); std::cout << "\n";

    // Max 96-bit value: hi=0xFFFFFFFF, lo=0xFFFFFFFFFFFFFFFF
    uint96_t maxVal(0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFULL);
    std::cout << "Max  = "; maxVal.print(); std::cout << "\n";

    // Addition: 1 + max should wrap hi to 0 (overflow)
    uint96_t one(0, 1ULL);
    uint96_t wrapped = maxVal + one;
    std::cout << "Max+1= "; wrapped.print();
    assert(wrapped.hi == 0 && wrapped.lo == 0);
    std::cout << "  (overflow → 0, PASSED)\n";

    // Left shift
    uint96_t base(0, 1ULL);          // = 1
    uint96_t shifted = base << 64;   // should move lo bit into hi
    std::cout << "1<<64= "; shifted.print(); std::cout << "\n";
    assert(shifted.hi == 1 && shifted.lo == 0);
    std::cout << "Shift check PASSED\n";

    std::cout << "sizeof(uint96_t)= " << sizeof(uint96_t)
              << " bytes (" << sizeof(uint96_t)*8 << " bits, padded)\n";
}

int main() {
    std::cout << "=== Large Integer Width Test ===\n";
    std::cout << "Platform int sizes:\n";
    std::cout << "  uint32_t : " << sizeof(uint32_t)*8 << " bits\n";
    std::cout << "  uint64_t : " << sizeof(uint64_t)*8 << " bits\n";

    test_int128();
    test_uint96();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
