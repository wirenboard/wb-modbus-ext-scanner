#pragma once

#include <stdint.h>

uint16_t modbus_crc_iv(uint16_t iv, const void * data, uint32_t size_bytes);

static inline uint16_t modbus_crc(const void * data, uint32_t size_bytes)
{
    return modbus_crc_iv(0xFFFF, data, size_bytes);
}
