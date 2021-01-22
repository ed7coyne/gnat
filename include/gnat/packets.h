#pragma once

#include <assert.h>
//#include <optional>

#include "optional.hpp"
#include "log.h"
#include "key.h"

namespace std {
  using nonstd::optional;
}

namespace gnat {

// These are laid out in the MQTT spec.
enum class PacketType {
    RESERVED,
    CONNECT,
    CONNACK,
    PUBLISH,
    PUBACK,
    PUBREC,
    PUBREL,
    PUBCOMP,
    SUBSCRIBE,
    SUBACK,
    UNSUBSCRIBE,
    UNSUBACK,
    PINGREQ,
    PINGRESP,
    DISCONNECT,
    AUTH
};


enum FieldType {
  TYPE_VARIABLE,
  TYPE_STRING,
  TYPE_BYTES,
};

template<uint16_t byte_count_t, uint8_t type_t = TYPE_BYTES>
struct PacketField {
    static constexpr uint16_t byte_count = byte_count_t;
    static constexpr uint8_t type = type_t;
};

template<size_t Size>
struct StringBuffer {
    static constexpr size_t kSize = Size;
    char data[Size] = {0};
    uint16_t length = 0;
};

namespace {

template<typename T, typename Client>
static bool ReadVariableByteInteger(T* out, Client* client) {
    int multiplier = 1;
    *out = 0;
    while (true) {
      uint8_t next_byte = 0;
      if (!client->Read(&next_byte, 1)) {
          return false;
      }

      *out += (next_byte & 127) * multiplier;

      multiplier *= 128;

      if (!(next_byte & 128))
        return true;
    }
}

template<typename T, typename Client>
static bool ReadString(T* out, Client* client) {
    uint8_t length_raw[2];
    if (!client->Read(length_raw, 2)) return false;

    out->length = (length_raw[0] << 8) | length_raw[1];
    if (out->length > T::kSize) {
        LOG("String too long for buffer!\n");
        return false;
    }

   return client->Read((uint8_t*)out->data, out->length);
}

//TODO: In some glorious world where we could run C++17 this would be a if constexpr
// block and way cleaner.
template<typename Field, uint8_t type>
struct TypedReadField;

template<typename Field>
struct TypedReadField<Field, TYPE_BYTES> {
template<typename T, typename Client>
static bool Read(Client* client, T* out) {
  static_assert(sizeof(*out) >= Field::byte_count, "");
  bool result = true;
  // Wire format is big endian but all target arch are little.
  // For noop loops the compiler will remove all the garbage.
  for (int i = Field::byte_count - 1; i >= 0 && result; i--) {
    client->Read((uint8_t*)out + i, 1);
  }
  return result;
}
};

template<typename Field>
struct TypedReadField<Field, TYPE_STRING> {
template<typename T, typename Client>
static bool Read(Client* client, T* out) {
  return ReadString(out, client);
}
};

template<typename Field>
struct TypedReadField<Field, TYPE_VARIABLE> {
template<typename T, typename Client>
static bool Read(Client* client, T* out) {
  return ReadVariableByteInteger(out, client);
}
};

template<typename Field, typename T, typename Client>
static bool ReadField(Client* client, T* out) {
  return TypedReadField<Field, Field::type>::Read(client, out);
}

}  // namespace

// Header common to all MQTT packets.
struct FixedHeader {
    using Control = PacketField<1>;
    using RemainingSize = PacketField<0, TYPE_VARIABLE>;

    template<typename Client>
    static std::optional<FixedHeader> ReadFrom(Client* client) {
        FixedHeader out;
        if (!ReadField<Control>(client, &out.control)) {
            DEBUG_LOG("Failed to read control.\n");
            return {};
        }

        if (!ReadField<RemainingSize>(client, &out.remaining_size)) {
            DEBUG_LOG("Failed to read size.\n");
            return {};
        }
        return out;
    }

    uint8_t control = 0;
    uint32_t remaining_size = 0;
};

// Packets for MQTT <= 3.1.1
// Things changed dramatically for MQTT 5.
namespace proto3 {

struct Connect {
  using Name = PacketField<0, TYPE_STRING>;
  using ProtocolLevel = PacketField<1>;
  using ConnectFlags = PacketField<1>;
  using KeepAlive = PacketField<2>;

