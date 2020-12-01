
#include "datastore.h"

#include <gtest/gtest.h>
#include "key.h"

namespace {
constexpr auto kKey = gnat::key::Encode("TEST");

using gnat::DataStore;

DataStore::Entry ToEntry(const char* value) {
    DataStore::Entry out;
    out.length = strlen(value);
    out.data = std::make_unique<uint8_t[]>(out.length);
    memcpy(out.data.get(), value, out.length);
    return out;
}

}

TEST(DataStoreTest, StoreRetreive) {
    DataStore store;
    const std::string value("I'M A TEST!");
    store.Set(kKey, ToEntry(value.c_str()));

    const auto& entry = store.Get(kKey);
    ASSERT_TRUE(value == (char*)entry.data.get());
}

TEST(DataStoreTest, Notify) {
    DataStore store;

    uint64_t notified_key = 0;
    std::vector<char> notified_data;
    store.AddObserver([&notified_key, &notified_data](
                uint64_t key, const DataStore::Entry& entry) {
        notified_key = key;
        notified_data.resize(entry.length);
        memcpy(notified_data.data(), entry.data.get(), entry.length);
        return true;
    });


    const std::string value("I'M A TEST!");
    store.Set(kKey, ToEntry(value.c_str()));

    ASSERT_EQ(notified_key, kKey);
    ASSERT_NE(0, notified_data.size());

    const std::string notified_string(notified_data.data(), notified_data.size());
    ASSERT_TRUE(value == notified_string)
        << "notified_data: " << notified_string << "\n";
}

TEST(DataStoreTest, RemoveFailedObserver) {
    DataStore store;

    int notified_count = 0;
    store.AddObserver(
            [&notified_count](uint64_t, const DataStore::Entry&) {
                notified_count++;
                return false;
            });
    const std::string value("I'M A TEST!");
    store.Set(kKey, ToEntry(value.c_str()));
    store.Set(kKey, ToEntry("TEST2"));

    ASSERT_EQ(1, notified_count);
}
