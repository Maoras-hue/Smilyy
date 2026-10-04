#include "dns_tunnel.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <iomanip>
#include <regex>
#include <random>

#pragma comment(lib, "ws2_32.lib")

DNSTunnel::DNSTunnel() : is_running(false), max_packet_size(255), ttl(60), recursion_enabled(true) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    dns_server = "8.8.8.8"; // Default to Google DNS
}

DNSTunnel::~DNSTunnel() {
    StopTunnel();
    WSACleanup();
}

bool DNSTunnel::StartTunnel(const std::string& server_domain, int max_packet_size) {
    if (is_running) return false;
    
    this->server_domain = server_domain;
    this->max_packet_size = max_packet_size;
    current_session_id = GenerateSessionID();
    
    is_running = true;
    dns_thread = std::thread(&DNSTunnel::DNSServerLoop, this);
    
    // Start send queue processing
    std::thread sender(&DNSTunnel::ProcessSendQueue, this);
    sender.detach();
    
    return true;
}

bool DNSTunnel::StopTunnel() {
    is_running = false;
    if (dns_thread.joinable()) {
        dns_thread.join();
    }
    return true;
}

void DNSTunnel::DNSServerLoop() {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return;
    
    // Bind to DNS port
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(53);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return;
    }
    
    char buffer[512];
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    
    while (is_running) {
        int bytes = recvfrom(sock, buffer, sizeof(buffer), 0, 
                            (struct sockaddr*)&client_addr, &client_len);
        
        if (bytes > 0) {
            // Process DNS query
            std::vector<uint8_t> response(buffer, buffer + bytes);
            std::string data = ExtractDataFromDNS(response);
            
            if (!data.empty()) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                receive_queue.push(data);
            }
        }
    }
    
    closesocket(sock);
}

