#pragma once

#include "key.h"

#include <unordered_map>
#include <list>
#include <memory>
#include <functional>
#include <string>

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
    // Encode a string to this key type, this needs to be specialized below.
    static KeyType EncodeKey(const char* decoded, size_t bytes);

    // Dencode a string from this key type, this needs to be specialized below.
    static void DecodeKey(const KeyType& key, char* encoded, uint16_t* bytes);

    using Key = KeyType;

    void Set(const KeyType& key, DataStoreEntry entry) {
        entries_.erase(key);
        entries_.emplace(key, std::move(entry));
        NotifyObservers(key);
    }

    const DataStoreEntry& Get(const KeyType& key) {
        return entries_.at(key);
    }

    void AddObserver(std::function<bool(const KeyType&, const DataStoreEntry&)> observer) {
        observers_.emplace_back(std::move(observer));
    }

private:
    void NotifyObservers(const KeyType& key) {
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
   std::list<std::function<bool(const KeyType&, const DataStoreEntry&)>> observers_;
};

template<>
uint64_t DataStore<uint64_t>::EncodeKey(const char* decoded, size_t bytes) {
  return key::EncodeString(decoded, bytes);
}

template<>
void DataStore<uint64_t>::DecodeKey(const uint64_t& key, char* decoded, uint16_t* bytes) {
  key::DecodeString(key, decoded, bytes);
}

template<>
std::string DataStore<std::string>::EncodeKey(const char* decoded, size_t bytes) {
  return {decoded, bytes};
}

template<>
void DataStore<std::string>::DecodeKey(const std::string& key, char* decoded, uint16_t* bytes) {
  // DANGER DANGER DNAGER!!! come back and assert the length of the thing we are
  // putting this into!
  memcpy(decoded, key.c_str(), key.length());
  *bytes = key.length();
}

} // namespace gnat
