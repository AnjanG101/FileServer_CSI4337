//Author: Brandon Liu
//File: src/file_cache.h
//Description: Declaration of the FileCache class for caching

#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class FileCache {
public:
    struct LookupResult {
        std::vector<char> data;
        bool hit = false;
    };

    FileCache(bool enabled, std::size_t max_bytes);

    LookupResult get(const std::string& filename);
    void put(const std::string& filename, const std::vector<char>& data);

private:
    struct CacheEntry {
        std::vector<char> data;
        std::list<std::string>::iterator lru_position;
        std::size_t size = 0;
    };

    using EntryMap = std::unordered_map<std::string, CacheEntry>;

    void touch(EntryMap::iterator entry);
    void evict_one();

    bool enabled_;
    std::size_t max_bytes_;
    std::size_t current_bytes_ = 0;
    std::list<std::string> lru_order_;
    EntryMap entries_;
    std::mutex mutex_;
};

#endif
