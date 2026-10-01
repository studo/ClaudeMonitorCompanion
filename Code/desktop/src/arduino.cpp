// The Arduino and FreeRTOS runtime the firmware links against on the desktop.

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>

#include <unistd.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "desktop.h"

HardwareSerial Serial;
EspClass ESP;
TwoWire Wire;
WiFiClass WiFi;
MDNSResponder MDNS;

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point kStart = Clock::now();

std::mutex s_rx_mutex;
std::deque<char> s_rx;

std::mutex s_out_mutex;

}  // namespace

// --------------------------------------------------------------- timing --

uint32_t millis() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now() - kStart)
      .count();
}

uint32_t micros() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
             Clock::now() - kStart)
      .count();
}

void delay(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// --------------------------------------------------------------- serial --

void desktop::serial_feed(const char *s, size_t n) {
  std::lock_guard<std::mutex> lock(s_rx_mutex);
  s_rx.insert(s_rx.end(), s, s + n);
}

int HardwareSerial::available() {
  std::lock_guard<std::mutex> lock(s_rx_mutex);
  return (int)s_rx.size();
}

int HardwareSerial::read() {
  std::lock_guard<std::mutex> lock(s_rx_mutex);
  if (s_rx.empty()) return -1;
  const char c = s_rx.front();
  s_rx.pop_front();
  return (unsigned char)c;
}

// The render task and loop() both log, as they do on the board. A lock keeps
// their lines whole.
size_t HardwareSerial::printf(const char *fmt, ...) {
  std::lock_guard<std::mutex> lock(s_out_mutex);
  va_list ap;
  va_start(ap, fmt);
  const int n = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return n < 0 ? 0 : (size_t)n;
}

size_t HardwareSerial::print(const char *s) {
  std::lock_guard<std::mutex> lock(s_out_mutex);
  return fputs(s, stdout) < 0 ? 0 : strlen(s);
}

size_t HardwareSerial::println() {
  std::lock_guard<std::mutex> lock(s_out_mutex);
  fputc('\n', stdout);
  return 1;
}

size_t HardwareSerial::println(const char *s) {
  std::lock_guard<std::mutex> lock(s_out_mutex);
  fputs(s, stdout);
  fputc('\n', stdout);
  return strlen(s) + 1;
}

// ------------------------------------------------------------------ ESP --

// Resident set size, reported where the board reports free heap. Not the same
// quantity, but the settings page and the health route only display it.
uint32_t EspClass::getFreeHeap() {
  long pages = 0, resident = 0;
  FILE *f = fopen("/proc/self/statm", "r");
  if (f) {
    if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
    fclose(f);
  }
  return (uint32_t)(resident * sysconf(_SC_PAGESIZE));
}

void EspClass::restart() { desktop::restart(); }

// ----------------------------------------------------------------- WiFi --

IPAddress WiFiClass::localIP() {
  return IPAddress(desktop::display_address().c_str());
}

// ------------------------------------------------------------- FreeRTOS --

TickType_t xTaskGetTickCount() { return millis(); }

void vTaskDelay(TickType_t ticks) { delay(ticks); }

void taskYIELD() { std::this_thread::yield(); }

// Same contract as FreeRTOS: wake at a fixed cadence measured from the previous
// wake, not from now, so jitter in one frame does not push every later one.
void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment) {
  const TickType_t target = *previous_wake + increment;
  const int32_t wait = (int32_t)(target - millis());
  if (wait > 0) delay((uint32_t)wait);
  *previous_wake = target;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *, uint32_t,
                                   void *param, UBaseType_t, TaskHandle_t *handle,
                                   BaseType_t) {
  std::thread t(fn, param);
  if (handle) *handle = (TaskHandle_t)(uintptr_t)1;
  t.detach();
  return pdPASS;
}

struct DesktopQueue {
  std::mutex mutex;
  std::deque<std::vector<uint8_t>> items;
  size_t length;
  size_t item_size;
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  DesktopQueue *q = new DesktopQueue;
  q->length = length;
  q->item_size = item_size;
  return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t) {
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->items.size() >= q->length) return errQUEUE_FULL;
  const uint8_t *p = (const uint8_t *)item;
  q->items.emplace_back(p, p + q->item_size);
  return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t) {
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->items.empty()) return pdFALSE;
  memcpy(item, q->items.front().data(), q->item_size);
  q->items.pop_front();
  return pdTRUE;
}
