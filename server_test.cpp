#include "server.h"

#include <gtest/gtest.h>
#include "key.h"
#include "datastore.h"

namespace {

class FakeClock {
public:
    uint32_t timestamp() {
        return time;
    }
    uint32_t time = 0;
};

struct FakeConnection {
    std::function<bool(char*, size_t)> ReadAll;
    std::function<bool(char*, size_t)> WriteAll;

    FakeConnection CreateHeapCopy() {
        return *this; 
    }
};

struct BufferConnection {
    BufferConnection(uint8_t* buffer, size_t size) : out_buffer_(buffer), out_size_(size) {}

    bool ReadAll(uint8_t* to, size_t to_size) {
        LOG("Reading: %u\n", to_size);
        auto to_copy = std::min(to_size, out_size_ - out_position_);
        if (to_copy == 0) {
            LOG("Read past end!\n");
            return false;
        }
        memcpy(to, out_buffer_ + out_position_, to_copy);
        out_position_ += to_copy;
        return true;
    }

    bool WriteAll(uint8_t* from, size_t from_size) {
        auto to_copy = std::min(from_size, kInBufferSize - in_position_);
        if (to_copy == 0) {
            LOG("Read past end!\n");
            return false;
        }
        LOG("Writing: ");
        for (size_t i = 0; i  < to_copy; i++) {
            LOG("%02X ", *(from + i));
        }
        LOG("\n");

        memcpy(in_buffer_ + in_position_, from, to_copy);
        in_position_ += to_copy;
        return true;
    }

    bool Drain(size_t bytes) {
      constexpr size_t kBuffSize = 1024;
      static uint8_t buffer[kBuffSize];
      while (bytes > 0) {
        const auto to_read = std::min(kBuffSize, bytes);
        if (!ReadAll(buffer, to_read)) return false;
        bytes -= to_read;
      }
      return true;
    }
    
    BufferConnection CreateHeapCopy() {
        return BufferConnection(out_buffer_ + out_position_, out_size_ - out_position_); 
    }
    
    uint8_t* out_buffer_;
    const size_t out_size_;
    size_t out_position_ = 0;

    constexpr static const size_t kInBufferSize = 1024;
    uint8_t in_buffer_[kInBufferSize];
    size_t in_position_ = 0;
};

}  // namespace


TEST(ServerTest, ConnectPacket) {
    constexpr static uint8_t kData[] = {
        0x10, 0x1f, 0x00, 0x06, 0x4d, 0x51, 0x49, 0x73,
        0x64, 0x70, 0x03, 0x02, 0x00, 0x3c, 0x00, 0x11,
        0x6d, 0x6f, 0x73, 0x71, 0x70, 0x75, 0x62, 0x7c,
        0x31, 0x35, 0x36, 0x37, 0x35, 0x2d, 0x65, 0x37,
        0x63};

    FakeClock clock;
    gnat::DataStore data;
    gnat::Server<BufferConnection, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(packet.type(), gnat::PacketType::CONNECT); 
    ASSERT_EQ(packet.remaining_bytes(), 31); 

    auto header_opt = packet.ReadConnectHeader();
    ASSERT_TRUE(header_opt.has_value());

    auto header = *header_opt;
    EXPECT_EQ(6, header.protocol_name_length);
    EXPECT_EQ(std::string("MQIsdp"),
              std::string(header.protocol_name, header.protocol_name_length));
    EXPECT_EQ(3, header.protocol_version);
}

TEST(ServerTest, ConnectPacketHandling) {
    constexpr static uint8_t kData[] = {
        0x10, 0x1f, 0x00, 0x06, 0x4d, 0x51, 0x49, 0x73,
        0x64, 0x70, 0x03, 0x02, 0x00, 0x3c, 0x00, 0x11,
        0x6d, 0x6f, 0x73, 0x71, 0x70, 0x75, 0x62, 0x7c,
        0x31, 0x35, 0x36, 0x37, 0x35, 0x2d, 0x65, 0x37,
        0x63};

    FakeClock clock;
    gnat::DataStore data;
    gnat::Server<BufferConnection, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&packet));
}

TEST(ServerTest, PublishPacket) {
    constexpr static uint8_t kData[] = {
      0x30, 0xC, 0x0, 0x6, 0x74, 0x2F, 0x74, 0x65, 0x73, 
      0x74, 0x74, 0x65, 0x73, 0x74
    };

    FakeClock clock;
    gnat::DataStore data;
    gnat::Server<BufferConnection, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(packet.type(), gnat::PacketType::PUBLISH); 
    ASSERT_EQ(packet.remaining_bytes(), 12); 

    auto header_opt = packet.ReadPublish();
    ASSERT_TRUE(header_opt.has_value());

    auto& header = *header_opt;

    EXPECT_EQ(std::string(header.topic, header.topic_length), "t/test");
    EXPECT_EQ(4, header.payload_size);
}

TEST(ServerTest, PublishPacketHandling) {
    constexpr static uint8_t kData[] = {
      0x30, 0xC, 0x0, 0x6, 0x74, 0x2F, 0x74, 0x65, 0x73, 
      0x74, 0x74, 0x65, 0x73, 0x74
    };

    FakeClock clock;
    gnat::DataStore data;
    gnat::Server<BufferConnection, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&packet));
}


/*
TEST(ServerTest, IntakeExhaust) {
    FakeClock clock;
    gnat::DataStore data;
    gnat::Server<FakeConnection, FakeClock> server(&data, &clock);

    std::vector<char> written;
    FakeConnection write_conn;
    write_conn.WriteAll = [&written](char* buffer, size_t size) {
        const auto old_size = written.size();
        written.resize(old_size + size);
        memcpy(written.data() + old_size, buffer, size); 
        return true;
    };

    const std::string value("I'M A TEST!");
    {
        gnat::SessionInitialization command;
        command.type = gnat_chamber_SessionInitialization_SessionType_EXHAUST;
        command.has_key = true;
        command.key = kKey;

        ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(command, write_conn));
    }
    {
        gnat::SessionInitialization command;
        command.type = gnat_chamber_SessionInitialization_SessionType_INTAKE;
        command.has_key = true;
        command.key = kKey;
        command.has_data_size = true;
        command.data_size = value.length();

        FakeConnection read_conn;
        ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(command, read_conn));
    }
    
    ASSERT_TRUE(value == std::string(written.data(), written.size()));
}
*/

