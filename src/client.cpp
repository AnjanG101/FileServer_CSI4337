//Author: Brandon Liu
//File: src/client.cpp
//Description: Implementation of a simple client that connects to the file server, sends a filename,

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t kBufferSize = 4096;

bool send_all(int socket_fd, const char* data, std::size_t length) {
    std::size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(socket_fd, data + total_sent, length - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }

    return true;
}

}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: ./client <ip> <port> <filename>" << std::endl;
        return 1;
    }

    const char* server_ip = argv[1];
    const int port = std::stoi(argv[2]);
    const std::string filename = argv[3];

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << server_ip << std::endl;
        close(client_fd);
        return 1;
    }

    std::cout << "Connecting to " << server_ip << ":" << port << std::endl;
    if (connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "Connect failed: " << std::strerror(errno) << std::endl;
        close(client_fd);
        return 1;
    }

    std::cout << "Sending filename: " << filename << std::endl;
    if (!send_all(client_fd, filename.c_str(), filename.size())) {
        std::cerr << "Failed to send filename" << std::endl;
        close(client_fd);
        return 1;
    }

    char buffer[kBufferSize];
    ssize_t received = 0;
    while ((received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        std::cout.write(buffer, received);
    }

    if (received < 0) {
        std::cerr << "Receive failed: " << std::strerror(errno) << std::endl;
        close(client_fd);
        return 1;
    }

    close(client_fd);
    return 0;
}
