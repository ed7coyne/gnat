#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>

namespace gnat {

struct DataStoreEntry {
    std::unique_ptr<uint8_t[]> data;
    uint32_t length = 0;
    uint32_t timestamp = 0;

    DataStoreEntry() = default;
    DataStoreEntry(uint32_t timestamp) : timestamp(timestamp) {}

    DataStoreEntry(DataStoreEntry&& other) = default;
    DataStoreEntry(const DataStoreEntry&) = delete;
    DataStoreEntry& operator=(const DataStoreEntry&) = delete;
};

template<typename KeyType>
class DataStore {
public:
    void Set(KeyType key, DataStoreEntry entry) {
        entries_.erase(key);
        entries_.emplace(key, std::move(entry));
        NotifyObservers(key);
    }

    const DataStoreEntry& Get(KeyType key) {
        return entries_.at(key);
    }

    void AddObserver(std::function<bool(KeyType, const DataStoreEntry&)> observer) {
        observers_.emplace_back(std::move(observer));
    }

private:
    void NotifyObservers(KeyType key) {
        const auto& value = entries_[key];
        for (auto iter = observers_.begin(); iter != observers_.end(); iter++) {
            auto& observer = *iter;
            if (!observer(key, value)) {
                iter--;
                observers_.erase(std::next(iter));
            }
        }
    }

   std::unordered_map<KeyType, DataStoreEntry> entries_;
   std::list<std::function<bool(KeyType, const DataStoreEntry&)>> observers_;
};

} // namespace gnat
