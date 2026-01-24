#include "oram/crypto/AES_CTR.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace oram::crypto;

TEST(AES_CTRTest, EncryptDecrypt128) {
    auto key = AES_CTR::GenerateKey(AES_CTR::kKeySize128);
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher(key);

    std::string message = "Hello, ORAM World!";
    std::vector<uint8_t> plaintext(message.begin(), message.end());

    auto ciphertext = cipher.Encrypt(plaintext, iv);

    // Ciphertext should be different from plaintext
    EXPECT_NE(ciphertext, plaintext);

    // Decrypt should recover original
    auto decrypted = cipher.Decrypt(ciphertext, iv);
    EXPECT_EQ(decrypted, plaintext);
}

TEST(AES_CTRTest, EncryptDecrypt256) {
    auto key = AES_CTR::GenerateKey(AES_CTR::kKeySize256);
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher(key);

    std::vector<uint8_t> plaintext = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

    auto ciphertext = cipher.Encrypt(plaintext, iv);
    auto decrypted = cipher.Decrypt(ciphertext, iv);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(AES_CTRTest, DifferentIVProducesDifferentCiphertext) {
    auto key = AES_CTR::GenerateKey();
    auto iv1 = AES_CTR::GenerateIV();
    auto iv2 = AES_CTR::GenerateIV();

    AES_CTR cipher(key);

    std::vector<uint8_t> plaintext = {0xDE, 0xAD, 0xBE, 0xEF};

    auto ct1 = cipher.Encrypt(plaintext, iv1);
    auto ct2 = cipher.Encrypt(plaintext, iv2);

    // Same plaintext with different IVs should produce different ciphertexts
    EXPECT_NE(ct1, ct2);
}

TEST(AES_CTRTest, InvalidKeySize) {
    std::vector<uint8_t> bad_key = {0x01, 0x02, 0x03};  // Too short
    EXPECT_THROW(AES_CTR cipher(bad_key), std::invalid_argument);
}

TEST(AES_CTRTest, InvalidIVSize) {
    auto key = AES_CTR::GenerateKey();
    AES_CTR cipher(key);

    std::vector<uint8_t> plaintext = {0x01, 0x02};
    std::vector<uint8_t> bad_iv = {0x01, 0x02, 0x03};  // Too short

    EXPECT_THROW(cipher.Encrypt(plaintext, bad_iv), std::invalid_argument);
}

TEST(AES_CTRTest, InPlaceEncryption) {
    auto key = AES_CTR::GenerateKey();
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher(key);

    std::vector<uint8_t> original = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    std::vector<uint8_t> data = original;

    // Encrypt in place
    cipher.EncryptInPlace(data.data(), data.size(), iv.data());

    // Data should be modified
    EXPECT_NE(data, original);

    // Decrypt in place
    cipher.DecryptInPlace(data.data(), data.size(), iv.data());

    // Should recover original
    EXPECT_EQ(data, original);
}

TEST(AES_CTRTest, InPlaceMatchesBufferVersion) {
    auto key = AES_CTR::GenerateKey();
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher(key);

    std::vector<uint8_t> plaintext = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // Buffer version
    auto ciphertext = cipher.Encrypt(plaintext, iv);

    // In-place version
    std::vector<uint8_t> in_place_data = plaintext;
    cipher.EncryptInPlace(in_place_data.data(), in_place_data.size(), iv.data());

    // Should produce same result
    EXPECT_EQ(ciphertext, in_place_data);
}

TEST(AES_CTRTest, BatchEncryption) {
    auto key = AES_CTR::GenerateKey();

    AES_CTR cipher(key);

    std::vector<std::vector<uint8_t>> plaintexts = {
        {0x01, 0x02, 0x03}, {0x04, 0x05, 0x06}, {0x07, 0x08, 0x09}};

    std::vector<std::vector<uint8_t>> ivs = {AES_CTR::GenerateIV(), AES_CTR::GenerateIV(),
                                             AES_CTR::GenerateIV()};

    std::vector<std::vector<uint8_t>> ciphertexts;
    cipher.EncryptBlocks(plaintexts, ivs, ciphertexts);

    EXPECT_EQ(ciphertexts.size(), 3);

    // Verify each can be decrypted
    for (size_t i = 0; i < plaintexts.size(); ++i) {
        auto decrypted = cipher.Decrypt(ciphertexts[i], ivs[i]);
        EXPECT_EQ(decrypted, plaintexts[i]);
    }
}

TEST(AES_CTRTest, IncrementCounter) {
    std::vector<uint8_t> iv = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

    AES_CTR::IncrementCounter(iv.data(), 1);

    EXPECT_EQ(iv[15], 0x02);
    EXPECT_EQ(iv[14], 0x00);

    // Test overflow
    iv[15] = 0xFF;
    AES_CTR::IncrementCounter(iv.data(), 1);
    EXPECT_EQ(iv[15], 0x00);
    EXPECT_EQ(iv[14], 0x01);
}

TEST(AES_CTRTest, MoveSemantics) {
    auto key = AES_CTR::GenerateKey();
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher1(key);
    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03};
    auto ct1 = cipher1.Encrypt(plaintext, iv);

    // Move construct
    AES_CTR cipher2(std::move(cipher1));
    auto ct2 = cipher2.Encrypt(plaintext, iv);

    // Should produce same ciphertext
    EXPECT_EQ(ct1, ct2);
}

TEST(AES_CTRTest, RawPointerConstructor) {
    uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher(key, sizeof(key));

    std::vector<uint8_t> plaintext = {0xAB, 0xCD, 0xEF};
    auto ciphertext = cipher.Encrypt(plaintext, iv);
    auto decrypted = cipher.Decrypt(ciphertext, iv);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(AES_CTRTest, LargeDataEncryption) {
    auto key = AES_CTR::GenerateKey();
    auto iv = AES_CTR::GenerateIV();

    AES_CTR cipher(key);

    // 1MB of data (typical ORAM block sizes can be large)
    std::vector<uint8_t> plaintext(1024 * 1024);
    for (size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto ciphertext = cipher.Encrypt(plaintext, iv);
    auto decrypted = cipher.Decrypt(ciphertext, iv);

    EXPECT_EQ(decrypted, plaintext);
}
