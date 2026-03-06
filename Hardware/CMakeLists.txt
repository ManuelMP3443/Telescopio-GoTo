cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(PICO_BOARD pico_w)

# Este es el "interruptor maestro" que arregla el error NO_SYS=1
set(PICO_NO_FREERTOS 1) 

add_compile_definitions(MEM_LIBC_MALLOC=0)

project(Horizonte_Hardware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(Horizonte_Hardware
    src/main.c
)

target_include_directories(Horizonte_Hardware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Esta es la configuración CORRECTA
# Ambas librerías respetarán PICO_NO_FREERTOS
target_link_libraries(Horizonte_Hardware PRIVATE
    pico_stdlib
    hardware_i2c 
    hardware_pwm 
    pico_cyw43_arch_lwip_threadsafe_background
    pico_btstack_base                                 # BTstack base (IMPORTANTE)
    pico_btstack_ble                             # BLE
    pico_btstack_cyw43  
)

target_link_directories(Horizonte_Hardware PRIVATE
    ${PICO_SDK_PATH}/lib/btstack/src
)

target_include_directories(Horizonte_Hardware
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
)

pico_enable_stdio_usb(Horizonte_Hardware 1)
pico_enable_stdio_uart(Horizonte_Hardware 0)
pico_add_extra_outputs(Horizonte_Hardware)