void DNSTunnel::ProcessSendQueue() {
    while (is_running) {
        if (!send_queue.empty()) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            std::string data = send_queue.front();
            send_queue.pop();
            
            // Split data into chunks
            int chunk_size = max_packet_size - 50; // Reserve space for domain
            for (size_t i = 0; i < data.length(); i += chunk_size) {
                std::string chunk = data.substr(i, chunk_size);
                std::string encoded = EncodeData(chunk);
                std::string domain = std::to_string(i) + "." + encoded + "." + server_domain;
                
                auto query = BuildDNSQuery(domain, "");
                SendDNSQuery(query);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<uint8_t> DNSTunnel::BuildDNSQuery(const std::string& domain, const std::string& data) {
    std::vector<uint8_t> query;
    
    // DNS Header (12 bytes)
    uint16_t id = rand() % 65535;
    query.push_back(id >> 8);
    query.push_back(id & 0xFF);
    
    // Flags: Standard query with recursion desired
    query.push_back(0x01);
    query.push_back(0x00);
    
    // QDCOUNT: 1 question
    query.push_back(0x00);
    query.push_back(0x01);
    
    // ANCOUNT, NSCOUNT, ARCOUNT: 0
    query.push_back(0x00);
    query.push_back(0x00);
    query.push_back(0x00);
    query.push_back(0x00);
    query.push_back(0x00);
    query.push_back(0x00);
    
    // Question section: domain name
    std::istringstream iss(domain);
    std::string part;
    while (std::getline(iss, part, '.')) {
        query.push_back(part.length());
        for (char c : part) {
            query.push_back(c);
        }
    }
    query.push_back(0x00); // End of name
    
    // QTYPE: A (host address)
    query.push_back(0x00);
    query.push_back(0x01);
    
    // QCLASS: IN (Internet)
    query.push_back(0x00);
    query.push_back(0x01);
    
    return query;
}

bool DNSTunnel::SendDNSQuery(const std::vector<uint8_t>& query) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return false;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53);
    inet_pton(AF_INET, dns_server.c_str(), &server_addr.sin_addr);
    
    int result = sendto(sock, (const char*)query.data(), query.size(), 0,
                       (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    closesocket(sock);
    return result > 0;
}

std::string DNSTunnel::ExtractDataFromDNS(const std::vector<uint8_t>& response) {
    // Very simplified DNS response parsing
    // In reality, you'd need to properly parse DNS packet structure
    
    if (response.size() < 12) return "";
    
    // Skip header
    size_t pos = 12;
    
    // Skip question section
    while (pos < response.size() && response[pos] != 0) {
        uint8_t label_len = response[pos];
        pos += label_len + 1;
    }
    pos++; // Skip the null terminator
    pos += 4; // Skip QTYPE and QCLASS
    
    // Parse answer section
    while (pos < response.size()) {
        // Skip name (may be compressed)
        if ((response[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < response.size() && response[pos] != 0) {
                uint8_t label_len = response[pos];
                pos += label_len + 1;
            }
            pos++;
        }
        
        if (pos + 10 >= response.size()) break;
        
        uint16_t type = (response[pos] << 8) | response[pos + 1];
        pos += 2; // TYPE
        pos += 2; // CLASS
        pos += 4; // TTL
        
        uint16_t data_len = (response[pos] << 8) | response[pos + 1];
        pos += 2;
        
        if (type == 1 && data_len == 4) {
            // A record - extract IP
            pos += data_len;
        } else if (type == 16 && data_len > 0) {
            // TXT record - extract data
            std::string data;
            for (uint16_t i = 0; i < data_len && pos + i < response.size(); i++) {
                data += response[pos + i];
            }
            return DecodeData(data);
        } else {
            pos += data_len;
        }
    }
    
    return "";
}

std::string DNSTunnel::EncodeData(const std::string& data) {
    std::string encoded = EncodeBase64(data);
    // Replace problematic characters for DNS
    std::replace(encoded.begin(), encoded.end(), '+', '-');
    std::replace(encoded.begin(), encoded.end(), '/', '_');
    std::replace(encoded.begin(), encoded.end(), '=', '~');
    return encoded;
}

std::string DNSTunnel::DecodeData(const std::string& encoded) {
    std::string decoded = encoded;
    std::replace(decoded.begin(), decoded.end(), '-', '+');
    std::replace(decoded.begin(), decoded.end(), '_', '/');
    std::replace(decoded.begin(), decoded.end(), '~', '=');
    return DecodeBase64(decoded);
}

std::string DNSTunnel::EncodeBase64(const std::string& data) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    int i = 0, j = 0;
    
    for (size_t idx = 0; idx < data.length(); idx++) {
        char_array_3[i++] = data[idx];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                result += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (j = 0; j < i + 1; j++) {
            result += base64_chars[char_array_4[j]];
        }
        
        while (i++ < 3) {
            result += '=';
        }
    }
    
    return result;
}

std::string DNSTunnel::DecodeBase64(const std::string& data) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    int in_len = data.size();
    int i = 0, j = 0, in = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string result;
    
    while (in_len-- && data[in] != '=') {
        char_array_4[i++] = data[in];
        in++;
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                char_array_4[i] = strchr(base64_chars, char_array_4[i]) - base64_chars;
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++) {
                result += char_array_3[i];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j < 4; j++) {
            char_array_4[j] = 0;
        }
        
        for (j = 0; j < 4; j++) {
            char_array_4[j] = strchr(base64_chars, char_array_4[j]) - base64_chars;
        }
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];
        
        for (j = 0; j < i - 1; j++) {
            result += char_array_3[j];
        }
    }
    
    return result;
}

std::string DNSTunnel::GenerateSessionID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

bool DNSTunnel::SendData(const std::string& data, const std::string& target) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    send_queue.push(data);
    return true;
}

std::string DNSTunnel::ReceiveData(int timeout_ms) {
    // Wait for data with timeout
    auto start = std::chrono::steady_clock::now();
    while (is_running) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!receive_queue.empty()) {
            std::string data = receive_queue.front();
            receive_queue.pop();
            return data;
        }
        
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count() > timeout_ms) {
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return "";
}
