#include "rooms/runtime.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "comms/bus.h"
#include "common/config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rooms/core.h"

typedef struct {
    int room;                /* 1 or 2 */
    bus_node_t node;
    int pir_pin;
    int light_pin;
    ledc_channel_t channel;
    uint32_t seq;
} rm_ctx_t;

static rm_ctx_t rm_ctx[NUM_ROOMS] = {
    { 1, NODE_ROOM1, PIN_PIR_ROOM1, PIN_LIGHT_ROOM1, LEDC_CHANNEL_2, 0 },
    { 2, NODE_ROOM2, PIN_PIR_ROOM2, PIN_LIGHT_ROOM2, LEDC_CHANNEL_3, 0 },
};
static adc_oneshot_unit_handle_t rm_adc;
static SemaphoreHandle_t rm_adc_mtx;

static int64_t rm_now_ms(void) { return esp_timer_get_time() / 1000; }

/* Wokwi photoresistor module: AO voltage rises as the room gets darker.
   Convert with its divider (10k series, RL10 = 50k, gamma 0.7). */
static int rm_read_lux(void)
{
    int raw = 0;
    xSemaphoreTake(rm_adc_mtx, portMAX_DELAY);
    adc_oneshot_read(rm_adc, (adc_channel_t)LDR_ADC_CHANNEL, &raw);
    xSemaphoreGive(rm_adc_mtx);
    double v = raw / 4095.0 * 3.3;
    if (v < 0.01) v = 0.01;
    if (v > 3.29) v = 3.29;
    double r = 10000.0 * v / (3.3 - v);
    double lux = pow(50000.0 * pow(10, 0.7) / r, 1.0 / 0.7);
    return lux > 100000 ? 100000 : (int)lux;
}

static void rm_apply(const rm_ctx_t *c, int level)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, c->channel, (uint32_t)level * 1023u / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, c->channel);
}

static void rm_event(rm_ctx_t *c, const char *type, const char *fmt, ...)
{
    bus_msg_t m = { .kind = MSG_ROOM_EVENT, .src = (uint8_t)c->node };
    m.u.event.seq = ++c->seq;
    snprintf(m.u.event.type, sizeof m.u.event.type, "%s", type);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.u.event.detail, sizeof m.u.event.detail, fmt, ap);
    va_end(ap);
    bus_send(NODE_ENTRANCE, &m, pdMS_TO_TICKS(20));
}

static void rm_task(void *arg)
{
    rm_ctx_t *c = (rm_ctx_t *)arg;
    room_params_t p;
    room_core_t core;
    room_params_default(&p);
    room_core_init(&core, rm_now_ms());
    bus_building_t bld = bus_get_building();   /* retained state at start */
    int64_t last_beat = 0, pause_until = 0, last_intrusion = -60000;
    printf("[ROOM%d] terminal ready\n", c->room);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = rm_now_ms();

        /* 1. messages from the entrance */
        bus_msg_t m;
        while (bus_receive(c->node, &m, 0)) {
            if (m.kind == MSG_BUILDING) {
                bool em_changed = m.u.building.emergency != bld.emergency;
                bld = m.u.building;
                if (em_changed) rm_event(c, "EMERGENCY", bld.emergency ? "lights forced 100%%" : "cleared");
            } else if (m.kind == MSG_ROOM_CMD) {
                if (m.u.cmd.mode != BUS_NO_CHANGE && m.u.cmd.mode <= ROOM_MODE_FORCED_OFF) {
                    room_core_set_mode(&core, (room_mode_t)m.u.cmd.mode, now, &p);
                    rm_event(c, "REMOTE", "lights set to %s", room_mode_name(core.mode));
                }
                if (m.u.cmd.vacancy_s) {
                    p.vacant_after_s = m.u.cmd.vacancy_s;
                    rm_event(c, "CONFIG", "vacancy timeout %ds", p.vacant_after_s);
                }
                if (m.u.cmd.pause_s) {
                    pause_until = now + (int64_t)m.u.cmd.pause_s * 1000;
                    printf("[ROOM%d] fault drill: heartbeats stopped for %ds\n", c->room, m.u.cmd.pause_s);
                }
            }
        }

        /* 2. sensors and rules */
        room_inputs_t in = {
            .pir = gpio_get_level(c->pir_pin) == 1,
            .lux = rm_read_lux(),
            .emergency = bld.emergency,
            .building_open = bld.open,
            .building_inside = bld.inside,
            .inside_known = bld.inside_known,
            .now_ms = now,
        };
        room_events_t ev = room_core_step(&core, &in, &p);
        if (ev.level_changed) rm_apply(c, core.level);

        /* 3. events to the coordinator */
        if (ev.became_occupied) rm_event(c, "OCCUPIED", "motion, %d lux", in.lux);
        if (ev.became_vacant) rm_event(c, "VACANT", "no motion");
        if (ev.override_expired) rm_event(c, "OVERRIDE", "manual ON expired");
        if (ev.level_changed)
            rm_event(c, "LIGHT", "%d%% -> %d%% (%s)", ev.prev_level, core.level, room_mode_name(core.mode));
        if (ev.intrusion && now - last_intrusion > 30000) {
            last_intrusion = now;
            rm_event(c, "INTRUSION", "motion while building empty");
        }

        /* 4. heartbeat with the full room state */
        if (now - last_beat >= ROOM_HEARTBEAT_MS && now >= pause_until) {
            last_beat = now;
            bus_msg_t s = { .kind = MSG_ROOM_STATE, .src = (uint8_t)c->node };
            s.u.state.occupied = core.occupied;
            s.u.state.level = (uint8_t)core.level;
            s.u.state.mode = (uint8_t)core.mode;
            s.u.state.lux = in.lux;
            s.u.state.wh = core.wh;
            s.u.state.baseline_wh = core.baseline_wh;
            s.u.state.on_s = core.on_s;
            bus_send(NODE_ENTRANCE, &s, 0);
        }
    }
}

esp_err_t rooms_start(void)
{
    rm_adc_mtx = xSemaphoreCreateMutex();
    if (!rm_adc_mtx) return ESP_ERR_NO_MEM;
    adc_oneshot_unit_init_cfg_t ac = { .unit_id = ADC_UNIT_1 };
    esp_err_t e = adc_oneshot_new_unit(&ac, &rm_adc);
    if (e != ESP_OK) return e;
    adc_oneshot_chan_cfg_t ch = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if ((e = adc_oneshot_config_channel(rm_adc, (adc_channel_t)LDR_ADC_CHANNEL, &ch)) != ESP_OK) return e;

    gpio_config_t pir = {
        .pin_bit_mask = (1ULL << PIN_PIR_ROOM1) | (1ULL << PIN_PIR_ROOM2),
        .mode = GPIO_MODE_INPUT,
    };
    if ((e = gpio_config(&pir)) != ESP_OK) return e;

    ledc_timer_config_t lt = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    if ((e = ledc_timer_config(&lt)) != ESP_OK) return e;
    for (int i = 0; i < NUM_ROOMS; i++) {
        ledc_channel_config_t lc = {
            .gpio_num = rm_ctx[i].light_pin, .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = rm_ctx[i].channel, .timer_sel = LEDC_TIMER_1, .duty = 0,
        };
        if ((e = ledc_channel_config(&lc)) != ESP_OK) return e;
        char name[12];
        snprintf(name, sizeof name, "room%d", rm_ctx[i].room);
        if (xTaskCreate(rm_task, name, 3584, &rm_ctx[i], 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
