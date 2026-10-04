#include "crypter.h"
#include <wincrypt.h>
#include <random>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

std::vector<uint8_t> Crypter::EncryptPayload(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result;
    
    if (data.empty() || key.empty()) {
        return result;
    }
    
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    
    // Get crypto context
    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return XOREncrypt(data, key);
    }
    
    // Create hash for key derivation
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Hash the key
    if (!CryptHashData(hHash, (BYTE*)key.c_str(), (DWORD)key.length(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Derive AES key
    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Generate random IV
    BYTE iv[16];
    if (!CryptGenRandom(hProv, sizeof(iv), iv)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Set IV
    if (!CryptSetKeyParam(hKey, KP_IV, iv, 0)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Prepare for encryption
    DWORD dataSize = (DWORD)data.size();
    DWORD bufferSize = dataSize + 16;
    std::vector<BYTE> encrypted(bufferSize);
    memcpy(encrypted.data(), data.data(), dataSize);
    
    // Encrypt
    if (!CryptEncrypt(hKey, 0, TRUE, 0, encrypted.data(), &dataSize, bufferSize)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Prepend IV to encrypted data
    result.insert(result.end(), iv, iv + sizeof(iv));
    result.insert(result.end(), encrypted.begin(), encrypted.begin() + dataSize);
    
    // Cleanup
    CryptDestroyKey(hKey);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    return result;
}

std::vector<uint8_t> Crypter::DecryptPayload(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result;
    
    if (data.size() < 16 || key.empty()) {
        return result;
    }
    
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    
    // Get crypto context
    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return XORDecrypt(data, key);
    }
    
    // Create hash for key derivation
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return XORDecrypt(data, key);
    }
    
    // Hash the key
    if (!CryptHashData(hHash, (BYTE*)key.c_str(), (DWORD)key.length(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XORDecrypt(data, key);
    }
    
    // Derive AES key
    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XORDecrypt(data, key);
    }
    
    // Extract IV from data
    BYTE iv[16];
    memcpy(iv, data.data(), sizeof(iv));
    
    // Set IV
    if (!CryptSetKeyParam(hKey, KP_IV, iv, 0)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XORDecrypt(data, key);
    }
    
    // Prepare for decryption
    DWORD dataSize = (DWORD)data.size() - sizeof(iv);
    std::vector<BYTE> decrypted(data.begin() + sizeof(iv), data.end());
    
    // Decrypt
    if (!CryptDecrypt(hKey, 0, TRUE, 0, decrypted.data(), &dataSize)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XORDecrypt(data, key);
    }
    
    // Copy result
    result.assign(decrypted.begin(), decrypted.begin() + dataSize);
    
    // Cleanup
    CryptDestroyKey(hKey);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    return result;
}

std::vector<uint8_t> Crypter::XOREncrypt(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result = data;
    for (size_t i = 0; i < result.size(); i++) {
        result[i] ^= key[i % key.length()];
    }
    return result;
}

std::vector<uint8_t> Crypter::XORDecrypt(const std::vector<uint8_t>& data, const std::string& key) {
    // XOR is symmetric
    return XOREncrypt(data, key);
}

// FIXED: Match the header declaration
std::string Crypter::GenerateKey(size_t length) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(33, 126);
    
    std::string key;
    for (size_t i = 0; i < length; i++) {
        key += static_cast<char>(dis(gen));
    }
    return key;
}

bool Crypter::IsEncrypted(const std::vector<uint8_t>& data) {
    return !data.empty();
}

bool Crypter::DeriveKeyFromPassword(const std::string& password, std::vector<uint8_t>& key, std::vector<uint8_t>& iv) {
    // Simple key derivation - for production use proper KDF
    key.assign(password.begin(), password.end());
    while (key.size() < 32) {
        key.push_back(0);
    }
    if (key.size() > 32) {
        key.resize(32);
    }
    
    // Fixed IV for simplicity - in production use random IV
    iv.assign(16, 0);
    return true;
}
