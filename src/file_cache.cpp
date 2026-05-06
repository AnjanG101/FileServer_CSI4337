//Author: Brandon Liu
//File: src/file_cache.cpp
//Description: Implementation of the FileCache class for caching file contents in memory to improve server performance

#include "file_cache.h"

#include <utility>

FileCache::FileCache(bool enabled, std::size_t max_bytes) : enabled_(enabled), max_bytes_(max_bytes) {}

FileCache::LookupResult FileCache::get(const std::string& filename) {
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

void FileCache::put(const std::string& filename, const std::vector<char>& data) {
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

void FileCache::touch(EntryMap::iterator entry) {
    lru_order_.splice(lru_order_.begin(), lru_order_, entry->second.lru_position);
    entry->second.lru_position = lru_order_.begin();
}

void FileCache::evict_one() {
    const std::string& filename = lru_order_.back();
    auto entry = entries_.find(filename);
    if (entry != entries_.end()) {
        current_bytes_ -= entry->second.size;
        entries_.erase(entry);
    }
    lru_order_.pop_back();
}
