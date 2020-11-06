// Responsible for taking all of the data and serving it to any listeners.
// A simple key-value store over mqtt using gnat.

#pragma once

#include <memory>

#include "server.h"

// Enables debug messages in gnat-arduino.
//#define ARDUINO_DEBUG 1

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

  bool ReadAll(uint8_t* buffer, size_t bytes) {
    while (bytes > 0) {
      DEBUG_LOG("Reading: %u\n", bytes);
      
      while (client_->available() < 1 && client_->connected());
      const auto read = client_->read(buffer, bytes);
      DEBUG_LOG("\tRead: %d\n", read);
      //Serial.println((uint8_t)buffer[0], HEX);
      if (read == -1) return false;
      bytes -= read; 
    }
    
    return true;
  }

  bool Drain(size_t bytes) {
    // If we ever start using the other core re-consider the static.
    constexpr size_t kBufferSize = 64;
    static uint8_t buffer[kBufferSize];
    while (bytes > 0) {
      const auto to_drain = min(bytes, kBufferSize);
      if (!ReadAll(buffer, to_drain)) return false;
      bytes -= to_drain; 
    }
    return true;
  }
  
  bool WriteAll(uint8_t* buffer, size_t bytes) {
    while (bytes > 0) {
      DEBUG_LOG("Writing %u bytes\n", bytes);
     
      const auto written = client_->write((char*)buffer, bytes);
      DEBUG_LOG("\t Written %u bytes\n", written);
      if (written == -1 || !client_->connected()) return false;
      buffer += written;
      bytes -= written;
    }
    //client_->flush();
    return true;
  }

  void Close() {
    client_->stop();
  }

  gnat::ConnectionType connection_type() { return connection_type_; }
  void set_connection_type(gnat::ConnectionType type) { connection_type_ = type; }
  
private:
  WiFiClient* client_;
  std::shared_ptr<WiFiClient> owned_client_;
  gnat::ConnectionType connection_type_ = gnat::ConnectionType::UNKNOWN;
};

} // namespace arduino

#endif // ARDUINO
