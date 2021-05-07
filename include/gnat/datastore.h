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
    struct ObserverEntry {
      uint32_t client_id = 0;
      std::function<bool(const KeyType&, const DataStoreEntry&)> handler;
    };

    // Encode a string to this key type, this needs to be specialized below.
    static KeyType EncodeKey(const char* decoded, size_t bytes);

    // Decode a string from this key type, this needs to be specialized below.
    static void DecodeKey(const KeyType& key, char* encoded, uint16_t* bytes);

    static std::function<bool(const KeyType& key)> FullKeyMatcher(const KeyType& key);
    static std::function<bool(const KeyType& key)> PrefixKeyMatcher(const KeyType& key);

    using Key = KeyType;

    void Set(const KeyType& key, DataStoreEntry entry) {
        entries_.erase(key);
        entries_.emplace(key, std::move(entry));
        NotifyObservers(key);
    }

    const DataStoreEntry& Get(const KeyType& key) {
        return entries_.at(key);
    }

    void RemoveObserversForClient(uint32_t client_id) {
      for (auto iter = observers_.begin(); iter != observers_.end(); iter++) {
          auto& observer = *iter;
          if (observer.client_id == client_id) {
                if (iter == observers_.begin()) {
                  observers_.pop_front();
                  iter = observers_.begin();
                } else {
                  iter--;
                  observers_.erase(std::next(iter));
                }
          }
      }
    }

    void AddObserver(ObserverEntry observer) {
        observers_.emplace_back(std::move(observer));

        auto& handler = observers_.back().handler;
        // Send observer all existing data so it can filter by matching topics.
        for (const auto& [key, entry] : entries_) {
          handler(key, entry);
        }
    }

private:
    void NotifyObservers(const KeyType& key) {
        const auto& value = entries_[key];
        for (auto iter = observers_.begin(); iter != observers_.end(); iter++) {
            auto& observer = *iter;
            if (!observer.handler(key, value)) {
                if (iter == observers_.begin()) {
                  observers_.pop_front();
                  iter = observers_.begin();
                } else {
                  iter--;
                  observers_.erase(std::next(iter));
                }
            }
        }
    }

   std::unordered_map<KeyType, DataStoreEntry> entries_;
   std::list<ObserverEntry> observers_;
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
std::function<bool(const uint64_t&)> DataStore<uint64_t>::FullKeyMatcher(
    const uint64_t& target_key) {
  return [target_key](const uint64_t& other_key) {
    return target_key == other_key;
  };
}

template<>
std::function<bool(const uint64_t&)> DataStore<uint64_t>::PrefixKeyMatcher(
    const uint64_t& target_key) {
  return [target_key](const uint64_t& other_key) {
    // The parts of the target key that are not '0' are the prefix, after anding if
    // the other key had the prefix we should be left with the target key.
    return (target_key & other_key) == target_key;
  };
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

template<>
std::function<bool(const std::string&)> DataStore<std::string>::FullKeyMatcher(
    const std::string& target_key) {
  return [target_key](const std::string& other_key) {
    return target_key == other_key;
  };
}

template<>
std::function<bool(const std::string&)> DataStore<std::string>::PrefixKeyMatcher(
    const std::string& target_key) {
  return [target_key](const std::string& other_key) {
    return std::equal(target_key.begin(),
        target_key.begin() + std::min(target_key.size(), other_key.size()),
        other_key.begin());
  };
}


} // namespace gnat
