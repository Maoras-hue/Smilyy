#include "connection.h"
#include "../utils/logger.h"
#include "encryption.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

// Global encryption instance for the session
static Encryption g_crypto;

Connection::Connection(const std::string& ip, int port)
    : socket(INVALID_SOCKET), connected(false), server_ip(ip), server_port(port) {
    initialize_winsock();
}

Connection::~Connection() {
    disconnect();
    cleanup_winsock();
}

bool Connection::initialize_winsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::getInstance().log("WSAStartup failed.");
        return false;
    }
    return true;
}

void Connection::cleanup_winsock() {
    WSACleanup();
}

// Read exactly `len` bytes. Returns true on success.
static bool recv_exact(SOCKET s, char* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

bool Connection::connect_to_server() {
    if (connected) return true;

    socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        Logger::getInstance().log("Failed to create socket.");
        return false;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    if (::connect(socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        Logger::getInstance().log("Failed to connect.");
        closesocket(socket);
        socket = INVALID_SOCKET;
        return false;
    }

    // === KEY EXCHANGE HANDSHAKE ===
    // Server sends: [4-byte length=32][32 bytes of session key]
    uint32_t be_len = 0;
    if (!recv_exact(socket, (char*)&be_len, 4)) {
        Logger::getInstance().log("Handshake: failed to read key length.");
        closesocket(socket);
        socket = INVALID_SOCKET;
        return false;
    }
    uint32_t key_len = ntohl(be_len);
    if (key_len != 32) {
        Logger::getInstance().log("Handshake: bad key length.");
        closesocket(socket);
        socket = INVALID_SOCKET;
        return false;
    }

    std::vector<uint8_t> session_key(32);
    if (!recv_exact(socket, (char*)session_key.data(), 32)) {
        Logger::getInstance().log("Handshake: failed to read key.");
        closesocket(socket);
        socket = INVALID_SOCKET;
        return false;
    }

    g_crypto.Init(session_key);

    // Send ack (encrypted) so server knows we're ready
    send_data("READY");

    connected = true;
    Logger::getInstance().log("Connected and encrypted.");
    return true;
}

void Connection::disconnect() {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
    connected = false;
}

bool Connection::send_data(const std::string& data) {
    if (!connected) return false;

    // Encrypt
    std::vector<uint8_t> blob = g_crypto.Encrypt(data);
    if (blob.empty() && !data.empty()) {
        return false;
    }

    // Frame: [4-byte big-endian length][blob]
    uint32_t len = (uint32_t)blob.size();
    uint32_t be = htonl(len);
    if (send(socket, (char*)&be, 4, 0) != 4) return false;

    int sent = 0;
    while (sent < (int)blob.size()) {
        int result = send(socket, (char*)blob.data() + sent,
                          (int)blob.size() - sent, 0);
        if (result == SOCKET_ERROR) return false;
        sent += result;
    }
    return true;
}

bool Connection::receive_data(std::string& data, int timeout_ms) {
    if (!connected) return false;

    if (timeout_ms > 0) {
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   (char*)&timeout_ms, sizeof(timeout_ms));
    }

    // Read 4-byte length prefix
    uint32_t be_len = 0;
    if (!recv_exact(socket, (char*)&be_len, 4)) return false;
    uint32_t len = ntohl(be_len);

    if (len == 0 || len > 16 * 1024 * 1024) return false;

    // Read blob
    std::vector<uint8_t> blob(len);
    if (!recv_exact(socket, (char*)blob.data(), (int)len)) return false;

    // Decrypt
    data = g_crypto.Decrypt(blob);
    return !data.empty();
}