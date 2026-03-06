
// clang-format off
// src/horizonte_gatt.c generated from src/horizonte.gatt for BTstack
// it needs to be regenerated when the .gatt file is updated. 

// To generate src/horizonte_gatt.c:
// C:\Users\manue\pico-sdk\lib\btstack\tool\compile_gatt.py src/horizonte.gatt src/horizonte_gatt.c

// att db format version 1

// binary attribute representation:
// - size in bytes (16), flags(16), handle (16), uuid (16/128), value(...)

#include <stdint.h>

// Reference: https://en.cppreference.com/w/cpp/feature_test
#if __cplusplus >= 200704L
constexpr
#endif
const uint8_t profile_data[] =
{
    // ATT DB Version
    1,

    // horizonte.gatt (Corregido con permisos para 'ANYBODY')
    //
    // Servicio FFE0
    // 0x0001 PRIMARY_SERVICE-0000ffe0-0000-1000-8000-00805f9b34fb
    0x18, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe0, 0xff, 0x00, 0x00, 
    // Característica FFE2
    // Propiedades:
    // - READ, WRITE, NOTIFY, WRITE_WITHOUT_RESPONSE
    //
    // Permisos:
    // - READ_PERMISSION_BIT_0: Cualquiera puede leer
    // - WRITE_PERMISSION_BIT_0: Cualquiera puede escribir
    //
    // 0x0002 CHARACTERISTIC-0000ffe2-0000-1000-8000-00805f9b34fb - READ | WRITE | NOTIFY | WRITE_WITHOUT_RESPONSE
    0x1b, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x1e, 0x03, 0x00, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xff, 0x00, 0x00, 
    // 0x0003 VALUE CHARACTERISTIC-0000ffe2-0000-1000-8000-00805f9b34fb - READ | WRITE | NOTIFY | WRITE_WITHOUT_RESPONSE -'READ_PERMISSION_BIT_0 | WRITE_PERMISSION_BIT_0, '
    // READ_ANYBODY, WRITE_ANYBODY
    0x46, 0x00, 0x0e, 0x02, 0x03, 0x00, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xff, 0x00, 0x00, 0x52, 0x45, 0x41, 0x44, 0x5f, 0x50, 0x45, 0x52, 0x4d, 0x49, 0x53, 0x53, 0x49, 0x4f, 0x4e, 0x5f, 0x42, 0x49, 0x54, 0x5f, 0x30, 0x20, 0x7c, 0x20, 0x57, 0x52, 0x49, 0x54, 0x45, 0x5f, 0x50, 0x45, 0x52, 0x4d, 0x49, 0x53, 0x53, 0x49, 0x4f, 0x4e, 0x5f, 0x42, 0x49, 0x54, 0x5f, 0x30, 0x2c, 0x20, 
    // 0x0004 CLIENT_CHARACTERISTIC_CONFIGURATION
    // READ_ANYBODY, WRITE_ANYBODY
    0x0a, 0x00, 0x0e, 0x01, 0x04, 0x00, 0x02, 0x29, 0x00, 0x00, 
    // END
    0x00, 0x00, 
}; // total size 53 bytes 


//
// list service handle ranges
//
#define ATT_SERVICE_0000ffe0_0000_1000_8000_00805f9b34fb_START_HANDLE 0x0001
#define ATT_SERVICE_0000ffe0_0000_1000_8000_00805f9b34fb_END_HANDLE 0x0004
#define ATT_SERVICE_0000ffe0_0000_1000_8000_00805f9b34fb_01_START_HANDLE 0x0001
#define ATT_SERVICE_0000ffe0_0000_1000_8000_00805f9b34fb_01_END_HANDLE 0x0004

//
// list mapping between characteristics and handles
//
#define ATT_CHARACTERISTIC_0000ffe2_0000_1000_8000_00805f9b34fb__VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_0000ffe2_0000_1000_8000_00805f9b34fb__CLIENT_CONFIGURATION_HANDLE 0x0004
