#ifndef CONNECTION_H
#define CONNECTION_H
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
class Connection {
private:
    SOCKET socket;
    bool connected;
    std::string server_ip;
    int server_port;
    bool initialize_winsock();
    void cleanup_winsock();
public:
    Connection(const std::string& ip, int port);
    ~Connection();
    bool connect_to_server();
    void disconnect();
    bool send_data(const std::string& data);
    bool receive_data(std::string& data, int timeout_ms = 5000);
    bool is_connected() const { return connected; }
    SOCKET get_socket() const { return socket; }
};
#endif
