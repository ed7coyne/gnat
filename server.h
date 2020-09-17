#pragma once

#include <string.h>

#include "status.h"
#include "datastore.h"
#include "log.h"
#include "packets.h"

namespace gnat {

/*
 * ClientConnection is expected to provide:
 * bool ReadAll(char* buffer, size_t bytes);
 * bool WriteAll(char* buffer, size_t bytes);
 *
 * Clock should provide:
 * uint32_t timestamp();
*/

template<typename ClientConnection, typename Clock>
class Server {
public:
    Server(DataStore* data, Clock* clock) : data_(data), clock_(clock) {}

    Status HandleMessage(Packet<ClientConnection>* packet) {
      DEBUG_LOG("Handling message: %u\n", (uint8_t)packet->type());
      if (packet->type() == PacketType::CONNECT) {
        const auto header = packet->ReadConnectHeader();
        DEBUG_LOG("Header Read, proto: %s\n", (*header).protocol_name);
        ConnectAck ack;
        if (!header.has_value() || (
            strcmp("MQTT", (*header).protocol_name) == 0 && 
            strcmp("MQIsdp", (*header).protocol_name) == 0) ||
            (*header).protocol_version != 4) {
            LOG("Connect packet has wrong header or wrong protocol.\n");
            ack.error = true;
        }
        if(!ack.SendOn(packet->connection())) {
          LOG("Failed to send response.\n");
          return Status::Failure("Unable to send response.");
        }
      } else if (packet->type() == PacketType::PUBLISH) {
        const auto header_opt = packet->ReadPublish();
        if (!header_opt.has_value()) {
          return Status::Failure("No publish header!");
        }
        const auto& header = *header_opt;

        DataStore::Entry entry(clock_->timestamp());
        entry.length = header.payload_size;
        entry.data = std::make_unique<uint8_t[]>(entry.length);
        if (!packet->ReadAll(entry.data.get(), entry.length)) {
          LOG("Failed to read publish. Size: %lu \n", entry.length);
          return Status::Failure("Unable to complete read.");
        }
        DEBUG_LOG("Read publish.");
        //LogHex(entry.data.get(), entry.length);
        const uint64_t key = key::EncodeString(header.topic, header.topic_length);
        data_->Set(key, std::move(entry));
      } else if (packet->type() == PacketType::SUBSCRIBE) {
        const auto header_opt = packet->ReadSubscribe();
        if (!header_opt.has_value()) {
          return Status::Failure("");
        }
        const auto& header = *header_opt;

        SubAck ack {
          .responses = {0},
          .responses_count = 1,
        };
        if(!ack.SendOn(packet->connection())) {
          LOG("Failed to send response.\n");
          return Status::Failure("Unable to send response.");
        }

        auto connection_heap = packet->connection()->CreateHeapCopy();
        data_->AddObserver(
            [header, conn = std::move(connection_heap)](uint64_t key, const DataStore::Entry& entry) mutable {
            LOG("Observer called for key: %llX\n", key);
            //TODO add support for wildcards.
            for (const uint64_t topic : header.topics) {
              LOG("Testing topic: %llX == %llX\n", topic, key);
              if (topic == key) {
                LOG("Writing to client!\n");
                Publish packet;
                key::DecodeString(key, packet.topic, &packet.topic_length);
                packet.payload = entry.data.get();
                packet.payload_size = entry.length;
                if (!packet.SendOn<ClientConnection>(&conn)) {
                  return false;
                }
              }
            }
            return true;
        });
      } else if (packet->type() == PacketType::PINGREQ) {
        if (!PingResp::SendOn(packet->connection())) {
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
