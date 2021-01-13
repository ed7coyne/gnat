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
    std::function<bool(char*, size_t)> Read;
    std::function<bool(char*, size_t)> Write;
    bool WritePartial(char* buffer, size_t size) {
      return Write(buffer, size);
    }

    FakeConnection CreateHeapCopy() {
        return *this;
    }
};

struct Buffer {
  constexpr static const size_t kSize = 1024;
  uint8_t buffer[kSize] = {0};
  size_t position = 0;

  size_t remaining() {
    return kSize - position;
  }
};

struct BufferConnection {
    BufferConnection(uint8_t* buffer, size_t size)
        : out_buffer_(buffer), out_size_(size), in_buffer_(new Buffer()) {}

    BufferConnection(uint8_t* buffer, size_t size, std::shared_ptr<Buffer> in_buffer)
        : out_buffer_(buffer), out_size_(size), in_buffer_(in_buffer) {}

    bool Read(uint8_t* to, size_t to_size) {
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

    bool Write(uint8_t* from, size_t from_size) {
        auto to_copy = std::min(from_size, in_buffer_->remaining());
        if (to_copy == 0) {
            LOG("Read past end!\n");
            return false;
        }
        LOG("Writing: ");
        for (size_t i = 0; i  < to_copy; i++) {
            LOG("%02X ", *(from + i));
        }
        LOG("\n");

        memcpy(in_buffer_->buffer + in_buffer_->position, from, to_copy);
        in_buffer_->position += to_copy;
        return true;
    }

    bool WritePartial(uint8_t* buffer, size_t size) {
      return Write(buffer, size);
    }

    bool Drain(size_t bytes) {
      constexpr size_t kBuffSize = 1024;
      static uint8_t buffer[kBuffSize];
      while (bytes > 0) {
        const auto to_read = std::min(kBuffSize, bytes);
        if (!Read(buffer, to_read)) return false;
        bytes -= to_read;
      }
      return true;
    }

    void Close() {}

    BufferConnection CreateHeapCopy() {
        return BufferConnection(out_buffer_ + out_position_, out_size_ - out_position_,
                                in_buffer_);
    }

    gnat::ConnectionType connection_type() { return type_; }
    void set_connection_type(gnat::ConnectionType type) { type_ = type; }

    uint8_t* out_buffer_;
    const size_t out_size_;
    size_t out_position_ = 0;

    std::shared_ptr<Buffer> in_buffer_;

    gnat::ConnectionType type_ = gnat::ConnectionType::UNKNOWN;
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
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(packet.type(), gnat::PacketType::CONNECT);
    ASSERT_EQ(packet.bytes_remaining(), 31);

    const auto header_opt = gnat::proto3::Connect::ReadFrom(&packet);
    ASSERT_TRUE(header_opt.has_value());

    auto header = *header_opt;
    EXPECT_EQ(6, header.protocol_name.length);
    EXPECT_EQ(std::string("MQIsdp"),
              std::string(header.protocol_name.data, header.protocol_name.length));
    EXPECT_EQ(3, header.protocol_level);
}

TEST(ServerTest, ConnectPacketHandling) {
    constexpr static uint8_t kData[] = {
        0x10, 0x1f, 0x00, 0x06, 0x4d, 0x51, 0x49, 0x73,
        0x64, 0x70, 0x03, 0x02, 0x00, 0x3c, 0x00, 0x11,
        0x6d, 0x6f, 0x73, 0x71, 0x70, 0x75, 0x62, 0x7c,
        0x31, 0x35, 0x36, 0x37, 0x35, 0x2d, 0x65, 0x37,
        0x63};

    FakeClock clock;
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&packet));

    EXPECT_EQ(((BufferConnection*)(packet.connection()))->type_, gnat::ConnectionType::MQTT_31);
}

TEST(ServerTest, PublishPacket) {
    constexpr static uint8_t kData[] = {
      0x30, 0xC, 0x0, 0x6, 0x74, 0x2F, 0x74, 0x65, 0x73,
      0x74, 0x74, 0x65, 0x73, 0x74
    };

    FakeClock clock;
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(packet.type(), gnat::PacketType::PUBLISH);
    ASSERT_EQ(packet.bytes_remaining(), 12);

    const auto header_opt = gnat::proto3::Publish::ReadFrom(&packet, packet.type_flags());
    ASSERT_TRUE(header_opt.has_value());

    auto& header = *header_opt;

    EXPECT_EQ(std::string(header.topic.data, header.topic.length), "t/test");
    EXPECT_EQ(4, header.payload_bytes);
}

