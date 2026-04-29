#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t kMaxFilenameSize = 1024;
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

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./server <port>" << std::endl;
        return 1;
    }

    const int port = std::stoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed: " << std::strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        std::cerr << "Listen failed: " << std::strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "Server listening on port " << port << std::endl;

    sockaddr_in client_addr {};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
    if (client_fd < 0) {
        std::cerr << "Accept failed: " << std::strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    std::cout << "Client connected from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

    char filename_buffer[kMaxFilenameSize + 1] = {0};
    ssize_t received = recv(client_fd, filename_buffer, kMaxFilenameSize, 0);
    if (received <= 0) {
        std::cerr << "Failed to receive filename" << std::endl;
        close(client_fd);
        close(server_fd);
        return 1;
    }

    std::string filename(filename_buffer, static_cast<std::size_t>(received));
    std::cout << "Requested file: " << filename << std::endl;

    std::ifstream input_file(filename, std::ios::binary);
    if (!input_file.is_open()) {
        const std::string error_message = "ERROR: file not found";
        std::cerr << "File not found: " << filename << std::endl;
        if (!send_all(client_fd, error_message.c_str(), error_message.size())) {
            std::cerr << "Failed to send error message" << std::endl;
        }
        close(client_fd);
        close(server_fd);
        return 0;
    }

    std::cout << "Sending file..." << std::endl;
    char buffer[kBufferSize];
    while (input_file.good()) {
        input_file.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = input_file.gcount();
        if (bytes_read <= 0) {
            break;
        }

        if (!send_all(client_fd, buffer, static_cast<std::size_t>(bytes_read))) {
            std::cerr << "Failed while sending file contents" << std::endl;
            close(client_fd);
            close(server_fd);
            return 1;
        }
    }

    std::cout << "Transfer complete. Closing connection." << std::endl;
    close(client_fd);
    close(server_fd);
    return 0;
}
