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

struct ConnectHeader {
  char protocol_name[6] = {0};
  uint8_t protocol_name_length = 0;

  uint8_t protocol_version;

  // We don't support any features in the flags currently.
  uint8_t protocol_flags;
};

struct ConnectAck {
  bool error = false;

  // Set a reasonable max, we currently aren't hard limited to this anywhere.
  constexpr static uint32_t kMaxPacketSize = 1024;

  template<typename ClientConnection>
  bool SendOn(ClientConnection* connection) {
    static uint8_t buffer[128] = {0};
    uint8_t current_byte = 0;
    buffer[current_byte++] = ((uint8_t)PacketType::CONNACK << 4) & 0xF0;
    // We will come back to set the length last, it will be one byte though.
    uint8_t* remaining_length = &buffer[current_byte++];

    // Flags, all reserved except for 0 which is session present.
    // We don't support sessions though.
    buffer[current_byte++] = 0;

    // If it's and error we just send the generic error.
    buffer[current_byte++] = (error) ? 0x80 : 0;
    
    const auto header_size = current_byte; 
    uint8_t* props_length = &buffer[current_byte++];

    // Max QoS of 0 (for now).
    buffer[current_byte++] = 0x24;
    buffer[current_byte++] = 0;
    
    // We support retain.
    buffer[current_byte++] = 0x25;
    buffer[current_byte++] = 1;
    
    // We don't support subscription ids.
    buffer[current_byte++] = 0x29;
    buffer[current_byte++] = 0;
    
    // We don't support shared subscriptions.
    buffer[current_byte++] = 0x2A;
    buffer[current_byte++] = 0;
    
    // Max packet size.
    buffer[current_byte++] = 0x27;
    buffer[current_byte++] = *(uint8_t*)&kMaxPacketSize;
    buffer[current_byte++] = *((uint8_t*)&kMaxPacketSize + 1);
    buffer[current_byte++] = *((uint8_t*)&kMaxPacketSize + 2);
    buffer[current_byte++] = *((uint8_t*)&kMaxPacketSize + 3);

    // Assigned ClientId.
    buffer[current_byte++] = 0x12;
    buffer[current_byte++] = 0;
    buffer[current_byte++] = 3;
    buffer[current_byte++] = 'c';
    buffer[current_byte++] = 'i';
    buffer[current_byte++] = 'd';

    const uint8_t packet_size = current_byte - 1;
    *remaining_length = packet_size - 2; // remove bytes for type and length.
    *props_length = packet_size - header_size - 1;
    LOG("Sending Ack: size: %u, remaining: %u, props_length: %u\n",
      packet_size, *remaining_length, *props_length);

    return connection->WriteAll(buffer, packet_size);
  }
};

struct Publish {
  uint8_t flags = 0;
  char topic[8];
  uint8_t topic_length = 0;

  uint32_t timestamp = 0;

  uint8_t* payload = nullptr;
  uint16_t  payload_size = 0;

  bool flag_retain() {
    return (flags & 0b1) != 0;
  }

  uint8_t flag_qos_level() {
    return ((flags >> 1) & 0b11);
  }
  
  bool flag_dup() {
    return (flags & 0b100) != 0;
  }

  template<typename ClientConnection>
  bool SendOn(ClientConnection* connection) {
    static uint8_t buffer[48] = {0};
    uint8_t current_byte = 0;
    buffer[current_byte++] = 
        (((uint8_t)PacketType::PUBLISH << 4) & 0xF0) | (flags & 0xF);
    
    //TODO: handle over 128 bytes!
    auto* packet_length = &buffer[current_byte++];

    buffer[current_byte++] = 0;
    buffer[current_byte++] = topic_length;
    memcpy(buffer + current_byte, topic, topic_length);
    current_byte += topic_length;
    
    auto* props_length = &buffer[current_byte++];
    const auto header_size = current_byte; 

/*
    // We use userproprty to carry ourtimestamp.
    buffer[current_byte++] = 0x26;
    // empty key.
    buffer[current_byte++] = 0;
    buffer[current_byte++] = 0;
    // 4 byte value.
    buffer[current_byte++] = 0;
    buffer[current_byte++] = 4;
    *props_length = (current_byte - 1) - header_size;
  */
    props_length = 0;

/*
    // Put timestamp as first four bytes of payload.
    static_assert(sizeof(timestamp) == 4, "");
    memcpy(buffer + current_byte, &timestamp, 4);
    current_byte += 4;
*/
    const auto packet_size = header_size + payload_size;
    *packet_length = packet_size - 2; // remove fixed header from size.
    
//    DEBUG_LOG("Writing Publish, size: %u\n", packet_size);
//    LogHex(payload, payload_size);

    //// Write buffered data.
    if (!connection->WriteAll(buffer, header_size)) return false;
    // Write payload.
    return connection->WriteAll(payload, payload_size);
 }

};

