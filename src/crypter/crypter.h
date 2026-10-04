#ifndef CRYPTER_H
#define CRYPTER_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

class Crypter {
public:
    // Encrypt and decrypt payload
    static std::vector<uint8_t> EncryptPayload(const std::vector<uint8_t>& data, const std::string& key);
    static std::vector<uint8_t> DecryptPayload(const std::vector<uint8_t>& data, const std::string& key);
    
    // Simple XOR encryption/decryption (fallback)
    static std::vector<uint8_t> XOREncrypt(const std::vector<uint8_t>& data, const std::string& key);
    static std::vector<uint8_t> XORDecrypt(const std::vector<uint8_t>& data, const std::string& key);
    
    // Generate random encryption key
    static std::string GenerateKey(size_t length = 32);
    
    // Check if data is encrypted
    static bool IsEncrypted(const std::vector<uint8_t>& data);
    
private:
    // Internal helper functions
    static bool DeriveKeyFromPassword(const std::string& password, std::vector<uint8_t>& key, std::vector<uint8_t>& iv);
};

#endif // CRYPTER_H
