#pragma once
/* Host tests are single-threaded; a lock only has to be created once. */
typedef int *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { static int mutex; return &mutex; }
static inline int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned ticks) { (void)ticks; ++*mutex; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t mutex) { --*mutex; return 1; }
