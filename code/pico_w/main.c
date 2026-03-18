#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include "pico/stdlib.h"

// ===================== 完全禁用 WiFi =====================
// 全程不初始化、不调用 WiFi，等于彻底关闭

// ===================== 只走 USB CDC 串口（Type-C）=====================
#define TOTAL_BYTES 240    // 你要的30组×8字节数据
#define GROUP_COUNT 30
#define BYTES_PER_GROUP 8

// 电机引脚（你自己的，不变）
#define M1_IN1 2
#define M1_IN2 3
#define M1_IN3 4
#define M1_IN4 5
#define M2_IN1 6
#define M2_IN2 7
#define M2_IN3 8
#define M2_IN4 9

#define STEPS_PER_CM 256
#define STEP_DELAY_MS 2

float current_l1 = 0.0f;
float current_l2 = 0.0f;
float L1_list[30] = {0};
float L2_list[30] = {0};
bool has_new_data = false;

// 步进电机相序
const uint8_t PHASE_CW[8][4] = {
    {1,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,1,0},
    {0,0,1,0},{0,0,1,1},{0,0,0,1},{1,0,0,1}
};
const uint8_t PHASE_CCW[8][4] = {
    {1,0,0,1},{0,0,0,1},{0,0,1,1},{0,0,1,0},
    {0,1,1,0},{0,1,0,0},{1,1,0,0},{1,0,0,0}
};

// 电机控制
void set_motor(uint8_t motor_num, const uint8_t *pin_states) {
    if (motor_num == 1) {
        gpio_put(M1_IN1, pin_states[0]);
        gpio_put(M1_IN2, pin_states[1]);
        gpio_put(M1_IN3, pin_states[2]);
        gpio_put(M1_IN4, pin_states[3]);
    } else {
        gpio_put(M2_IN1, pin_states[0]);
        gpio_put(M2_IN2, pin_states[1]);
        gpio_put(M2_IN3, pin_states[2]);
        gpio_put(M2_IN4, pin_states[3]);
    }
}

void move_motors(int steps1, const uint8_t phase1[8][4],
                 int steps2, const uint8_t phase2[8][4]) {
    int max_steps = (steps1 > steps2) ? steps1 : steps2;
    for (int i=0; i<max_steps; i++) {
        if (i < steps1) set_motor(1, phase1[i%8]);
        if (i < steps2) set_motor(2, phase2[i%8]);
        sleep_ms(STEP_DELAY_MS);
    }
    set_motor(1, (uint8_t[]){0,0,0,0});
    set_motor(2, (uint8_t[]){0,0,0,0});
}

// 解析从机顶盒USB发来的240字节二进制
void parse_binary(uint8_t *data) {
    for (int i=0; i<GROUP_COUNT; i++) {
        int offset = i * BYTES_PER_GROUP;
        memcpy(&L1_list[i], &data[offset], 4);
        memcpy(&L2_list[i], &data[offset+4], 4);
    }
    current_l1 = L1_list[0];
    current_l2 = L2_list[0];
    has_new_data = true;
}

// 从 USB 串口（Type-C）读满240字节
bool read_usb_serial(uint8_t *dst, int len) {
    memset(dst, 0, len);
    int recv = 0;
    absolute_time_t timeout = make_timeout_time_ms(3000);

    while (recv < len && !time_reached(timeout)) {
        int c = getchar_timeout_us(50000);
        if (c != PICO_ERROR_TIMEOUT) {
            dst[recv++] = c;
        }
    }
    return recv == len;
}

// 画画流程
void draw_path(void) {
    for (int i=0; i<GROUP_COUNT; i++) {
        float d1 = L1_list[i] - current_l1;
        float d2 = L2_list[i] - current_l2;
        int s1 = roundf(fabs(d1) * STEPS_PER_CM);
        int s2 = roundf(fabs(d2) * STEPS_PER_CM);

        const uint8_t (*p1)[4] = (d1 > 0) ? PHASE_CCW : PHASE_CW;
        const uint8_t (*p2)[4] = (d2 > 0) ? PHASE_CCW : PHASE_CW;

        if (s1 > 0 || s2 > 0) {
            move_motors(s1, p1, s2, p2);
        }

        current_l1 = L1_list[i];
        current_l2 = L2_list[i];
        sleep_ms(200);
    }
    has_new_data = false;
    sleep_ms(1000);
}

// ===================== 主函数 =====================
int main() {
    // 只初始化 USB 串口（Type-C），不初始化任何UART
    stdio_init_all();
    sleep_ms(1000);

    // 电机引脚初始化
    int pins[] = {M1_IN1,M1_IN2,M1_IN3,M1_IN4,M2_IN1,M2_IN2,M2_IN3,M2_IN4};
    for (int i=0; i<8; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }

    uint8_t buf[TOTAL_BYTES];

    while (1) {
        // 从机顶盒 USB 读数据
        if (read_usb_serial(buf, TOTAL_BYTES)) {
            parse_binary(buf);
        }

        if (has_new_data) {
            draw_path();
        }

        sleep_ms(10);
    }
}