  template<typename Client>
  static std::optional<Connect> ReadFrom(Client* client) {
    Connect out;
    if (!ReadField<Name>(client, &out.protocol_name)) {
      DEBUG_LOG("Failed to read protocol name.\n");
      return {};
    }

    if (!ReadField<ProtocolLevel>(client, &out.protocol_level)) {
      DEBUG_LOG("Failed to read protocol level.\n");
      return {};
    }

    if (!ReadField<ConnectFlags>(client, &out.flags)) {
      DEBUG_LOG("Failed to read flags.\n");
      return {};
    }

    if (!ReadField<KeepAlive>(client, &out.keep_alive)) {
      DEBUG_LOG("Failed to read flags.\n");
      return {};
    }

    return out;
  }

  template<typename Client>
  bool SendOn(Client* client) const {
    static uint8_t buffer[128] = {0};
    uint8_t current_byte = 0;
    buffer[current_byte++] = (((uint8_t)PacketType::CONNECT << 4) & 0xF0);

    // We will come back to set the length last, it will be one byte though.
    uint8_t* remaining_length = &buffer[current_byte++];

    // Write protocol_name.
    buffer[current_byte++] = 0;
    buffer[current_byte++] = protocol_name.length;
    memcpy(buffer + current_byte, protocol_name.data, protocol_name.length);
    current_byte += protocol_name.length;

    buffer[current_byte++] = protocol_level;
    buffer[current_byte++] = flags;

    buffer[current_byte++] = keep_alive >> 8;
    buffer[current_byte++] = keep_alive & 0xFF;

    // Write client id.
    buffer[current_byte++] = 0;
    buffer[current_byte++] = client_id.length;
    memcpy(buffer + current_byte, client_id.data, client_id.length);
    current_byte += client_id.length;

    *remaining_length = current_byte - 2; // remove common header bytes.
    return client->Write(buffer, current_byte);
  }

  StringBuffer<6> protocol_name;
  uint8_t protocol_level;
  uint8_t flags;
  uint16_t keep_alive;
  StringBuffer<23> client_id;
};

// Connect for generic mqtt 3 session.
static constexpr Connect kDefaultConnect{
  .protocol_name = {"MQTT", 4},
  .protocol_level = 4,
  // Bit 1 means each session is independant and things don't carry over
  // between connections based on client_id.
  .flags = 0b10,
  .keep_alive = 0,
  .client_id = {"GNAT", 4},
};

struct ConnectAck {
  using Flags = PacketField<1>;
  using ReturnCode = PacketField<1>;
  
  template<typename Client>
  static std::optional<ConnectAck> ReadFrom(Client* client) {
    ConnectAck out;
    if (!ReadField<Flags>(client, &out.flags)) {
      DEBUG_LOG("Failed to read flags.\n");
      return {};
    }

    if (!ReadField<ReturnCode>(client, &out.return_code)) {
      DEBUG_LOG("Failed to read code.\n");
      return {};
		}

    return out;
  }
  
  template<typename Client>
  bool SendOn(Client* client) {
    static uint8_t buffer[128] = {0};
    uint8_t current_byte = 0;
    buffer[current_byte++] = ((uint8_t)PacketType::CONNACK << 4) & 0xF0;

    // We will come back to set the length last, it will be one byte though.
    uint8_t* remaining_length = &buffer[current_byte++];

    // Flags, all reserved except for 0 which is session present.
    // We don't support sessions though.
    buffer[current_byte++] = 0;

    // If it's an error we just send the generic error.
    buffer[current_byte++] = (error) ? 0x80 : 0;
    const uint8_t packet_size = current_byte;
    *remaining_length = packet_size - 2; // remove bytes for type and length.
    //*props_length = packet_size - header_size - 1;
    LOG("Sending Ack: size: %u, remaining: %u\n",
      packet_size, *remaining_length);

    return client->Write(buffer, packet_size);
  }

	// Used when sending.
  bool error = false;

	// Used when receiving.
	uint8_t flags = 0;
	uint8_t return_code = 0;
};

struct Publish {
  using Topic = PacketField<0, TYPE_STRING>;
  using PacketId = PacketField<2>;

