//Author: Brandon Liu
//File: src/server_logger.h
//Description: Declaration of the ServerLogger class for logging

#ifndef SERVER_LOGGER_H
#define SERVER_LOGGER_H

#include "request_queue.h"

#include <fstream>
#include <mutex>
#include <string>

class ServerLogger {
public:
    explicit ServerLogger(const std::string& path);

    void log_request(const Request& request,
                     SchedulingPolicy policy,
                     int worker_id,
                     const std::string& cache_state,
                     const std::string& status,
                     double response_ms);

private:
    std::ofstream output_;
    std::mutex mutex_;
};

#endif