struct Subscribe {
  uint16_t packet_id;  
  std::vector<uint64_t> topics;
};

struct SubAck {
  uint8_t responses[32] = {0};
  uint8_t responses_count = 0;

  template<typename ClientConnection>
  bool SendOn(ClientConnection* connection) {
    static uint8_t buffer[32] = {0};
    uint8_t current_byte = 0;
    buffer[current_byte++] = ((uint8_t)PacketType::SUBACK << 4) & 0xF0;
    // We will come back to set the length last, it will be one byte though.
    uint8_t* remaining_length = &buffer[current_byte++];
    
    buffer[current_byte++] = 0; // payload bytes.

    memcpy(buffer + current_byte, responses, responses_count);
    current_byte += responses_count + 1;
   
    const uint8_t packet_size = current_byte - 1;
    *remaining_length = packet_size - 2; // remove bytes for type and length.
    LOG("Sending SubAck: size: %u remaining: %u responses: %u\n",
      packet_size, *remaining_length, responses_count);

    return connection->WriteAll(buffer, packet_size);
  }
};


struct PingResp {
  template<typename ClientConnection>
  static bool SendOn(ClientConnection* connection) {
    static uint8_t buffer[] = {
      ((uint8_t)PacketType::PINGRESP << 4) & 0xF0,
      0}; // Size is always zero.
    DEBUG_LOG("Sending Ping Response.\n");
    return connection->WriteAll(buffer, sizeof(buffer));
  }
};

template<typename ClientConnection>
class Packet {
public:

    static std::optional<Packet> ReadNext(ClientConnection connection) {
        DEBUG_LOG("Reading.\n");
        uint8_t control = 0;
        if (!connection.ReadAll(&control, 1)) {
            DEBUG_LOG("Failed to read control.\n");
            return {};
        }
        size_t remaining_size = 0;
        if (!ReadVariableByteInteger(&remaining_size, &connection)) {
            DEBUG_LOG("Failed to read size.\n");
            return {};
        }
        return Packet(control, remaining_size, std::move(connection));
    }

  Packet(uint8_t control, size_t remaining_bytes, ClientConnection connection) 
      : control_(control), remaining_bytes_(remaining_bytes), connection_(std::move(connection)) {
    DEBUG_LOG("New packet, size: %u\n", (uint32_t)remaining_bytes);
  }

  Packet(Packet&& from) 
    : control_(from.control_),
      remaining_bytes_(from.remaining_bytes_),
      connection_(std::move(from.connection_)) {
    from.remaining_bytes_ = 0;
  }

  ~Packet() {
    // If the packet is destructed make sure we read all of our data from the 
    // connection so it is clean for the next packet.
    if (remaining_bytes_ > 0) {
      DEBUG_LOG("Packet destructed with %d bytes, draining.\n", remaining_bytes_);
      if(!connection_.Drain(remaining_bytes_)) {
        LOG("drain on dtor fail\n");
      }
    }
  }

  PacketType type() {
    return static_cast<PacketType>(control_ >> 4);
  }

  uint8_t remaining_bytes() {
    return remaining_bytes_;
  }

  uint8_t type_flags() {
    return control_ & 0xF;
  }

  ClientConnection* connection() {
    return &connection_;
  }

