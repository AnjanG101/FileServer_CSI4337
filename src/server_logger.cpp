//Author: Brandon Liu
//File: src/server_logger.cpp
//Description: Implementation of the ServerLogger class for logging request handling details in a multi-threaded

#include "server_logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

ServerLogger::ServerLogger(const std::string& path) : output_(path, std::ios::app) {
    if (!output_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + path);
    }
}

void ServerLogger::log_request(const Request& request,
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
