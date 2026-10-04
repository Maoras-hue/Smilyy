#include "memory_obfuscation.h"
#include <random>
#include <algorithm>
#include <wincrypt.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "advapi32.lib")

// Static members
thread_local std::map<std::string, std::string> MemoryObfuscation::decrypted_cache;
std::map<LPVOID, DWORD> MemoryObfuscation::protected_regions;
DWORD MemoryObfuscation::current_protection = PAGE_READWRITE;

MemoryObfuscation::MemoryObfuscation() {
    // Initialize
}

MemoryObfuscation::~MemoryObfuscation() {
    // Cleanup
}

std::string MemoryObfuscation::ObfuscateString(const std::string& str) {
    std::string obfuscated;
    std::string key = GenerateKey(16);
    
    // XOR each character with key and add junk
    for (size_t i = 0; i < str.length(); i++) {
        char encrypted = str[i] ^ key[i % key.length()];
        obfuscated += encrypted;
        
        // Add junk character if i is even
        if (i % 2 == 0) {
            char junk = rand() % 256;
            obfuscated += junk;
        }
    }
    
    // Add key at the end
    obfuscated += "::";
    obfuscated += key;
    
    return obfuscated;
}

std::string MemoryObfuscation::DeobfuscateString(const std::string& str) {
    // Find key separator
    size_t pos = str.find("::");
    if (pos == std::string::npos) return str;
    
    std::string data = str.substr(0, pos);
    std::string key = str.substr(pos + 2);
    
    std::string result;
    int j = 0;
    for (size_t i = 0; i < data.length(); i++) {
        if (i % 2 == 0) {
            char decrypted = data[i] ^ key[j % key.length()];
            result += decrypted;
            j++;
        }
        // Skip junk characters
    }
    
    return result;
}

std::vector<uint8_t> MemoryObfuscation::XOREncrypt(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result = data;
    for (size_t i = 0; i < result.size(); i++) {
        result[i] ^= key[i % key.length()];
    }
    return result;
}

std::vector<uint8_t> MemoryObfuscation::XORDecrypt(const std::vector<uint8_t>& data, const std::string& key) {
    // XOR is symmetric
    return XOREncrypt(data, key);
}

std::vector<uint8_t> MemoryObfuscation::AESEncrypt(const std::vector<uint8_t>& data, const std::string& key) {
    // Use Windows CryptoAPI for AES encryption
    std::vector<uint8_t> result;
    
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    
    // Get crypto context
    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        // Fallback to XOR
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
    
    // Calculate required buffer size
    DWORD dataSize = (DWORD)data.size();
    DWORD bufferSize = dataSize + 16; // Add space for padding
    
    std::vector<BYTE> encrypted(bufferSize);
    memcpy(encrypted.data(), data.data(), dataSize);
    
    // Encrypt
    if (!CryptEncrypt(hKey, 0, TRUE, 0, encrypted.data(), &dataSize, bufferSize)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return XOREncrypt(data, key);
    }
    
    // Copy result
    result.assign(encrypted.begin(), encrypted.begin() + dataSize);
    
    // Cleanup
    CryptDestroyKey(hKey);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    return result;
}

std::vector<uint8_t> MemoryObfuscation::AESDecrypt(const std::vector<uint8_t>& data, const std::string& key) {
    std::vector<uint8_t> result;
    
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
    
    // Decrypt
    DWORD dataSize = (DWORD)data.size();
    std::vector<BYTE> decrypted = data;
    
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

std::string MemoryObfuscation::GenerateKey(size_t length) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(33, 126); // Printable ASCII
    
    std::string key;
    for (size_t i = 0; i < length; i++) {
        key += static_cast<char>(dis(gen));
    }
    return key;
}

std::string MemoryObfuscation::DeriveKey(const std::string& password) {
    // Simple key derivation using SHA-256 via CryptoAPI
    std::string key = password;
    while (key.length() < 32) {
        key += key;
    }
    key = key.substr(0, 32);
    return key;
}

bool MemoryObfuscation::ProtectMemory(LPVOID address, SIZE_T size, DWORD protection) {
    DWORD old_protect;
    if (VirtualProtect(address, size, protection, &old_protect)) {
        protected_regions[address] = old_protect;
        return true;
    }
    return false;
}

