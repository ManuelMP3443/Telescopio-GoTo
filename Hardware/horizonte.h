
// clang-format off
// horizonte.h generated from horizonte.gatt for BTstack
// it needs to be regenerated when the .gatt file is updated. 

// To generate horizonte.h:
// C:\Users\manue\pico\pico-examples\btstack\tool\compile_gatt.py horizonte.gatt horizonte.h

// att db format version 1

// binary attribute representation:
// - size in bytes (16), flags(16), handle (16), uuid (16/128), value(...)

#include <stdint.h>

// Reference: https://en.cppreference.com/w/cpp/feature_test
#if __cplusplus >= 200704L
constexpr
#endif
static const uint8_t profile_data[] =
{
    // ATT DB Version
    1,

    // 0x0001 PRIMARY_SERVICE-0000FFE0-0000-1000-8000-00805F9B34FB
    0x18, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe0, 0xff, 0x00, 0x00, 
    // 0x0002 CHARACTERISTIC-0000FFE2-0000-1000-8000-00805F9B34FB - DYNAMIC | READ | WRITE_WITHOUT_RESPONSE | NOTIFY
    0x1b, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x16, 0x03, 0x00, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xff, 0x00, 0x00, 
    // 0x0003 VALUE CHARACTERISTIC-0000FFE2-0000-1000-8000-00805F9B34FB - DYNAMIC | READ | WRITE_WITHOUT_RESPONSE | NOTIFY
    // READ_ANYBODY, WRITE_ANYBODY
    0x16, 0x00, 0x06, 0x03, 0x03, 0x00, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xff, 0x00, 0x00, 
    // 0x0004 CLIENT_CHARACTERISTIC_CONFIGURATION
    // READ_ANYBODY, WRITE_ANYBODY
    0x0a, 0x00, 0x0e, 0x01, 0x04, 0x00, 0x02, 0x29, 0x00, 0x00, 
    // END
    0x00, 0x00, 
}; // total size 53 bytes 


//
// list service handle ranges
//
#define ATT_SERVICE_0000FFE0_0000_1000_8000_00805F9B34FB_START_HANDLE 0x0001
#define ATT_SERVICE_0000FFE0_0000_1000_8000_00805F9B34FB_END_HANDLE 0x0004
#define ATT_SERVICE_0000FFE0_0000_1000_8000_00805F9B34FB_01_START_HANDLE 0x0001
#define ATT_SERVICE_0000FFE0_0000_1000_8000_00805F9B34FB_01_END_HANDLE 0x0004

//
// list mapping between characteristics and handles
//
#define ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_0000FFE2_0000_1000_8000_00805F9B34FB_01_CLIENT_CONFIGURATION_HANDLE 0x0004
