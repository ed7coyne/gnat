#pragma once

#ifdef ARDUINO

#ifdef ARDUINO_DEBUG


static constexpr size_t kBufferSize = 128;
static char log_buffer[kBufferSize];
void serial_printf(char* format, ...) {
  va_list args;
  va_start (args, format);

  int written = vsnprintf(log_buffer, kBufferSize - 1, format, args);
  va_end (args);

  log_buffer[written] = 0;
  Serial.println(log_buffer);
}

#else

#define serial_printf(...) 

#endif

#define LOG(...) serial_printf( __VA_ARGS__);
#define DEBUG_LOG(...) serial_printf(__VA_ARGS__)

#else 

#define LOG(...) printf(__VA_ARGS__);
#define DEBUG_LOG(...) printf(__VA_ARGS__)

#endif
//#define LOG(...) printf(__VA_ARGS__);
//#define DEBUG_LOG(...) printf(__VA_ARGS__)

void LogHex(uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; i++) {
    LOG("%02X", data[i]);
  }
  LOG("\n");
}