bool MemoryObfuscation::EncryptMemoryRegion(LPVOID address, SIZE_T size, const std::string& key) {
    std::vector<uint8_t> data((uint8_t*)address, (uint8_t*)address + size);
    std::vector<uint8_t> encrypted = AESEncrypt(data, key);
    
    if (encrypted.size() <= size) {
        memcpy(address, encrypted.data(), encrypted.size());
        ProtectMemory(address, size, PAGE_READWRITE);
        return true;
    }
    return false;
}

bool MemoryObfuscation::DecryptMemoryRegion(LPVOID address, SIZE_T size, const std::string& key) {
    std::vector<uint8_t> data((uint8_t*)address, (uint8_t*)address + size);
    std::vector<uint8_t> decrypted = AESDecrypt(data, key);
    
    if (decrypted.size() <= size) {
        memcpy(address, decrypted.data(), decrypted.size());
        return true;
    }
    return false;
}

// SecureString implementation
MemoryObfuscation::SecureString::SecureString(const std::string& str) {
    key = GenerateKey(32);
    std::vector<uint8_t> data(str.begin(), str.end());
    encrypted_data = AESEncrypt(data, key);
}

MemoryObfuscation::SecureString::SecureString(const char* str) {
    SecureString(std::string(str));
}

MemoryObfuscation::SecureString::~SecureString() {
    Clear();
}

std::string MemoryObfuscation::SecureString::GetDecrypted() const {
    std::vector<uint8_t> decrypted = AESDecrypt(encrypted_data, key);
    return std::string(decrypted.begin(), decrypted.end());
}

void MemoryObfuscation::SecureString::Clear() {
    // Overwrite memory
    for (size_t i = 0; i < encrypted_data.size(); i++) {
        encrypted_data[i] = 0;
    }
    encrypted_data.clear();
    key.clear();
}

std::wstring MemoryObfuscation::ObfuscateWString(const std::wstring& str) {
    std::string utf8;
    for (wchar_t c : str) {
        utf8 += (char)(c & 0xFF);
        utf8 += (char)((c >> 8) & 0xFF);
    }
    std::string obf = ObfuscateString(utf8);
    std::wstring result;
    for (size_t i = 0; i < obf.length(); i += 2) {
        if (i + 1 < obf.length()) {
            result += (wchar_t)((unsigned char)obf[i] | ((unsigned char)obf[i+1] << 8));
        }
    }
    return result;
}

std::wstring MemoryObfuscation::DeobfuscateWString(const std::wstring& str) {
    std::string utf8;
    for (wchar_t c : str) {
        utf8 += (char)(c & 0xFF);
        utf8 += (char)((c >> 8) & 0xFF);
    }
    std::string dec = DeobfuscateString(utf8);
    std::wstring result;
    for (size_t i = 0; i < dec.length(); i += 2) {
        if (i + 1 < dec.length()) {
            result += (wchar_t)((unsigned char)dec[i] | ((unsigned char)dec[i+1] << 8));
        }
    }
    return result;
}

std::string MemoryObfuscation::DecryptStringA(const std::string& encrypted, const std::string& key) {
    std::vector<uint8_t> data(encrypted.begin(), encrypted.end());
    std::vector<uint8_t> decrypted = AESDecrypt(data, key);
    return std::string(decrypted.begin(), decrypted.end());
}

std::wstring MemoryObfuscation::DecryptStringW(const std::wstring& encrypted, const std::wstring& key) {
    std::string encStr, keyStr;
    for (wchar_t c : encrypted) {
        encStr += (char)(c & 0xFF);
        encStr += (char)((c >> 8) & 0xFF);
    }
    for (wchar_t c : key) {
        keyStr += (char)(c & 0xFF);
        keyStr += (char)((c >> 8) & 0xFF);
    }
    std::string dec = DecryptStringA(encStr, keyStr);
    std::wstring result;
    for (size_t i = 0; i < dec.length(); i += 2) {
        if (i + 1 < dec.length()) {
            result += (wchar_t)((unsigned char)dec[i] | ((unsigned char)dec[i+1] << 8));
        }
    }
    return result;
}

void MemoryObfuscation::ObfuscateControlFlow() {
    // Empty - would be implemented with compiler-specific features
}

void MemoryObfuscation::AddJunkCode() {
    // Empty - would be implemented with compiler-specific features
}

void MemoryObfuscation::MutateCode() {
    // Empty - would be implemented with compiler-specific features
}
