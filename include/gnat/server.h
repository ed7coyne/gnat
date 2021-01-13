#pragma once

#include <string>
#include <memory>

#include "status.h"
#include "datastore.h"
#include "log.h"
#include "packets.h"

namespace gnat {

enum class ConnectionType {
    UNKNOWN,
    MQTT_31,
    MQTT_311,
    MQTT_5,
};


/*
 * ClientConnection is expected to provide:
 * bool Read(char* buffer, size_t bytes);
 * bool Write(char* buffer, size_t bytes);
 * ConnectionType connection_type();
 * void set_connection_type(ConnectionType);
 *
 * Clock should provide:
 * uint32_t timestamp();
 *
 * DataStore should be a gnat::DataStore with template parameters.
*/

template<typename DataStore, typename Clock>
class Server {
public:
    Server(DataStore* data, Clock* clock) : data_(data), clock_(clock) {}

    template<typename ClientConnection>
    Status HandleMessage(Packet<ClientConnection>* packet) {
      DEBUG_LOG("Handling message: %u\n", (uint8_t)packet->type());
      if (packet->type() == PacketType::CONNECT) {
        const auto connect = proto3::Connect::ReadFrom(packet);
        DEBUG_LOG("Header Read, proto: %s\n", (*connect).protocol_name.data);
        proto3::ConnectAck ack;
        if (!connect.has_value() || (
            strcmp("MQTT", (*connect).protocol_name.data) == 0 &&
            strcmp("MQIsdp", (*connect).protocol_name.data) == 0)) {
            LOG("Connect packet has wrong header or wrong protocol.\n");
            ack.error = true;
        }

        if ((*connect).protocol_level == 3) {
            packet->connection()->set_connection_type(ConnectionType::MQTT_31);
        } else if ((*connect).protocol_level == 4) {
            packet->connection()->set_connection_type(ConnectionType::MQTT_311);
        } else if ((*connect).protocol_level == 5) {
            packet->connection()->set_connection_type(ConnectionType::MQTT_5);
        } else {
            LOG("Connect packet has unsupported protocol version.\n");
            ack.error = true;
        }

        if(!ack.SendOn(packet->connection())) {
          LOG("Failed to send response.\n");
          return Status::Failure("Unable to send response.");
        }
      } else if (packet->type() == PacketType::PUBLISH) {
        const auto publish_opt = proto3::Publish::ReadFrom(packet, packet->type_flags());
        if (!publish_opt.has_value()) {
          return Status::Failure("No publish header!");
        }
        const auto& publish = *publish_opt;

        DataStoreEntry entry(clock_->timestamp());
        entry.length = publish.payload_bytes;
        entry.data = std::unique_ptr<uint8_t[]>(new uint8_t[entry.length]);
        if (!packet->Read(entry.data.get(), entry.length)) {
          LOG("Failed to read publish. Size: %lu \n", entry.length);
          return Status::Failure("Unable to complete read.");
        }
        DEBUG_LOG("Read publish.\n");
        const auto key = DataStore::EncodeKey(publish.topic.data, publish.topic.length);
        data_->Set(key, std::move(entry));
      } else if (packet->type() == PacketType::SUBSCRIBE) {
        auto topic_callback = [&](char* topic, size_t topic_length) {
            auto connection_heap = packet->connection()->CreateHeapCopy();

            if (memchr(topic, '+', topic_length) != nullptr) {
              LOG("Use of + wildcard in topics not supported.");
              return false;
            }

            const bool is_prefix = (memchr(topic, '#', topic_length) != nullptr);

            const auto target_key = DataStore::EncodeKey(
                topic, (is_prefix) ? topic_length -1 : topic_length);

            const auto key_matcher = is_prefix ?
              DataStore::PrefixKeyMatcher(target_key) :
              DataStore::FullKeyMatcher(target_key);

            data_->AddObserver(
                [key_matcher, conn = std::move(connection_heap)]
                (typename DataStore::Key key, const DataStoreEntry& entry) mutable {
                  if (key_matcher(key)) {
                    proto3::Publish packet;
                    DataStore::DecodeKey(key, packet.topic.data, &packet.topic.length);
                    packet.payload_bytes = entry.length;
                    if (!packet.SendOn<ClientConnection>(&conn, entry.data.get())) {
                      return false;
                    }
                  }
                  return true;
            });
            return true;
        };

        const auto subscribe_opt = proto3::Subscribe::ReadFrom(packet, topic_callback);
        if (!subscribe_opt.has_value()) {
          return Status::Failure("");
        }

        proto3::SubscribeAck ack {
          .subscribe_packet_id = subscribe_opt->packet_id,
          .responses = {0},
          .responses_count = 1,
        };
        if(!ack.SendOn(packet->connection())) {
          LOG("Failed to send response.\n");
          return Status::Failure("Unable to send response.");
        }
      } else if (packet->type() == PacketType::PINGREQ) {
        if (!proto3::PingResp::SendOn(packet->connection())) {
          LOG("Failed to send response.\n");
          return Status::Failure("Unable to send response.");
        }
      } else if (packet->type() == PacketType::DISCONNECT) {
        LOG("Client disconnected..\n");
        packet->connection()->Close();
      } else {
        LOG("Unsupported packet type: %u\n", (uint8_t)packet->type());
        return Status::Failure("Unsupported packet type.");
      }

      return Status::Ok();
    }

private:
    DataStore* data_;
    Clock* clock_;
};

} // namespace gnat
