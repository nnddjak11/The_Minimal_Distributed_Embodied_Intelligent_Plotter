#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "driver/gpio.h"
#include "driver/i2s.h"

// ===================== 核心配置（聚焦2秒录音） =====================
// WiFi 配置
#define WIFI_SSID               "CMCC-adWg"    
#define WIFI_PASS               "QEDyKSp*"     
#define WIFI_STA_IP             "192.168.1.102"
#define WIFI_STA_GW             "192.168.1.1"
#define WIFI_STA_NETMASK        "255.255.255.0"

// UDP 配置
#define UDP_SERVER_IP           "192.168.1.101"
#define UDP_SERVER_PORT         8888

// 音频核心配置（时间导向）
#define RECORD_DURATION_MS      2000            // 强制录音2秒（核心！）
#define SAMPLE_RATE             16000           // 16K采样率
#define BITS_PER_SAMPLE         16              // 16位
#define CHANNELS                1               // 单声道
#define WAKEUP_THRESHOLD        550
#define DETECT_BUF_LEN          512

// 推导值（无需修改）
#define BYTES_PER_SAMPLE        (BITS_PER_SAMPLE / 8)
#define TOTAL_SAMPLES           (SAMPLE_RATE * RECORD_DURATION_MS / 1000)
#define TOTAL_BYTES             (TOTAL_SAMPLES * BYTES_PER_SAMPLE * CHANNELS) // 64000字节

// INMP441 引脚
#define I2S_PORT_NUM            I2S_NUM_0
#define I2S_SCK_PIN             18
#define I2S_WS_PIN              19
#define I2S_SD_PIN              20

// 日志标签
static const char *TAG = "ESP32_AUDIO_2S";

// 全局变量
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT      BIT0
static int sock_fd = -1;
static bool is_recording = false;

// ===================== WiFi 初始化 =====================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi断连，重试连接...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi连接成功，IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));
    esp_netif_ip_info_t ip_info;
    inet_pton(AF_INET, WIFI_STA_IP, &ip_info.ip);
    inet_pton(AF_INET, WIFI_STA_GW, &ip_info.gw);
    inet_pton(AF_INET, WIFI_STA_NETMASK, &ip_info.netmask);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.capable = true, .required = false},
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

// ===================== INMP441 初始化 =====================
static esp_err_t i2s_mic_init(void)
{
    ESP_LOGI(TAG, "初始化INMP441，录音配置：%d秒/%dK采样率", RECORD_DURATION_MS/1000, SAMPLE_RATE/1000);

    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    esp_err_t ret = i2s_driver_install(I2S_PORT_NUM, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "安装I2S驱动失败: %d", ret);
        return ret;
    }

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };
    ret = i2s_set_pin(I2S_PORT_NUM, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置I2S引脚失败: %d", ret);
        i2s_driver_uninstall(I2S_PORT_NUM);
        return ret;
    }

    ESP_LOGI(TAG, "INMP441初始化成功");
    return ESP_OK;
}

// ===================== UDP 初始化（阻塞模式） =====================
static int udp_client_init(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "创建UDP套接字失败: %d", errno);
        return -1;
    }

    // 阻塞模式：确保2秒音频完整发送
    sock_fd = sock;
    ESP_LOGI(TAG, "UDP客户端初始化成功（阻塞模式）");
    return sock;
}

// ===================== 新增：UDP分批发送函数 =====================
static int udp_send_batched(int sock, const void *buf, size_t total_len, 
                           const struct sockaddr *dest, socklen_t dest_len)
{
    const uint8_t *data = (const uint8_t *)buf;
    size_t sent = 0;
    const size_t batch_size = 1400; // 关键：单包1400字节，避免分片

    while (sent < total_len) {
        size_t send_size = (total_len - sent) > batch_size ? batch_size : (total_len - sent);
        int ret = sendto(sock, data + sent, send_size, 0, dest, dest_len);
        
        if (ret <= 0) {
            ESP_LOGE(TAG, "分批发送失败：第%d包，错误码%d", (int)(sent/batch_size) + 1, errno);
            return sent;
        }

        sent += ret;
        vTaskDelay(pdMS_TO_TICKS(1)); // 核心：每包延时1ms，防止接收端缓冲区溢出
    }

    return sent;
}

// ===================== 音频能量计算 =====================
static int16_t calculate_audio_energy(int16_t *buf, size_t len)
{
    if (len == 0 || buf == NULL) return 0;
    int32_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += abs(buf[i]);
    return (int16_t)(sum / len);
}

