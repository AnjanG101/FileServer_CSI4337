#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <deque>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

constexpr std::size_t kMaxFilenameSize = 1024;
constexpr int kDefaultWorkerCount = 4;
constexpr std::size_t kDefaultQueueSize = 20;
constexpr std::size_t kDefaultCacheSize = 1024 * 1024;
constexpr const char* kFileNotFoundMessage = "ERROR: file not found";

volatile std::sig_atomic_t g_stop_requested = 0;

enum class SchedulingPolicy {
    Fifo,
    Sff,
};

struct ServerConfig {
    int port = 0;
    int worker_count = kDefaultWorkerCount;
    std::size_t queue_size = kDefaultQueueSize;
    SchedulingPolicy policy = SchedulingPolicy::Fifo;
    bool cache_enabled = true;
    std::size_t cache_size_bytes = kDefaultCacheSize;
};

struct Request {
    int client_socket_fd = -1;
    std::string filename;
    std::string client_address;
    std::chrono::steady_clock::time_point arrival_timestamp;
    std::size_t file_size_estimate = 0;
    std::size_t sequence_number = 0;
};

std::string policy_to_string(SchedulingPolicy policy);

class RequestQueue {
public:
    RequestQueue(std::size_t max_size, SchedulingPolicy policy)
        : max_size_(max_size), policy_(policy) {}

    void push(Request request) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [&] { return shutdown_ || requests_.size() < max_size_; });
        if (shutdown_) {
            close(request.client_socket_fd);
            return;
        }

        requests_.push_back(std::move(request));
        not_empty_.notify_one();
    }

    std::optional<Request> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [&] { return shutdown_ || !requests_.empty(); });
        if (requests_.empty()) {
            return std::nullopt;
        }

        Request request;
        if (policy_ == SchedulingPolicy::Fifo) {
            request = std::move(requests_.front());
            requests_.pop_front();
        } else {
            auto next_request = requests_.begin();
            for (auto it = std::next(requests_.begin()); it != requests_.end(); ++it) {
                if (it->file_size_estimate < next_request->file_size_estimate ||
                    (it->file_size_estimate == next_request->file_size_estimate &&
                     it->sequence_number < next_request->sequence_number)) {
                    next_request = it;
                }
            }

            request = std::move(*next_request);
            requests_.erase(next_request);
        }

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
    SchedulingPolicy policy_;
    std::deque<Request> requests_;
    bool shutdown_ = false;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

class FileCache {
public:
    struct LookupResult {
        std::vector<char> data;
        bool hit = false;
    };

    FileCache(bool enabled, std::size_t max_bytes) : enabled_(enabled), max_bytes_(max_bytes) {}

    LookupResult get(const std::string& filename) {
        if (!enabled_) {
            return {};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto entry = entries_.find(filename);
        if (entry == entries_.end()) {
            return {};
        }

        touch(entry);
        return {entry->second.data, true};
    }

    void put(const std::string& filename, const std::vector<char>& data) {
        if (!enabled_ || data.size() > max_bytes_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto existing = entries_.find(filename);
        if (existing != entries_.end()) {
            current_bytes_ -= existing->second.size;
            lru_order_.erase(existing->second.lru_position);
            entries_.erase(existing);
        }

        while (!lru_order_.empty() && current_bytes_ + data.size() > max_bytes_) {
            evict_one();
        }

        lru_order_.push_front(filename);
        CacheEntry entry;
        entry.data = data;
        entry.size = data.size();
        entry.lru_position = lru_order_.begin();
        entries_.emplace(filename, std::move(entry));
        current_bytes_ += data.size();
    }

private:
    struct CacheEntry {
        std::vector<char> data;
        std::list<std::string>::iterator lru_position;
        std::size_t size = 0;
    };

    using EntryMap = std::unordered_map<std::string, CacheEntry>;

    void touch(EntryMap::iterator entry) {
        lru_order_.splice(lru_order_.begin(), lru_order_, entry->second.lru_position);
        entry->second.lru_position = lru_order_.begin();
    }

    void evict_one() {
        const std::string& filename = lru_order_.back();
        auto entry = entries_.find(filename);
        if (entry != entries_.end()) {
            current_bytes_ -= entry->second.size;
            entries_.erase(entry);
        }
        lru_order_.pop_back();
    }

    bool enabled_;
    std::size_t max_bytes_;
    std::size_t current_bytes_ = 0;
    std::list<std::string> lru_order_;
    EntryMap entries_;
    std::mutex mutex_;
};

class ServerLogger {
public:
    explicit ServerLogger(const std::string& path) : output_(path, std::ios::app) {
        if (!output_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + path);
        }
    }

    void log_request(const Request& request,
                     SchedulingPolicy policy,
                     int worker_id,
                     const std::string& cache_state,
                     const std::string& status,
                     double response_ms) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm timestamp {};
        localtime_r(&now_time, &timestamp);

        std::ostringstream line;
        line << std::put_time(&timestamp, "%Y-%m-%d %H:%M:%S")
             << " | client=" << request.client_address
             << " | file=" << request.filename
             << " | policy=" << policy_to_string(policy)
             << " | worker=" << worker_id
             << " | cache=" << cache_state
             << " | status=" << status
             << " | response_ms=" << std::fixed << std::setprecision(2) << response_ms;

        std::lock_guard<std::mutex> lock(mutex_);
        output_ << line.str() << '\n';
        output_.flush();
    }

private:
    std::ofstream output_;
    std::mutex mutex_;
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

std::string policy_to_string(SchedulingPolicy policy) {
    return policy == SchedulingPolicy::Fifo ? "fifo" : "sff";
}

std::optional<std::vector<char>> read_file_contents(const std::string& filename) {
    std::ifstream input_file(filename, std::ios::binary);
    if (!input_file.is_open()) {
        return std::nullopt;
    }

    input_file.seekg(0, std::ios::end);
    const std::streamoff file_size = input_file.tellg();
    if (file_size < 0) {
        return std::nullopt;
    }

    input_file.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<std::size_t>(file_size));
    if (!data.empty()) {
        input_file.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (!input_file) {
            return std::nullopt;
        }
    }

