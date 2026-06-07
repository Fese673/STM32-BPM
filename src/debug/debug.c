#include "debug/debug.h"
#include "uart1.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;

#if APP_DEBUG_ENABLED

#define DEBUG_TX_BUFFER_SIZE 192U

typedef enum
{
    DEBUG_SCAN_IDLE = 0U,
    DEBUG_SCAN_ACTIVE,
    DEBUG_SCAN_DONE
} DebugScanState_t;

typedef enum
{
    DEBUG_SCAN_BUS_NONE = 0U,
    DEBUG_SCAN_BUS_I2C1,
    DEBUG_SCAN_BUS_I2C2
} DebugScanBus_t;

static DebugScanState_t debug_scan_state = DEBUG_SCAN_IDLE;
static DebugScanBus_t debug_scan_bus = DEBUG_SCAN_BUS_NONE;
static I2C_HandleTypeDef *debug_scan_i2c = NULL;
static const char *debug_scan_bus_name = NULL;
static uint8_t debug_scan_address = 0x08U;
static uint16_t debug_scan_found_count = 0U;
static uint32_t debug_scan_start_tick = 0U;

static const char *Debug_FindDeviceName(DebugScanBus_t bus, uint8_t address7)
{
    if (bus == DEBUG_SCAN_BUS_I2C1)
    {
        if (address7 == 0x3CU)
        {
            return "OLED SSD1306";
        }

        if (address7 == 0x3DU)
        {
            return "OLED SSD1306 (alt addr)";
        }
    }

    if (bus == DEBUG_SCAN_BUS_I2C2 && address7 == 0x57U)
    {
        return "MAX30102";
    }

    return NULL;
}

static void Debug_StartBusScan(DebugScanBus_t bus, I2C_HandleTypeDef *hi2c, const char *bus_name)
{
    debug_scan_bus = bus;
    debug_scan_i2c = hi2c;
    debug_scan_bus_name = bus_name;
    debug_scan_address = 0x08U;
    debug_scan_found_count = 0U;

    if (bus_name != NULL)
    {
        DEBUG_LOG("[%s] scan start: 0x08-0x77\r\n", bus_name);
    }
}

static void Debug_ScanStep(void)
{
    if (debug_scan_i2c == NULL || debug_scan_bus_name == NULL)
    {
        return;
    }

    if (HAL_I2C_GetState(debug_scan_i2c) != HAL_I2C_STATE_READY)
    {
        return;
    }

    if (debug_scan_address <= 0x77U)
    {
        if (HAL_I2C_IsDeviceReady(debug_scan_i2c, (uint16_t)(debug_scan_address << 1), APP_DEBUG_I2C_SCAN_TRIALS, APP_DEBUG_I2C_PROBE_TIMEOUT_MS) == HAL_OK)
        {
            const char *device_name = Debug_FindDeviceName(debug_scan_bus, debug_scan_address);

            if (device_name != NULL)
            {
                DEBUG_LOG("[%s] found 0x%02X %s\r\n", debug_scan_bus_name, debug_scan_address, device_name);
            }
            else
            {
                DEBUG_LOG("[%s] found 0x%02X\r\n", debug_scan_bus_name, debug_scan_address);
            }

            ++debug_scan_found_count;
        }

        ++debug_scan_address;
        return;
    }

    DEBUG_LOG("[%s] total: %u device(s)\r\n", debug_scan_bus_name, (unsigned int)debug_scan_found_count);

    if (debug_scan_bus == DEBUG_SCAN_BUS_I2C1)
    {
        Debug_StartBusScan(DEBUG_SCAN_BUS_I2C2, &hi2c2, "I2C2");
        return;
    }

    debug_scan_state = DEBUG_SCAN_DONE;
    DEBUG_LOG_SECTION("END DEBUG");
    DEBUG_LOG("[DEBUG] I2C scan completed in %lu ms\r\n", (unsigned long)(HAL_GetTick() - debug_scan_start_tick));
}

