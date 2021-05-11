
#include "datastore.h"

#include <gtest/gtest.h>
#include "key.h"

namespace {
constexpr auto kKey = "TEST";
constexpr auto kKeyUint = gnat::key::Encode("TEST");

gnat::DataStoreEntry ToEntry(const char* value) {
    gnat::DataStoreEntry out;
    out.length = strlen(value);
    out.data = std::make_unique<uint8_t[]>(out.length);
    memcpy(out.data.get(), value, out.length);
    return out;
}

}

TEST(DataStoreTest, StoreRetreive) {
    gnat::DataStore<uint64_t> store;
    const std::string value("I'M A TEST!");
    store.Set(kKeyUint, ToEntry(value.c_str()));

    const auto& entry = store.Get(kKeyUint);
    ASSERT_TRUE(value == (char*)entry.data.get());
}

TEST(DataStoreTest, StoreRetreiveString) {
    gnat::DataStore<std::string> store;
    const std::string value("I'M A TEST!");
    store.Set(kKey, ToEntry(value.c_str()));

    const auto& entry = store.Get(kKey);
    ASSERT_TRUE(value == (char*)entry.data.get());
}


TEST(DataStoreTest, Notify) {
    gnat::DataStore<uint64_t> store;

    uint64_t notified_key = 0;
    std::vector<char> notified_data;
    store.AddObserver({0, [&notified_key, &notified_data](
                uint64_t key, const gnat::DataStoreEntry& entry) {
        notified_key = key;
        notified_data.resize(entry.length);
        memcpy(notified_data.data(), entry.data.get(), entry.length);
        return true;
    }});

    const std::string value("I'M A TEST!");
    store.Set(kKeyUint, ToEntry(value.c_str()));

    ASSERT_EQ(notified_key, kKeyUint);
    ASSERT_NE(0, notified_data.size());

    const std::string notified_string(notified_data.data(), notified_data.size());
    ASSERT_TRUE(value == notified_string)
        << "notified_data: " << notified_string << "\n";
}

TEST(DataStoreTest, NotifyString) {
    gnat::DataStore<std::string> store;

    std::string notified_key;
    std::vector<char> notified_data;
    store.AddObserver({0, [&notified_key, &notified_data](
                const std::string& key, const gnat::DataStoreEntry& entry) {
        notified_key = key;
        notified_data.resize(entry.length);
        memcpy(notified_data.data(), entry.data.get(), entry.length);
        return true;
    }});

    const std::string value("I'M A TEST!");
    store.Set(kKey, ToEntry(value.c_str()));

    ASSERT_EQ(notified_key, kKey);
    ASSERT_NE(0, notified_data.size());

    const std::string notified_string(notified_data.data(), notified_data.size());
    ASSERT_TRUE(value == notified_string)
        << "notified_data: " << notified_string << "\n";
}
