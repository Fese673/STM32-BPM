#ifndef DEBUG_DEBUG_H
#define DEBUG_DEBUG_H

#include "stm32f1xx_hal.h"

/*
 * One place to disable all debug output in the future.
 * Set APP_DEBUG_ENABLED to 0U and all DEBUG_* calls become no-ops.
 */
#ifndef APP_DEBUG_ENABLED
#define APP_DEBUG_ENABLED 1U
#endif

#ifndef APP_DEBUG_I2C_SCAN_ENABLED
#define APP_DEBUG_I2C_SCAN_ENABLED 1U
#endif

#ifndef APP_DEBUG_UART_TIMEOUT_MS
#define APP_DEBUG_UART_TIMEOUT_MS 50U
#endif

#ifndef APP_DEBUG_I2C_SCAN_DELAY_MS
#define APP_DEBUG_I2C_SCAN_DELAY_MS 5000U
#endif

#ifndef APP_DEBUG_I2C_SCAN_TRIALS
#define APP_DEBUG_I2C_SCAN_TRIALS 2U
#endif

#ifndef APP_DEBUG_I2C_PROBE_TIMEOUT_MS
#define APP_DEBUG_I2C_PROBE_TIMEOUT_MS 5U
#endif

#if APP_DEBUG_ENABLED
#define DEBUG_LOG(...) Debug_Log(__VA_ARGS__)
#define DEBUG_LOG_LINE(text) Debug_LogLine(text)
#define DEBUG_LOG_SECTION(title) Debug_LogSection(title)
#else
#define DEBUG_LOG(...) ((void)0)
#define DEBUG_LOG_LINE(text) ((void)0)
#define DEBUG_LOG_SECTION(title) ((void)0)
#endif

void Debug_Init(void);
void Debug_QuickI2CProbe(void);
void Debug_Process(void);
void Debug_Log(const char *format, ...);
void Debug_LogLine(const char *text);
void Debug_LogSection(const char *title);

#endif /* DEBUG_DEBUG_H */