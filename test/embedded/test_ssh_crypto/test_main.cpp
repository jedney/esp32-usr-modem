// On-board checks for the PSA Crypto behind the basic SSH client: known-answer tests for
// every algorithm it uses, in place as lib/ssh calls them, and the key-agreement worker.
// No wiring needed; nothing is stored.
#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "ssh_crypto_psa.h"

void setUp() {}
void tearDown() {}

namespace {
size_t hex(const char *text, uint8_t *out) {
    size_t n = 0;
    for (; text[0] && text[1]; text += 2) {
        auto nibble = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        out[n++] = uint8_t(nibble(text[0]) << 4 | nibble(text[1]));
    }
    return n;
}
bool wait(hw::PsaSshCrypto &c) {
    for (int i = 0; i < 500 && c.agreeState() == ssh::Work::kBusy; ++i) {
        delay(5);
    }
    return c.agreeState() == ssh::Work::kDone;
}
}  // namespace

void test_sha256_in_parts() {
    hw::PsaSshCrypto c;
    uint8_t out[32], expect[32];
    hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", expect);
    TEST_ASSERT_TRUE(c.hashBegin());
    TEST_ASSERT_TRUE(c.hashUpdate(reinterpret_cast<const uint8_t *>("a"), 1));
    TEST_ASSERT_TRUE(c.hashUpdate(reinterpret_cast<const uint8_t *>("bc"), 2));
    TEST_ASSERT_TRUE(c.hashFinish(out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 32);
}

// RFC 7748 6.1, with PSA directly: X25519 is available and its output byte order is the one
// RFC 8731 hashes.
void test_x25519_known_answer() {
    uint8_t alice[32], bob[32], shared[32], out[32];
    hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice);
    hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bob);
    hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", shared);
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 255);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    psa_key_id_t key = PSA_KEY_ID_NULL;
    TEST_ASSERT_EQUAL(PSA_SUCCESS, psa_import_key(&attributes, alice, 32, &key));
    size_t n = 0;
    TEST_ASSERT_EQUAL(PSA_SUCCESS,
                      psa_raw_key_agreement(PSA_ALG_ECDH, key, bob, 32, out, sizeof out, &n));
    psa_destroy_key(key);
    TEST_ASSERT_EQUAL(32, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(shared, out, 32);
}

// Both exchanges through the worker task: two ends reach the same secret.
void test_key_agreement_on_the_worker() {
    for (ssh::Exchange kind : {ssh::Exchange::kX25519, ssh::Exchange::kP256}) {
        hw::PsaSshCrypto a, b;
        TEST_ASSERT_TRUE(a.agreeBegin(kind));
        TEST_ASSERT_TRUE(b.agreeBegin(kind));
        TEST_ASSERT_TRUE(wait(a));
        TEST_ASSERT_TRUE(wait(b));
        uint8_t pa[65], pb[65], sa[32], sb[32];
        const size_t na = a.agreePublic(pa, sizeof pa), nb = b.agreePublic(pb, sizeof pb);
        const size_t size = kind == ssh::Exchange::kX25519 ? 32 : 65;
        TEST_ASSERT_EQUAL(size, na);
        TEST_ASSERT_EQUAL(size, nb);
        if (kind == ssh::Exchange::kP256) {
            TEST_ASSERT_EQUAL_UINT8(4, pa[0]);
        }
        const uint32_t pairMs = a.pairMs();
        TEST_ASSERT_TRUE(a.agreeFinish(pb, nb));
        TEST_ASSERT_TRUE(b.agreeFinish(pa, na));
        TEST_ASSERT_TRUE(wait(a));
        TEST_ASSERT_TRUE(wait(b));
        TEST_ASSERT_EQUAL(32, a.agreeSecret(sa, sizeof sa));
        TEST_ASSERT_EQUAL(32, b.agreeSecret(sb, sizeof sb));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(sa, sb, 32);
        TEST_ASSERT_EQUAL(0, a.agreeSecret(sa, sizeof sa));  // handed over once
        char line[96];
        snprintf(line, sizeof line, "%s: key pair %lu ms, shared secret %lu ms",
                 kind == ssh::Exchange::kX25519 ? "X25519" : "P-256", (unsigned long)pairMs,
                 (unsigned long)a.secretMs());
        TEST_MESSAGE(line);
        a.agreeEnd();
        b.agreeEnd();
        TEST_ASSERT_EQUAL(int(ssh::Work::kIdle), int(a.agreeState()));
    }
}

