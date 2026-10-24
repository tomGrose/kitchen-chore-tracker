#pragma once
/*---------------------------------------------------------------
 * Leveled serial logging. Level is set by APP_LOG_LEVEL in config.h;
 * levels above it compile to nothing.
 * Usage: LOGI("tag", "value=%d", v);
 *--------------------------------------------------------------*/
#include <Arduino.h>
#include "config.h"

#define APP_LOG_(lvl, tag, fmt, ...) \
    Serial.printf("[%lu][" lvl "][%s] " fmt "\n", (unsigned long)millis(), tag, ##__VA_ARGS__)

#if APP_LOG_LEVEL >= 1
#define LOGE(tag, fmt, ...) APP_LOG_("E", tag, fmt, ##__VA_ARGS__)
#else
#define LOGE(tag, fmt, ...) ((void)0)
#endif
#if APP_LOG_LEVEL >= 2
#define LOGW(tag, fmt, ...) APP_LOG_("W", tag, fmt, ##__VA_ARGS__)
#else
#define LOGW(tag, fmt, ...) ((void)0)
#endif
#if APP_LOG_LEVEL >= 3
#define LOGI(tag, fmt, ...) APP_LOG_("I", tag, fmt, ##__VA_ARGS__)
#else
#define LOGI(tag, fmt, ...) ((void)0)
#endif
#if APP_LOG_LEVEL >= 4
#define LOGD(tag, fmt, ...) APP_LOG_("D", tag, fmt, ##__VA_ARGS__)
#else
#define LOGD(tag, fmt, ...) ((void)0)
#endif