  template<typename Client>
  static std::optional<Publish> ReadFrom(Client* client, uint8_t flags) {
    Publish out;
    if (!ReadField<Topic>(client, &out.topic)) {
      DEBUG_LOG("Failed to read topic.\n");
      return {};
    }

    if (((flags >> 1) & 0b11) != 0) {
      // We currently don't support QoS and ignore this, we need to
      // read it to ensure the payload size is right.
      uint16_t id = 0;
      if (!ReadField<PacketId>(client, &id)) {
        DEBUG_LOG("Failed to read packet id.\n");
        return {};
      }
    }

    out.payload_bytes = client->bytes_remaining();

    return out;
  }

  template<typename ClientConnection>
  bool SendOn(ClientConnection* connection, uint8_t* payload) {
    // This buffer contains the topic as well which can be long.
    constexpr auto kBufferSize = 256;
    static uint8_t buffer[kBufferSize] = {0};

    uint8_t current_byte = 0;
    constexpr uint8_t flags = 0; // We can expand functionality here.
    buffer[current_byte++] =
        (((uint8_t)PacketType::PUBLISH << 4) & 0xF0) | (flags & 0xF);

    // We pre-calculate the length here in order to do variable encoding.
    // Unlike other packets this one can easily be over 127 bytes long.
    int length = 2 /* topic length storage */ + topic.length + payload_bytes;
    while (length > 0) {
      if (length > 127) {
        buffer[current_byte++] = (length % 128) | 128;
      } else {
        buffer[current_byte++] = length;
      }
      length = length / 128;
    }

    buffer[current_byte++] = 0;
    buffer[current_byte++] = topic.length;
    memcpy(buffer + current_byte, topic.data, topic.length);
    current_byte += topic.length;

    assert(current_byte < kBufferSize);
    const auto header_size = current_byte;

    // Write buffered data.
    if (!connection->WritePartial(buffer, header_size)) return false;
    // Write payload.
    return connection->Write(payload, payload_bytes);
  }

  StringBuffer<128> topic;
  uint32_t payload_bytes = 0;
};

struct Subscribe {
    using PacketId = PacketField<2>;
    using Topic = PacketField<0, TYPE_STRING>;
    using Flags = PacketField<1>;

    template<typename Client, typename TopicCallback>
    static std::optional<Subscribe> ReadFrom(Client* client, TopicCallback&& callback) {
        Subscribe out;

        if (!ReadField<PacketId>(client, &out.packet_id)) {
            DEBUG_LOG("Failed to read packet id.\n");
            return {};
        }

        StringBuffer<128> topic;
        while (client->bytes_remaining() > 0) {
            if (!ReadField<Topic>(client, &topic)) {
                DEBUG_LOG("Failed to read topic.\n");
                return {};
            }
            if (!callback(topic.data, topic.length)) {
                return {};
            }

            // These are reserved in MQTT 3.1.1, ignore it.
            uint8_t flags;
            if (!ReadField<Flags>(client, &flags)) {
                DEBUG_LOG("Failed to read flags.\n");
                return {};
            }
        }

        return out;
    }

    template<typename Client>
    bool SendOn(Client* client) const {
      static uint8_t buffer[128] = {0};
      uint8_t current_byte = 0;
      // Spec requires bit 1 be set to 1.
      buffer[current_byte++] = (((uint8_t)PacketType::SUBSCRIBE << 4) | 0b10);

      // We will come back to set the length last, it will be one byte though.
      uint8_t* remaining_length = &buffer[current_byte++];

      // Packet identifier
      buffer[current_byte++] = packet_id >> 8;
      buffer[current_byte++] = packet_id & 0xFF;

      // Write topic name.
      buffer[current_byte++] = 0;
      buffer[current_byte++] = topic_name.length;
      memcpy(buffer + current_byte, topic_name.data, topic_name.length);
      current_byte += topic_name.length;

      // QoS is always zero for now.
      buffer[current_byte++] = 0;

      *remaining_length = current_byte - 2; // remove common header bytes.

      // If it is greater than 127 we need to use variable encoding on the length byte.
      assert(*remaining_length < 127);
      return client->Write(buffer, current_byte);
    }

    // TODO Spec supports more than one topic.
    StringBuffer<25> topic_name;
    uint16_t packet_id = 0;
};

struct SubscribeAck {
  using PacketId = PacketField<2>;
  using Response = PacketField<1>;
  
  uint16_t subscribe_packet_id = 0;
  uint8_t responses[32] = {0};
  uint8_t responses_count = 0;