TEST(ServerTest, PublishPacketHandling) {
    constexpr static uint8_t kData[] = {
      0x30, 0xC, 0x0, 0x6, 0x74, 0x2F, 0x74, 0x65, 0x73,
      0x74, 0x74, 0x65, 0x73, 0x74
    };

    FakeClock clock;
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&packet));
}

TEST(ServerTest, PublishPacketStringKeyHandling) {
    constexpr static uint8_t kData[] = {
      0x30, 0xC, 0x0, 0x6, 0x74, 0x2F, 0x74, 0x65, 0x73,
      0x74, 0x74, 0x65, 0x73, 0x74
    };

    FakeClock clock;
    gnat::DataStore<std::string> data;
    gnat::Server<gnat::DataStore<std::string>, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&packet));
}

TEST(ServerTest, SubscribePacket) {
    constexpr static uint8_t kData[] = {
      0b10000010, 11, 0x0, 0x1, 0x0, 0x6,
      't', '/', 't', 'e', 's', 't', 0,
    };

    FakeClock clock;
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    BufferConnection connection((uint8_t*)kData, sizeof(kData));

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(packet.type(), gnat::PacketType::SUBSCRIBE);
    ASSERT_EQ(packet.bytes_remaining(), 11);

    int topics = 0;
    std::string captured_topic;
    auto topic_callback = [&](auto* topic) {
      topics++;
      captured_topic = std::string(topic->data, topic->length);
      return true;
    };

    const auto header_opt = gnat::proto3::Subscribe::ReadFrom(&packet, topic_callback);
    ASSERT_TRUE(header_opt.has_value());

    EXPECT_EQ(header_opt->packet_id, 1);
    EXPECT_EQ(topics, 1);
    EXPECT_EQ(captured_topic, "t/test");
}

TEST(ServerTest, SubscribePacketHandling) {
    constexpr static uint8_t kData[] = {
      0b10000010, 11, 0x0, 0x1, 0x0, 0x6,
      't', '/', 't', 'e', 's', 't', 0,
    };

    FakeClock clock;
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    std::shared_ptr<Buffer> data_written(new Buffer);
    BufferConnection connection((uint8_t*)kData, sizeof(kData), data_written);

    auto packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&packet));
    // Ensure we got a subscribtion ack.
    ASSERT_GE(5, data_written->position);
    ASSERT_EQ(0b10010000, data_written->buffer[0]);

    // And that it is for our packet id
    ASSERT_EQ(1, data_written->buffer[3]);

    // And that it is not an error
    ASSERT_EQ(0, data_written->buffer[4]);
}

TEST(ServerTest, SubscribePublishHandling) {
    FakeClock clock;
    gnat::DataStore<uint64_t> data;
    gnat::Server<gnat::DataStore<uint64_t>, FakeClock> server(&data, &clock);

    constexpr static uint8_t kSubscribeData[] = {
      0b10000010, 11, 0x0, 0x1, 0x0, 0x6,
      't', '/', 't', 'e', 's', 't', 0,
    };

    std::shared_ptr<Buffer> data_written(new Buffer);
    BufferConnection subscribe_connection((uint8_t*)kSubscribeData, sizeof(kSubscribeData),
                                          data_written);
    auto subscribe_packet =
        *gnat::Packet<BufferConnection>::ReadNext(std::move(subscribe_connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&subscribe_packet));

    // Ensure we got a subscribtion ack.
    ASSERT_GE(5, data_written->position);
    ASSERT_EQ(0b10010000, data_written->buffer[0]);
    const auto ack_length = data_written->buffer[1] + 2;

    // No other data in our buffer before we publish something.
    ASSERT_EQ(ack_length, data_written->position);

    constexpr static uint8_t kPublishData[] = {
      0x30, 0xC, 0x0, 0x6, 0x74, 0x2F, 0x74, 0x65, 0x73,
      0x74, 0x74, 0x65, 0x73, 0x74
    };

    BufferConnection publish_connection((uint8_t*)kPublishData, sizeof(kPublishData));
    auto publish_packet = *gnat::Packet<BufferConnection>::ReadNext(std::move(publish_connection));
    ASSERT_EQ(gnat::Status::Ok(), server.HandleMessage(&publish_packet));

    // Ensure we got a publish.
    ASSERT_LT(ack_length, data_written->position);
    ASSERT_EQ(0b0011, data_written->buffer[ack_length] >> 4);
    ASSERT_EQ(kPublishData[sizeof(kPublishData) - 1], data_written->buffer[data_written->position-1]);
}