// SP 800-38A F.5.1 and F.5.5, in uneven pieces: the key stream runs on across calls.
void test_aes_ctr_known_answer_in_pieces() {
    struct Case {
        ssh::Cipher cipher;
        const char *key, *expect;
    } cases[] = {
        {ssh::Cipher::kAes128Ctr, "2b7e151628aed2a6abf7158809cf4f3c",
         "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"},
        {ssh::Cipher::kAes256Ctr,
         "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
         "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5"},
    };
    for (const Case &c : cases) {
        hw::PsaSshCrypto crypto;
        uint8_t key[32], iv[16], mac[32] = {}, data[32], expect[32];
        hex(c.key, key);
        hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv);
        hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", data);
        hex(c.expect, expect);
        TEST_ASSERT_TRUE(crypto.keys(1, c.cipher, key, iv, mac));
        TEST_ASSERT_TRUE(crypto.ctr(1, data, 5));
        TEST_ASSERT_TRUE(crypto.ctr(1, data + 5, 11));
        TEST_ASSERT_TRUE(crypto.ctr(1, data + 16, 16));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, data, 32);
        crypto.forget(1);
        TEST_ASSERT_FALSE(crypto.ctr(1, data, 16));
    }
}

// The GCM specification's test cases 3 (no AAD) and 4 (AAD), sealed and opened in place.
void test_aes_gcm_known_answer() {
    const char *plain =
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255";
    const char *cipher =
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
        "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985";
    uint8_t key[16], nonce[12], aad[20], data[64], expect[64], tag[16], expectTag[16];
    hex("feffe9928665731c6d6a8f9467308308", key);
    hex("cafebabefacedbaddecaf888", nonce);
    hex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad);
    hw::PsaSshCrypto c;
    TEST_ASSERT_TRUE(c.keys(0, ssh::Cipher::kAes128Gcm, key, nonce, nullptr));
    hex(plain, data);
    hex(cipher, expect);
    hex("4d5c2af327cd64a62cf35abd2ba6fab4", expectTag);
    TEST_ASSERT_TRUE(c.gcm(0, true, nonce, nullptr, 0, data, 64, tag));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, data, 64);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectTag, tag, 16);
    TEST_ASSERT_TRUE(c.gcm(0, false, nonce, nullptr, 0, data, 64, tag));
    hex(plain, expect);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, data, 64);
    hex(plain, data);
    hex("5bc94fbc3221a5db94fae95ae7121a47", expectTag);
    TEST_ASSERT_TRUE(c.gcm(0, true, nonce, aad, sizeof aad, data, 60, tag));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectTag, tag, 16);
    tag[0] ^= 1;
    TEST_ASSERT_FALSE(c.gcm(0, false, nonce, aad, sizeof aad, data, 60, tag));
}

// HMAC-SHA-256 with a 32-byte key against the RFC 2104 construction built from SHA-256.
void test_hmac_matches_its_definition() {
    uint8_t key[32], ipad[64], opad[64], inner[32], expect[32], out[32];
    memset(key, 0x0b, sizeof key);
    for (size_t i = 0; i < 64; ++i) {
        ipad[i] = (i < 32 ? key[i] : 0) ^ 0x36;
        opad[i] = (i < 32 ? key[i] : 0) ^ 0x5c;
    }
    const uint8_t seq[4] = {0, 0, 0, 7};
    const char *packet = "Hi There, packet";
    hw::PsaSshCrypto c;
    TEST_ASSERT_TRUE(c.hashBegin());
    c.hashUpdate(ipad, 64);
    c.hashUpdate(seq, 4);
    c.hashUpdate(reinterpret_cast<const uint8_t *>(packet), strlen(packet));
    TEST_ASSERT_TRUE(c.hashFinish(inner));
    c.hashBegin();
    c.hashUpdate(opad, 64);
    c.hashUpdate(inner, 32);
    TEST_ASSERT_TRUE(c.hashFinish(expect));
    uint8_t aesKey[16] = {}, iv[16] = {};
    TEST_ASSERT_TRUE(c.keys(0, ssh::Cipher::kAes128Ctr, aesKey, iv, key));
    const ssh::Slice parts[] = {{seq, 4},
                                {reinterpret_cast<const uint8_t *>(packet), strlen(packet)}};
    TEST_ASSERT_TRUE(c.mac(0, parts, 2, out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 32);
}

void test_random_bytes_differ() {
    hw::PsaSshCrypto c;
    uint8_t a[32] = {}, b[32] = {};
    c.random(a, sizeof a);
    c.random(b, sizeof b);
    TEST_ASSERT_TRUE(memcmp(a, b, sizeof a) != 0);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_sha256_in_parts);
    RUN_TEST(test_x25519_known_answer);
    RUN_TEST(test_key_agreement_on_the_worker);
    RUN_TEST(test_aes_ctr_known_answer_in_pieces);
    RUN_TEST(test_aes_gcm_known_answer);
    RUN_TEST(test_hmac_matches_its_definition);
    RUN_TEST(test_random_bytes_differ);
    UNITY_END();
}

void loop() {}
