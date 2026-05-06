//Author: Brandon Liu
//File: src/request_queue.h
//Description: Declaration of the RequestQueue class for managing incoming file requests in a multi-threaded

#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

enum class SchedulingPolicy {
    Fifo,
    Sff,
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
    RequestQueue(std::size_t max_size, SchedulingPolicy policy);

    void push(Request request);
    std::optional<Request> pop();
    void shutdown();

private:
    std::size_t max_size_;
    SchedulingPolicy policy_;
    std::deque<Request> requests_;
    bool shutdown_ = false;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

#endif
