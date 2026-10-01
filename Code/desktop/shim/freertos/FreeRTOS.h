#pragma once

// The two FreeRTOS facilities the firmware uses — a task and a queue — over
// std::thread and a mutex.
//
// The thread layout mirrors the board's two cores: the render task gets its own
// thread, exactly as it gets core 1 on the ESP32, and setup()/loop() run on
// another. So the firmware's own cross-task handoffs (the mood queue, the
// volatile style and view requests) are exercised the same way on both.

#include <stddef.h>
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef struct DesktopQueue *QueueHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define errQUEUE_FULL 0
#define portMAX_DELAY 0xffffffffu

// One tick is one millisecond, as configured on the board.
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

TickType_t xTaskGetTickCount();
void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment);
void vTaskDelay(TickType_t ticks);
void taskYIELD();

// Core affinity and priority have no meaning here; the task simply gets a
// thread of its own.
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                   uint32_t stack, void *param,
                                   UBaseType_t priority, TaskHandle_t *handle,
                                   BaseType_t core);

// Only the non-blocking forms are supported (wait == 0), which is all the
// firmware uses.
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