  template<typename Client>
  static std::optional<SubscribeAck> ReadFrom(Client* client) {
    SubscribeAck out;
    if (!ReadField<PacketId>(client, &out.subscribe_packet_id)) {
      DEBUG_LOG("Failed to read id.\n");
      return {};
    }

		out.responses_count = 1;
    if (!ReadField<Response>(client, &out.responses[0])) {
      DEBUG_LOG("Failed to read response.\n");
      return {};
		}

    return out;
  }
  
  template<typename ClientConnection>
  bool SendOn(ClientConnection* connection) {
    static uint8_t buffer[32] = {0};
    uint8_t current_byte = 0;
    buffer[current_byte++] = ((uint8_t)PacketType::SUBACK << 4) & 0xF0;
    // We will come back to set the length last, it will be one byte though.
    uint8_t* remaining_length = &buffer[current_byte++];

    // PacketId
    buffer[current_byte++] = subscribe_packet_id >> 8;
    buffer[current_byte++] = subscribe_packet_id & 0xFF;

    memcpy(buffer + current_byte, responses, responses_count);
    current_byte += responses_count + 1;

    const uint8_t packet_size = current_byte - 1;
    *remaining_length = packet_size - 2; // remove bytes for type and length.
    DEBUG_LOG("Sending SubAck: size: %u remaining: %u responses: %u\n",
      packet_size, *remaining_length, responses_count);

    return connection->Write(buffer, packet_size);
  }
};

struct PingResp {
  template<typename ClientConnection>
  static bool SendOn(ClientConnection* connection) {
    static uint8_t buffer[] = {
      ((uint8_t)PacketType::PINGRESP << 4) & 0xF0,
      0}; // Size is always zero.
    DEBUG_LOG("Sending Ping Response.\n");
    return connection->Write(buffer, sizeof(buffer));
  }
};

}  // namespace proto3

// Represents a raw packet steaming from the connection, manages it's bytes left and ensure that all bytes are read.
template<typename ClientConnection>
class Packet {
public:
    static std::optional<Packet> ReadNext(ClientConnection connection) {
        DEBUG_LOG("Reading.\n");
        const auto header = FixedHeader::ReadFrom(&connection);
        if (!header) {
            return {};
        }

        return Packet(header->control, header->remaining_size, std::move(connection));
    }

  Packet(uint8_t control, size_t bytes_remaining, ClientConnection connection)
      : control_(control), bytes_remaining_(bytes_remaining), connection_(std::move(connection)) {
    DEBUG_LOG("New packet, size: %u\n", (uint32_t)bytes_remaining);
  }

  Packet(Packet&& from)
    : control_(from.control_),
      bytes_remaining_(from.bytes_remaining_),
      connection_(std::move(from.connection_)) {
    from.bytes_remaining_ = 0;
  }

  ~Packet() {
    // If the packet is destructed make sure we read all of our data from the
    // connection so it is clean for the next packet.
    if (bytes_remaining_ > 0) {
      DEBUG_LOG("Packet destructed with %d bytes, draining.\n", bytes_remaining_);
      if(!connection_.Drain(bytes_remaining_)) {
        LOG("drain on dtor fail\n");
      }
    }
  }

  PacketType type() {
    return static_cast<PacketType>(control_ >> 4);
  }

  uint32_t bytes_remaining() {
    return bytes_remaining_;
  }

  uint8_t type_flags() {
    return control_ & 0xF;
  }

  ClientConnection* connection() {
    return &connection_;
  }

  void Dump() {
    static uint8_t buffer[1024];
    size_t to_read = bytes_remaining_;
    Read(buffer, to_read);

    LOG("--\n");
    for(int i = 0; i < to_read; i++) {
      LOG("%X ", buffer[i]);
    }
    LOG("--\n");
  }

  bool Read(uint8_t* buffer, size_t size) {
    assert(size <= bytes_remaining_);
    if (connection_.Read(buffer, size)) {
      bytes_remaining_ -= size;
      return true;
    }
    return false;
  }

  bool Drain(size_t size) {
    if (size > bytes_remaining_) {
      LOG("Failed to read %u bytes, only %u left\n", size, bytes_remaining_);
      return false;
    }

    if (connection_.Drain(size)) {
      bytes_remaining_ -= size;
      return true;
    }
    return false;
  }

private:
  const uint8_t control_;
  uint32_t bytes_remaining_ = 0;
  ClientConnection connection_;
};

}  //namespace gnat