// ===================== 核心：2秒录音任务（时间导向） =====================
static void audio_record_task(void *arg)
{
    // 申请2秒音频缓存
    int16_t *audio_buf = (int16_t *)malloc(TOTAL_BYTES);
    if (audio_buf == NULL) {
        ESP_LOGE(TAG, "申请%d字节缓存失败", TOTAL_BYTES);
        vTaskDelete(NULL);
        return;
    }

    int16_t detect_buf[DETECT_BUF_LEN];
    size_t bytes_read = 0;
    bool wakeup_locked = false;
    TickType_t wakeup_lock_time = 0;

    // UDP目标地址
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_SERVER_PORT);
    inet_pton(AF_INET, UDP_SERVER_IP, &dest_addr.sin_addr);

    ESP_LOGI(TAG, "等待唤醒（阈值: %d），触发后强制录音%d秒", WAKEUP_THRESHOLD, RECORD_DURATION_MS/1000);

    while (1) {
        // 唤醒冷却（避免重复触发）
        if (wakeup_locked && xTaskGetTickCount() - wakeup_lock_time < pdMS_TO_TICKS(3000)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        } else if (wakeup_locked) {
            wakeup_locked = false;
        }

        // 1. 唤醒检测（仅解锁时）
        if (!is_recording && !wakeup_locked) {
            if (i2s_read(I2S_PORT_NUM, detect_buf, sizeof(detect_buf), &bytes_read, pdMS_TO_TICKS(100)) == ESP_OK && bytes_read > 0) {
                int16_t energy = calculate_audio_energy(detect_buf, bytes_read / sizeof(int16_t));
                if (energy > WAKEUP_THRESHOLD) {
                    ESP_LOGI(TAG, "✅ 检测到唤醒（能量: %d），开始录制%d秒音频...", energy, RECORD_DURATION_MS/1000);
                    is_recording = true;
                    wakeup_locked = true;
                    wakeup_lock_time = xTaskGetTickCount();
                    memset(audio_buf, 0, TOTAL_BYTES); // 清空缓存
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 2. 强制录制2秒音频（核心逻辑：按时间，不是字节）
        if (is_recording) {
            TickType_t record_start = xTaskGetTickCount();
            size_t total_read = 0;
            ESP_LOGI(TAG, "开始计时录音，目标时长：%dms", RECORD_DURATION_MS);

            // 录音循环：直到录满2秒
            while (xTaskGetTickCount() - record_start < pdMS_TO_TICKS(RECORD_DURATION_MS)) {
                // 每次读取1024字节，避免单次读取过大
                size_t read_size = (TOTAL_BYTES - total_read) > 1024 ? 1024 : (TOTAL_BYTES - total_read);
                esp_err_t ret = i2s_read(I2S_PORT_NUM, 
                                        audio_buf + (total_read / sizeof(int16_t)), 
                                        read_size, 
                                        &bytes_read, 
                                        pdMS_TO_TICKS(100));

                if (ret == ESP_OK && bytes_read > 0) {
                    total_read += bytes_read;
                }

                // 避免CPU占用过高
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            // 3. 录音结束：补零到2秒对应的字节数（兜底）
            if (total_read < TOTAL_BYTES) {
                ESP_LOGW(TAG, "实际读取%d字节，补零到%d字节（2秒）", total_read, TOTAL_BYTES);
                memset(audio_buf + (total_read / sizeof(int16_t)), 0, TOTAL_BYTES - total_read);
            } else {
                ESP_LOGI(TAG, "2秒录音完成，读取%d字节", total_read);
            }


            // 4. UDP分批发送完整2秒音频（替换原一次性sendto）
            int send_len = udp_send_batched(sock_fd, 
                                   audio_buf, 
                                   TOTAL_BYTES, 
                                   (struct sockaddr *)&dest_addr, 
                                   sizeof(dest_addr));

            if (send_len == TOTAL_BYTES) {
                ESP_LOGI(TAG, "✅ 2秒音频（%d字节）分批发送成功！", TOTAL_BYTES);
            } else {
                ESP_LOGE(TAG, "❌ 分批发送失败：仅发送%d字节（目标%d字节）", send_len, TOTAL_BYTES);
            }

            // 重置状态
            is_recording = false;
            ESP_LOGI(TAG, "回到等待唤醒状态（冷却3秒）...\n");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(audio_buf);
    vTaskDelete(NULL);
}

// ===================== 主函数 =====================
void app_main(void)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化WiFi
    wifi_init_sta();

    // 初始化INMP441
    ret = i2s_mic_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "INMP441初始化失败，程序退出");
        return;
    }

    // 初始化UDP
    if (udp_client_init() < 0) {
        ESP_LOGE(TAG, "UDP初始化失败，程序退出");
        return;
    }

    // 创建录音任务
    xTaskCreate(audio_record_task, "audio_task", 16384, NULL, 5, NULL);

    ESP_LOGI(TAG, "程序初始化完成，核心逻辑：触发唤醒→录制2秒音频→发送");
}
