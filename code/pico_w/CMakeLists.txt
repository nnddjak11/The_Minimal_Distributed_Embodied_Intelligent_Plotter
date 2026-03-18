### 注意不要用pico SDK默认的wifi库，因为我这是国产版的缝合ESP8285

# CMakeLists.txt 完整内容（Pico W 适配版）
cmake_minimum_required(VERSION 3.13)

# 引入 Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# 项目名称（和编译错误中的 pico_esp8285_udp_draw 对应）
project(pico_esp8285_udp_draw C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 初始化 Pico SDK
pico_sdk_init()

# 核心：将 main.c 加入编译目标（必须包含！）
add_executable(${PROJECT_NAME}
    main.c
)

# 链接 Pico 核心库（UART/GPIO/stdlib 依赖）
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_uart
    hardware_gpio
)

# 启用 USB 串口（Pico W 推荐），禁用 UART 串口（避免冲突）
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# 生成 UF2 文件（烧录用）
pico_add_extra_outputs(${PROJECT_NAME})
