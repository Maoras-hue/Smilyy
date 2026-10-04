#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <cstdint>

#pragma comment(lib, "bcrypt.lib")

class Encryption {
public:
    Encryption();
    ~Encryption();

    // Initialize with a session key (32 bytes for AES-256)
    void Init(const std::vector<uint8_t>& key);

    // Encrypt plaintext -> [12-byte nonce][ciphertext][16-byte tag]
    std::vector<uint8_t> Encrypt(const std::string& plaintext);

    // Decrypt [nonce][ciphertext][tag] -> plaintext (empty on failure)
    std::string Decrypt(const std::vector<uint8_t>& blob);

    // Generate a random 32-byte session key
    static std::vector<uint8_t> GenerateSessionKey();

    bool IsInitialized() const { return initialized; }

private:
    BCRYPT_ALG_HANDLE hAlg;
    BCRYPT_KEY_HANDLE hKey;
    bool initialized;
    uint64_t nonceCounter;
};

#endif // ENCRYPTION_H