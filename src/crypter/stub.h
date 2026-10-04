#ifndef STUB_H
#define STUB_H

#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>

// Function to decrypt the payload
std::vector<uint8_t> DecryptPayload(const BYTE* data, DWORD size, const std::string& key);

// Function to execute the decrypted payload
bool ExecutePayload(const std::vector<uint8_t>& payload);

#endif // STUB_H
