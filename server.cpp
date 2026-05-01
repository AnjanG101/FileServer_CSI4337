#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

constexpr std::size_t kMaxFilenameSize = 1024;
constexpr std::size_t kBufferSize = 4096;
constexpr int kDefaultWorkerCount = 4;
constexpr std::size_t kDefaultQueueSize = 20;
constexpr const char* kFileNotFoundMessage = "ERROR: file not found";

volatile std::sig_atomic_t g_stop_requested = 0;

struct ServerConfig {
    int port = 0;
    int worker_count = kDefaultWorkerCount;
    std::size_t queue_size = kDefaultQueueSize;
};

struct Request {
    int client_socket_fd = -1;
    std::string filename;
    std::string client_address;
    std::chrono::steady_clock::time_point arrival_timestamp;
    std::size_t file_size_estimate = 0;
};

class RequestQueue {
public:
    explicit RequestQueue(std::size_t max_size) : max_size_(max_size) {}

    void push(Request request) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [&] { return shutdown_ || queue_.size() < max_size_; });
        if (shutdown_) {
            close(request.client_socket_fd);
            return;
        }

        queue_.push(std::move(request));
        not_empty_.notify_one();
    }

    std::optional<Request> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [&] { return shutdown_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }

        Request request = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return request;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::size_t max_size_;
    std::queue<Request> queue_;
    bool shutdown_ = false;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

bool send_all(int socket_fd, const char* data, std::size_t length) {
    std::size_t total_sent = 0;

    while (total_sent < length) {
        const ssize_t sent = send(socket_fd, data + total_sent, length - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }

    return true;
}

std::size_t estimate_file_size(const std::string& filename) {
    struct stat file_info {};
    if (stat(filename.c_str(), &file_info) == 0 && S_ISREG(file_info.st_mode)) {
        return static_cast<std::size_t>(file_info.st_size);
    }
    return 0;
}

std::string make_client_address(const sockaddr_in& client_addr) {
    char client_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    return std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
}

void handle_signal(int) {
    g_stop_requested = 1;
}

ServerConfig parse_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::runtime_error("Usage: ./server <port> [--workers <n>] [--queue <size>]");
    }

    ServerConfig config;
    config.port = std::stoi(argv[1]);

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workers" && i + 1 < argc) {
            config.worker_count = std::stoi(argv[++i]);
        } else if (arg == "--queue" && i + 1 < argc) {
            config.queue_size = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else {
            throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
    }

    if (config.port <= 0 || config.port > 65535) {
        throw std::runtime_error("Port must be between 1 and 65535");
    }
    if (config.worker_count <= 0) {
        throw std::runtime_error("Worker count must be positive");
    }
    if (config.queue_size == 0) {
        throw std::runtime_error("Queue size must be positive");
    }

    return config;
}

void serve_request(const Request& request, int worker_id) {
    std::cout << "Worker " << worker_id << " serving " << request.filename
              << " for " << request.client_address << std::endl;

    std::ifstream input_file(request.filename, std::ios::binary);
    if (!input_file.is_open()) {
        std::cerr << "File not found: " << request.filename << std::endl;
        if (!send_all(request.client_socket_fd,
                      kFileNotFoundMessage,
                      std::strlen(kFileNotFoundMessage))) {
            std::cerr << "Failed to send error message" << std::endl;
        }
        close(request.client_socket_fd);
        return;
    }

    char buffer[kBufferSize];
    while (input_file.good()) {
        input_file.read(buffer, sizeof(buffer));
        const std::streamsize bytes_read = input_file.gcount();
        if (bytes_read <= 0) {
            break;
        }

        if (!send_all(request.client_socket_fd, buffer, static_cast<std::size_t>(bytes_read))) {
            std::cerr << "Failed while sending file contents" << std::endl;
            close(request.client_socket_fd);
            return;
        }
    }

    close(request.client_socket_fd);
}

void worker_loop(int worker_id, RequestQueue& request_queue) {
    while (true) {
        std::optional<Request> request = request_queue.pop();
        if (!request.has_value()) {
            return;
        }
        serve_request(*request, worker_id);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const ServerConfig config = parse_arguments(argc, argv);

        struct sigaction signal_action {};
        signal_action.sa_handler = handle_signal;
        sigemptyset(&signal_action.sa_mask);
        sigaction(SIGINT, &signal_action, nullptr);
        sigaction(SIGTERM, &signal_action, nullptr);

        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Failed to create socket: " << std::strerror(errno) << std::endl;
            return 1;
        }

        int reuse_addr = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
            std::cerr << "setsockopt failed: " << std::strerror(errno) << std::endl;
            close(server_fd);
            return 1;
        }

        sockaddr_in server_addr {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(config.port);

        if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            std::cerr << "Bind failed: " << std::strerror(errno) << std::endl;
            close(server_fd);
            return 1;
        }

        if (listen(server_fd, SOMAXCONN) < 0) {
            std::cerr << "Listen failed: " << std::strerror(errno) << std::endl;
            close(server_fd);
            return 1;
        }

        RequestQueue request_queue(config.queue_size);
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(config.worker_count));
        for (int worker_id = 0; worker_id < config.worker_count; ++worker_id) {
            workers.emplace_back(worker_loop, worker_id, std::ref(request_queue));
        }

        std::cout << "Server listening on port " << config.port
                  << " with workers=" << config.worker_count
                  << " and queue=" << config.queue_size << std::endl;

        while (!g_stop_requested) {
            sockaddr_in client_addr {};
            socklen_t client_addr_len = sizeof(client_addr);
            const int client_fd =
                accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
            if (client_fd < 0) {
                if (errno == EINTR && g_stop_requested) {
                    break;
                }
                std::cerr << "Accept failed: " << std::strerror(errno) << std::endl;
                continue;
            }

            char filename_buffer[kMaxFilenameSize + 1] = {0};
            const ssize_t received = recv(client_fd, filename_buffer, kMaxFilenameSize, 0);
            if (received <= 0) {
                std::cerr << "Failed to receive filename from "
                          << make_client_address(client_addr) << std::endl;
                close(client_fd);
                continue;
            }

            Request request;
            request.client_socket_fd = client_fd;
            request.filename.assign(filename_buffer, static_cast<std::size_t>(received));
            request.client_address = make_client_address(client_addr);
            request.arrival_timestamp = std::chrono::steady_clock::now();
            request.file_size_estimate = estimate_file_size(request.filename);

            request_queue.push(std::move(request));
        }

        close(server_fd);
        request_queue.shutdown();

        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        std::cout << "Server shutdown complete." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
}
