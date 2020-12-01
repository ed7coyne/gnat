#pragma once

#include <string.h>
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
 * bool ReadAll(char* buffer, size_t bytes);
 * bool WriteAll(char* buffer, size_t bytes);
 * ConnectionType connection_type();
 * void set_connection_type(ConnectionType);
 *
 * Clock should provide:
 * uint32_t timestamp();
*/

template<typename ClientConnection, typename DataStore, typename Clock>
class Server {
public:
    Server(DataStore* data, Clock* clock) : data_(data), clock_(clock) {}

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
        if (!packet->ReadAll(entry.data.get(), entry.length)) {
          LOG("Failed to read publish. Size: %lu \n", entry.length);
          return Status::Failure("Unable to complete read.");
        }
        DEBUG_LOG("Read publish.");
        //LogHex(entry.data.get(), entry.length);
        const auto key = DataStore::EncodeKey(publish.topic.data, publish.topic.length);
        data_->Set(key, std::move(entry));
      } else if (packet->type() == PacketType::SUBSCRIBE) {
        auto topic_callback = [&](auto* topic) {
            auto connection_heap = packet->connection()->CreateHeapCopy();
            const typename DataStore::Key target_key = DataStore::EncodeKey(topic->data, topic->length);

            data_->AddObserver(
                [&target_key, conn = std::move(connection_heap)]
                (typename DataStore::Key key, const DataStoreEntry& entry) mutable {
                  if (target_key == key) {
                    LOG("Writing to client!\n");
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
      } else {
        LOG("Unsupported packet type: %u\n", (uint8_t)packet->type());
        return Status::Failure("Unsupported packet type.");
      }

    	return Status::Ok();
    }

private:
    static bool WriteHeader(uint64_t /*key*/, uint32_t /*timestamp*/, uint32_t /*length*/,
	    ClientConnection* /*connection*/) {
     	return true;
    }

    std::list<std::unique_ptr<ClientConnection>> connections_;
    DataStore* data_;
    Clock* clock_;
};

} // namespace gnat