static void Debug_WriteRaw(const char *text, uint16_t length)
{
    if (text == NULL || length == 0U)
    {
        return;
    }

    (void)uart1_write((const uint8_t *)text, length);
}

void Debug_Init(void)
{
    debug_scan_state = DEBUG_SCAN_IDLE;
    debug_scan_bus = DEBUG_SCAN_BUS_NONE;
    debug_scan_i2c = NULL;
    debug_scan_bus_name = NULL;
    debug_scan_address = 0x08U;
    debug_scan_found_count = 0U;
    debug_scan_start_tick = 0U;
}

void Debug_QuickI2CProbe(void)
{
    if (APP_DEBUG_I2C_SCAN_ENABLED == 0U)
    {
        return;
    }

    HAL_StatusTypeDef probe_status_3c;
    HAL_StatusTypeDef probe_status_3d;

    DEBUG_LOG_SECTION("I2C QUICK PROBE");

    if (HAL_I2C_GetState(&hi2c1) == HAL_I2C_STATE_READY)
    {
        probe_status_3c = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(0x3CU << 1), APP_DEBUG_I2C_SCAN_TRIALS, APP_DEBUG_I2C_PROBE_TIMEOUT_MS);
        if (probe_status_3c == HAL_OK)
        {
            DEBUG_LOG("[I2C1] found 0x3C OLED SSD1306\r\n");
        }
        else
        {
            probe_status_3d = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(0x3DU << 1), APP_DEBUG_I2C_SCAN_TRIALS, APP_DEBUG_I2C_PROBE_TIMEOUT_MS);
            if (probe_status_3d == HAL_OK)
            {
                DEBUG_LOG("[I2C1] found 0x3D OLED SSD1306 (alt addr)\r\n");
            }
            else
            {
                DEBUG_LOG("[I2C1] OLED not ready on 0x3C/0x3D (3C=%d, 3D=%d, err=0x%08lX)\r\n",
                          (int)probe_status_3c,
                          (int)probe_status_3d,
                          (unsigned long)HAL_I2C_GetError(&hi2c1));
            }
        }
    }
    else
    {
        DEBUG_LOG("[I2C1] bus not ready for OLED probe\r\n");
    }

    DEBUG_LOG_SECTION("END QUICK PROBE");
}

void Debug_Log(const char *format, ...)
{
    char buffer[DEBUG_TX_BUFFER_SIZE];
    va_list args;
    int written;
    size_t length;

    if (format == NULL)
    {
        return;
    }

    va_start(args, format);
    written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written <= 0)
    {
        return;
    }

    length = (size_t)written;
    if (length >= sizeof(buffer))
    {
        length = sizeof(buffer) - 1U;
    }

    Debug_WriteRaw(buffer, (uint16_t)length);
}

void Debug_LogLine(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    DEBUG_LOG("%s\r\n", text);
}

void Debug_LogSection(const char *title)
{
    if (title == NULL)
    {
        return;
    }

    DEBUG_LOG("===== %s =====\r\n", title);
}

void Debug_Process(void)
{
    if (debug_scan_state == DEBUG_SCAN_DONE)
    {
        return;
    }

    if (debug_scan_state == DEBUG_SCAN_IDLE)
    {
        if (HAL_GetTick() < APP_DEBUG_I2C_SCAN_DELAY_MS)
        {
            return;
        }

        debug_scan_start_tick = HAL_GetTick();
        debug_scan_state = DEBUG_SCAN_ACTIVE;
        DEBUG_LOG_SECTION("I2C STARTUP TEST");
        Debug_StartBusScan(DEBUG_SCAN_BUS_I2C1, &hi2c1, "I2C1");
        return;
    }

#if APP_DEBUG_I2C_SCAN_ENABLED
    Debug_ScanStep();
#else
    debug_scan_state = DEBUG_SCAN_DONE;
#endif
}

#else

void Debug_Init(void)
{
}

void Debug_Process(void)
{
}

void Debug_Log(const char *format, ...)
{
    (void)format;
}

void Debug_LogLine(const char *text)
{
    (void)text;
}

void Debug_LogSection(const char *title)
{
    (void)title;
}

#endif