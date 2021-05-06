// Responsible for taking all of the data and serving it to any listeners.
// A simple key-value store over mqtt using gnat.

#pragma once

#include <memory>

#include "server.h"

#ifdef ARDUINO

namespace arduino {

class Clock {
public:
  uint32_t timestamp() {
    return millis();
  }
};
Clock ard_clock;

class Connection {
public:
  // Owns the wifi client.
  explicit Connection(std::unique_ptr<WiFiClient> client) : client_(client.get()), owned_client_(client.release()) {}

  // Borrows a wifi client.
  explicit Connection(WiFiClient* client) : client_(client) {}

  Connection CreateHeapCopy() {
    std::unique_ptr<WiFiClient> heap_client(new WiFiClient());
    *heap_client = *client_;
    return Connection(std::move(heap_client));
  }

  bool Read(uint8_t* buffer, size_t bytes) {
    while (bytes > 0) {
      DEBUG_LOG("Reading: %u\n", bytes);
      
      while (client_->available() < 1 && client_->connected()) delay(1);

      if (!client_->connected()) {
        LOG("Client disconnected..");
        return false;
      }
      errno = 0;
      const auto read = client_->read(buffer, bytes);
      DEBUG_LOG("\tRead: %d\n", read);
      if (read < 1) {
        if (errno == ENOTCONN) {
          LOG("Connection dropped.");
          return false;
        } else if (errno == EAGAIN) {
          LOG("\tRead timeout, retry.\n");
          delay(50);
        } else if (read < 0)  {
          LOG("\tRead failed errno: %d\n", errno);
          return false;
        }
      } else {
        bytes -= read;
      }
    }
    
    return true;
  }

  bool Drain(size_t bytes) {
    // If we ever start using the other core re-consider the static.
    constexpr size_t kBufferSize = 64;
    static uint8_t buffer[kBufferSize];
    while (bytes > 0) {
      const auto to_drain = min(bytes, kBufferSize);
      if (!Read(buffer, to_drain)) return false;
      bytes -= to_drain; 
    }
    return true;
  }
  
  bool WritePartial(uint8_t* buffer, size_t bytes) {
    // For the tcp connection we don't care if it is a full or partial write.
    return Write(buffer, bytes);
  }

  bool Write(uint8_t* buffer, size_t bytes) {
    DEBUG_LOG("Writing %u bytes, first %X last: %x\n", bytes, buffer[0], buffer[bytes-1]);
    while (bytes > 0) {
      const auto written = client_->write((char*)buffer, bytes);
      DEBUG_LOG("\t Wrote %u bytes\n", written);
      if (written == -1 || !client_->connected()) return false;
      buffer += written;
      bytes -= written;
    }
    return true;
  }

  void Close() {
    client_->stop();
  }

  gnat::ConnectionType connection_type() { return connection_type_; }
  void set_connection_type(gnat::ConnectionType type) { connection_type_ = type; }

  uint32_t id() { return client_->fd(); }

private:
  WiFiClient* client_;
  std::shared_ptr<WiFiClient> owned_client_;
  gnat::ConnectionType connection_type_ = gnat::ConnectionType::UNKNOWN;
};

} // namespace arduino

#endif // ARDUINO