  std::optional<ConnectHeader> ReadConnectHeader() {
    DEBUG_LOG("Reading connect header.\n");

    ConnectHeader out;
    uint8_t length[2];
    if (!ReadAll(length, 2)) return {};

    if (length[0] != 0 || length[1] > 6) {
      DEBUG_LOG("Protocol name is longer than any that we support. bytes: %X %X\n", length[0], length[1]);
      return {};
    }
    out.protocol_name_length = length[1];

    if (!ReadAll((uint8_t*)out.protocol_name, out.protocol_name_length)) return {};
    if (!ReadAll(&out.protocol_version, 1)) return {};
    if (!ReadAll(&out.protocol_flags, 1)) return {};

    // We will read and discard the keep-alive since we don't support it.
    if (!ReadAll(length, 2)) return {};

    uint16_t property_bytes = 0;
    if (!ReadVariableByteInteger(&property_bytes)) return {};
    return out;
  }

  std::optional<Publish> ReadPublish() {
    DEBUG_LOG("Reading publish. Flags: %X\n", type_flags());

    Publish out;

    uint8_t length[2];
    if (!ReadAll(length, 2)) return {};

    if (length[0] != 0 || length[1] > 8) {
      DEBUG_LOG("Topic is longer than any that we support. bytes: %X %X\n", length[0], length[1]);
      return {};
    }
    out.topic_length = length[1];

    if (!ReadAll((uint8_t*)out.topic, out.topic_length)) return {};
    DEBUG_LOG("topic: %s (%d)\n", 
        std::string(out.topic, out.topic_length).c_str(), out.topic_length);
   

    out.payload_size = remaining_bytes_;

    return out;
  }

  std::optional<Subscribe> ReadSubscribe() {
    DEBUG_LOG("Reading subscribe.\n");
   
    Subscribe out;

    if (!ReadAll((uint8_t*)&out.packet_id, 2)) return {};
    
    // We don't support any properties here, just drain them.
    uint16_t property_bytes = 0;
    if (!ReadVariableByteInteger(&property_bytes)) return {};
    
    if (!Drain(property_bytes)) return {};

    while (remaining_bytes_ > 0) {
      // read subscription topic.
      uint8_t topic_bytes[2];
      if (!ReadAll(topic_bytes, 2)) return {};

      if (topic_bytes[0] != 0 || topic_bytes[1] > 8) {
        LOG("Topic too long: %x\n", topic_bytes);
        return {};
      }

      uint64_t topic = 0;
      if (!ReadAll((uint8_t*)&topic, topic_bytes[1])) return {};
      topic = key::EncodeString((char*)&topic, topic_bytes[1]);

      out.topics.emplace_back(std::move(topic));

      // read subscriptions options. We don't support any so drop it.
      if(!Drain(1)) return {};
    }

    return out;
  }

  void Dump() {
    static uint8_t buffer[1024];
    size_t to_read = remaining_bytes_;
    ReadAll(buffer, to_read);

    LOG("--\n");
    for(int i = 0; i < to_read; i++) {
      LOG("%X ", buffer[i]);
    }
    LOG("--\n");
  }

  // Wraps the connection but tracks read bytes.
  bool ReadAll(uint8_t* buffer, size_t size) {
    assert(size <= remaining_bytes_);
    if (connection_.ReadAll(buffer, size)) {
      remaining_bytes_ -= size;
      return true;
    }
    return false;
  }

  bool Drain(size_t size) {
    if (size > remaining_bytes_) {
      LOG("Failed to read %u bytes, only %u left\n", size, remaining_bytes_);
      return false;
    }

    if (connection_.Drain(size)) {
      remaining_bytes_ -= size;
      return true;
    }
    return false;
  }

  template<typename T>
  bool ReadVariableByteInteger(T* out) {
    uint8_t next_byte = 0;
    *out = 0;
    while(true) {
        if (!ReadAll(&next_byte, 1)) {
            return false;
        }
        *out |= next_byte & 127;

        if ((next_byte & 128) == 0) {
            break;
        } else {
            *out = (*out << 7) & 0xFFFFFF00;
        }
    }
    return true;
  }

  template<typename T>
  static bool ReadVariableByteInteger(T* out, ClientConnection* connection) {
    uint8_t next_byte = 0;
    *out = 0;
    while(true) {
        if (!connection->ReadAll(&next_byte, 1)) {
            return false;
        }
        *out |= next_byte & 127;

        if ((next_byte & 128) == 0) {
            break;
        } else {
            *out = (*out << 7) & 0xFFFFFF00;
        }
    }
    return true;
  }

private:
  const uint8_t control_;
  uint8_t remaining_bytes_ = 0;
  ClientConnection connection_;

};

}  //namespace gnat
