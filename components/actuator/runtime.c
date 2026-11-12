#include "actuator/runtime.h"

#include <stdint.h>
#include "common/config.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* LEDC plan: low-speed timer 0 (50 Hz) servos on channels 0 and 1,
   high-speed timer 0 buzzer on channel 0. Room lights use low-speed
   timer 1 on channels 2 and 3 (rooms component). */
#define ACT_SERVO_TIMER LEDC_TIMER_0
#define ACT_BUZZ_TIMER LEDC_TIMER_0

static const ledc_channel_t ACT_SERVO_CH[2] = { LEDC_CHANNEL_0, LEDC_CHANNEL_1 };
static esp_timer_handle_t act_door_timer[2];
static volatile bool act_door_state[2];
static volatile bool act_emergency_on, act_alarm_on;
static QueueHandle_t act_beep_q;

static void act_servo(scms_point_t door, int deg)
{
    /* 50 Hz, 14-bit: 0.5 ms (0 deg) .. 2.5 ms (180 deg) */
    uint32_t us = 500u + (uint32_t)deg * 2000u / 180u;
    uint32_t duty = us * 16384u / 20000u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ACT_SERVO_CH[door], duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ACT_SERVO_CH[door]);
}

static void act_door_close_cb(void *arg)
{
    scms_point_t door = (scms_point_t)(intptr_t)arg;
    if (act_emergency_on) return;          /* doors stay open in emergency */
    act_servo(door, 0);
    act_door_state[door] = false;
}

void actuator_open_door(scms_point_t door)
{
    act_servo(door, 90);
    act_door_state[door] = true;
    esp_timer_stop(act_door_timer[door]);
    esp_timer_start_once(act_door_timer[door], DOOR_OPEN_MS * 1000ULL);
}

void actuator_close_all(void)
{
    if (act_emergency_on) return;
    for (int d = 0; d < 2; d++) {
        esp_timer_stop(act_door_timer[d]);
        act_servo((scms_point_t)d, 0);
        act_door_state[d] = false;
    }
}

void actuator_emergency(bool on)
{
    act_emergency_on = on;
    for (int d = 0; d < 2; d++) {
        esp_timer_stop(act_door_timer[d]);
        act_servo((scms_point_t)d, on ? 90 : 0);
        act_door_state[d] = on;
    }
}

bool actuator_door_open(scms_point_t door) { return act_door_state[door]; }

static void act_tone(int hz)
{
    static int current;
    if (hz > 0 && hz != current) {
        ledc_set_freq(LEDC_HIGH_SPEED_MODE, ACT_BUZZ_TIMER, (uint32_t)hz);
        current = hz;
    }
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, hz > 0 ? 512 : 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

static void act_buzzer_task(void *arg)
{
    (void)arg;
    actuator_beep_t b;
    for (;;) {
        if (act_alarm_on || act_emergency_on) {    /* two-tone siren */
            act_tone(1800); vTaskDelay(pdMS_TO_TICKS(250));
            act_tone(900);  vTaskDelay(pdMS_TO_TICKS(250));
            xQueueReset(act_beep_q);
            continue;
        }
        act_tone(0);
        if (xQueueReceive(act_beep_q, &b, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        switch (b) {
        case BEEP_OK: act_tone(2000); vTaskDelay(pdMS_TO_TICKS(120)); break;
        case BEEP_KEY: act_tone(2500); vTaskDelay(pdMS_TO_TICKS(30)); break;
        case BEEP_DENY:
            for (int i = 0; i < 2; i++) {
                act_tone(400); vTaskDelay(pdMS_TO_TICKS(180));
                act_tone(0);   vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;
        case BEEP_ALERT:
            for (int i = 0; i < 4; i++) {
                act_tone(1500); vTaskDelay(pdMS_TO_TICKS(100));
                act_tone(0);    vTaskDelay(pdMS_TO_TICKS(80));
            }
            break;
        }
        act_tone(0);
    }
}

void actuator_beep(actuator_beep_t kind)
{
    if (act_beep_q) xQueueSend(act_beep_q, &kind, 0);
}

void actuator_alarm(bool on) { act_alarm_on = on; }
bool actuator_alarm_active(void) { return act_alarm_on; }

esp_err_t actuator_start(void)
{
    ledc_timer_config_t st = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = ACT_SERVO_TIMER,
        .duty_resolution = LEDC_TIMER_14_BIT, .freq_hz = 50, .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&st);
    if (e != ESP_OK) return e;
    const int pins[2] = { PIN_SERVO_ENTRY, PIN_SERVO_EXIT };
    for (int d = 0; d < 2; d++) {
        ledc_channel_config_t c = {
            .gpio_num = pins[d], .speed_mode = LEDC_LOW_SPEED_MODE, .channel = ACT_SERVO_CH[d],
            .timer_sel = ACT_SERVO_TIMER, .duty = 0,
        };
        if ((e = ledc_channel_config(&c)) != ESP_OK) return e;
        act_servo((scms_point_t)d, 0);
        esp_timer_create_args_t ta = {
            .callback = act_door_close_cb, .arg = (void *)(intptr_t)d, .name = "door",
        };
        if ((e = esp_timer_create(&ta, &act_door_timer[d])) != ESP_OK) return e;
    }

    ledc_timer_config_t bt = {
        .speed_mode = LEDC_HIGH_SPEED_MODE, .timer_num = ACT_BUZZ_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK,
    };
    if ((e = ledc_timer_config(&bt)) != ESP_OK) return e;
    ledc_channel_config_t bc = {
        .gpio_num = PIN_BUZZER, .speed_mode = LEDC_HIGH_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = ACT_BUZZ_TIMER, .duty = 0,
    };
    if ((e = ledc_channel_config(&bc)) != ESP_OK) return e;

    act_beep_q = xQueueCreate(6, sizeof(actuator_beep_t));
    if (!act_beep_q) return ESP_ERR_NO_MEM;
    return xTaskCreate(act_buzzer_task, "buzzer", 2560, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
