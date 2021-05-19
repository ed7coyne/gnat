#pragma once

#ifdef ARDUINO

#if GNAT_LOG_LEVEL > 0

static constexpr size_t kLogBufferSize = 128;
static char log_buffer[kLogBufferSize];
void serial_printf(const char* format, ...) {
  va_list args;
  va_start (args, format);

  int written = vsnprintf(log_buffer, kLogBufferSize - 1, format, args);
  va_end (args);

  log_buffer[written] = 0;
  Serial.println(log_buffer);
}

#define LOG(...) serial_printf( __VA_ARGS__);

#else

#define LOG(...)

#endif

#if GNAT_LOG_LEVEL > 1

#define DEBUG_LOG(...) serial_printf(__VA_ARGS__)

#else

#define DEBUG_LOG(...)

#endif

#else // Not Arduino

#include <cstdint>
#include <cstdio>

// Since this is currently only for tests log level is ignored and you get all the logs.

#define LOG(...) printf(__VA_ARGS__);
#define DEBUG_LOG(...) printf(__VA_ARGS__)

#endif

void LogHex(uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; i++) {
    LOG("%02X", data[i]);
  }
  LOG("\n");
}