    return data;
}

double elapsed_milliseconds(std::chrono::steady_clock::time_point start_time) {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void handle_signal(int) {
    g_stop_requested = 1;
}

ServerConfig parse_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::runtime_error(
            "Usage: ./server <port> [--workers <n>] [--queue <size>] "
            "[--policy fifo|sff] [--cache-size <bytes>] [--cache off]");
    }

    ServerConfig config;
    config.port = std::stoi(argv[1]);

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workers" && i + 1 < argc) {
            config.worker_count = std::stoi(argv[++i]);
        } else if (arg == "--queue" && i + 1 < argc) {
            config.queue_size = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--policy" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "fifo") {
                config.policy = SchedulingPolicy::Fifo;
            } else if (value == "sff") {
                config.policy = SchedulingPolicy::Sff;
            } else {
                throw std::runtime_error("Policy must be 'fifo' or 'sff'");
            }
        } else if (arg == "--cache-size" && i + 1 < argc) {
            config.cache_size_bytes = static_cast<std::size_t>(std::stoull(argv[++i]));
            config.cache_enabled = true;
        } else if (arg == "--cache" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "off") {
                config.cache_enabled = false;
            } else {
                throw std::runtime_error("Cache option only supports '--cache off'");
            }
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
    if (config.cache_enabled && config.cache_size_bytes == 0) {
        throw std::runtime_error("Cache size must be positive when cache is enabled");
    }

    return config;
}

void serve_request(const Request& request,
                   int worker_id,
                   SchedulingPolicy policy,
                   FileCache& file_cache,
                   ServerLogger& logger) {
    std::cout << "Worker " << worker_id << " serving " << request.filename
              << " for " << request.client_address << std::endl;

    std::vector<char> file_data;
    std::string cache_state = "miss";
    const FileCache::LookupResult cache_result = file_cache.get(request.filename);
    if (cache_result.hit) {
        file_data = cache_result.data;
        cache_state = "hit";
    } else {
        std::optional<std::vector<char>> loaded_file = read_file_contents(request.filename);
        if (!loaded_file.has_value()) {
            std::cerr << "File not found: " << request.filename << std::endl;
            const bool sent_error =
                send_all(request.client_socket_fd, kFileNotFoundMessage, std::strlen(kFileNotFoundMessage));
            if (!sent_error) {
                std::cerr << "Failed to send error message" << std::endl;
            }
            logger.log_request(request,
                               policy,
                               worker_id,
                               cache_state,
                               "error",
                               elapsed_milliseconds(request.arrival_timestamp));
            close(request.client_socket_fd);
            return;
        }

        file_data = std::move(*loaded_file);
        file_cache.put(request.filename, file_data);
    }

    if (!file_data.empty() &&
        !send_all(request.client_socket_fd, file_data.data(), file_data.size())) {
        std::cerr << "Failed while sending file contents" << std::endl;
        logger.log_request(request,
                           policy,
                           worker_id,
                           cache_state,
                           "error",
                           elapsed_milliseconds(request.arrival_timestamp));
        close(request.client_socket_fd);
        return;
    }

    logger.log_request(
        request, policy, worker_id, cache_state, "success", elapsed_milliseconds(request.arrival_timestamp));

    if (file_data.empty()) {
        close(request.client_socket_fd);
        return;
    }

    close(request.client_socket_fd);
}

void worker_loop(int worker_id,
                 SchedulingPolicy policy,
                 RequestQueue& request_queue,
                 FileCache& file_cache,
                 ServerLogger& logger) {
    while (true) {
        std::optional<Request> request = request_queue.pop();
        if (!request.has_value()) {
            return;
        }
        serve_request(*request, worker_id, policy, file_cache, logger);
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

        RequestQueue request_queue(config.queue_size, config.policy);
        FileCache file_cache(config.cache_enabled, config.cache_size_bytes);
        ServerLogger logger("server.log");
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(config.worker_count));
        for (int worker_id = 0; worker_id < config.worker_count; ++worker_id) {
            workers.emplace_back(worker_loop,
                                 worker_id,
                                 config.policy,
                                 std::ref(request_queue),
                                 std::ref(file_cache),
                                 std::ref(logger));
        }

        std::cout << "Server listening on port " << config.port
                  << " with workers=" << config.worker_count
                  << " queue=" << config.queue_size
                  << " policy=" << policy_to_string(config.policy)
                  << " cache="
                  << (config.cache_enabled ? std::to_string(config.cache_size_bytes) + " bytes"
                                           : std::string("off"))
                  << std::endl;

        std::size_t request_sequence = 0;
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
            request.sequence_number = request_sequence++;

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
