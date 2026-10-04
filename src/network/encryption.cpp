#include "encryption.h"
#include <cstring>
#include <stdexcept>

#define NONCE_LEN 12
#define TAG_LEN   16

Encryption::Encryption()
    : hAlg(NULL), hKey(NULL), initialized(false), nonceCounter(0) {
    // Open AES-GCM algorithm provider
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
        hAlg = NULL;
        return;
    }
    // Set GCM chaining mode
    if (!BCRYPT_SUCCESS(BCryptSetProperty(
            hAlg,
            BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
            sizeof(BCRYPT_CHAIN_MODE_GCM),
            0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        hAlg = NULL;
        return;
    }
}

Encryption::~Encryption() {
    if (hKey) BCryptDestroyKey(hKey);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
}

void Encryption::Init(const std::vector<uint8_t>& key) {
    if (!hAlg) return;
    if (key.size() != 32) return;

    if (hKey) {
        BCryptDestroyKey(hKey);
        hKey = NULL;
    }

    // Import raw key
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
            hAlg, &hKey, NULL, 0,
            (PUCHAR)key.data(), (ULONG)key.size(), 0))) {
        hKey = NULL;
        return;
    }

    nonceCounter = 0;
    initialized = true;
}

std::vector<uint8_t> Encryption::GenerateSessionKey() {
    std::vector<uint8_t> key(32);
    if (!BCRYPT_SUCCESS(BCryptGenRandom(
            NULL, key.data(), (ULONG)key.size(),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        // Fallback — should not happen on Windows
        for (size_t i = 0; i < key.size(); i++) {
            key[i] = (uint8_t)(rand() & 0xFF);
        }
    }
    return key;
}

std::vector<uint8_t> Encryption::Encrypt(const std::string& plaintext) {
    std::vector<uint8_t> out;
    if (!initialized || !hKey) return out;

    // Build a 12-byte nonce: 8 bytes counter + 4 bytes random-ish
    uint8_t nonce[NONCE_LEN];
    uint64_t ctr = ++nonceCounter;
    memcpy(nonce, &ctr, 8);
    // Fill the last 4 bytes with a value derived from the counter too,
    // so we don't have to call BCryptGenRandom for every message.
    uint32_t tail = (uint32_t)(ctr ^ (ctr >> 32));
    memcpy(nonce + 8, &tail, 4);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce;
    authInfo.cbNonce = NONCE_LEN;
    authInfo.pbTag   = NULL;   // will point into output buffer
    authInfo.cbTag   = TAG_LEN;

    ULONG cbCipher = (ULONG)plaintext.size();
    std::vector<uint8_t> ciphertext(cbCipher);

    // We need the tag buffer alive when BCryptEncrypt runs
    uint8_t tag[TAG_LEN] = {0};
    authInfo.pbTag = tag;
    authInfo.cbTag = TAG_LEN;

    ULONG cbResult = 0;
    NTSTATUS st = BCryptEncrypt(
        hKey,
        (PUCHAR)plaintext.data(), (ULONG)plaintext.size(),
        &authInfo,
        NULL, 0,
        ciphertext.data(), cbCipher,
        &cbResult, 0);

    if (!BCRYPT_SUCCESS(st)) {
        return out;
    }

    ciphertext.resize(cbResult);

    // Assemble: [nonce][ciphertext][tag]
    out.reserve(NONCE_LEN + ciphertext.size() + TAG_LEN);
    out.insert(out.end(), nonce, nonce + NONCE_LEN);
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    out.insert(out.end(), tag, tag + TAG_LEN);
    return out;
}

std::string Encryption::Decrypt(const std::vector<uint8_t>& blob) {
    std::string empty;
    if (!initialized || !hKey) return empty;
    if (blob.size() < NONCE_LEN + TAG_LEN) return empty;

    const uint8_t* nonce = blob.data();
    const uint8_t* tag   = blob.data() + blob.size() - TAG_LEN;
    const uint8_t* ct    = blob.data() + NONCE_LEN;
    ULONG ctLen          = (ULONG)(blob.size() - NONCE_LEN - TAG_LEN);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = (PUCHAR)nonce;
    authInfo.cbNonce = NONCE_LEN;
    authInfo.pbTag   = (PUCHAR)tag;
    authInfo.cbTag   = TAG_LEN;

    std::vector<uint8_t> plaintext(ctLen);
    ULONG cbResult = 0;
    NTSTATUS st = BCryptDecrypt(
        hKey,
        (PUCHAR)ct, ctLen,
        &authInfo,
        NULL, 0,
        plaintext.data(), (ULONG)plaintext.size(),
        &cbResult, 0);

    if (!BCRYPT_SUCCESS(st)) {
        return empty;
    }

    plaintext.resize(cbResult);
    return std::string(plaintext.begin(), plaintext.end());
}