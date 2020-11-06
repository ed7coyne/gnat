#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>

namespace gnat {

class DataStore {
public:
    struct Entry {
        std::unique_ptr<uint8_t[]> data;
        uint32_t length = 0;
        uint32_t timestamp = 0;

        Entry() = default;
        Entry(uint32_t timestamp) : timestamp(timestamp) {}

        Entry(Entry&& other) = default;
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;      
    };

    void Set(uint64_t key, Entry entry) {
        entries_.erase(key);
        entries_.emplace(key, std::move(entry));
        NotifyObservers(key);
    }

    const Entry& Get(uint64_t key) {
    	return entries_.at(key);
    }

    void AddObserver(std::function<bool(uint64_t, const Entry&)> observer) {
        observers_.emplace_back(std::move(observer));
    }

private:
    void NotifyObservers(uint64_t key) {
        const auto& value = entries_[key];
        for (auto iter = observers_.begin(); iter != observers_.end(); iter++) {
            auto& observer = *iter;
            if (!observer(key, value)) {
                iter--;
                observers_.erase(std::next(iter));
            }
        }
    }

   std::unordered_map<uint64_t, Entry> entries_;
   std::list<std::function<bool(uint64_t, const Entry&)>> observers_;
};

} // namespace gnat
