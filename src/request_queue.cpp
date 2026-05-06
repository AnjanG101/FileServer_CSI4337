//Author: Brandon Liu
//File: src/request_queue.cpp
//Description: Implementation of the RequestQueue class for managing incoming file requests in a multi-threaded server environment.

#include "request_queue.h"

#include <utility>

#include <unistd.h>

std::string policy_to_string(SchedulingPolicy policy) {
    return policy == SchedulingPolicy::Fifo ? "fifo" : "sff";
}

RequestQueue::RequestQueue(std::size_t max_size, SchedulingPolicy policy)
    : max_size_(max_size), policy_(policy) {}

void RequestQueue::push(Request request) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [&] { return shutdown_ || requests_.size() < max_size_; });
    if (shutdown_) {
        close(request.client_socket_fd);
        return;
    }

    requests_.push_back(std::move(request));
    not_empty_.notify_one();
}

std::optional<Request> RequestQueue::pop() {
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

void RequestQueue::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
}
