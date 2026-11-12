#include "comms/bus.h"

#include <string.h>
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static QueueHandle_t bus_inbox[NODE_COUNT];
static SemaphoreHandle_t bus_mtx;
static bus_building_t bus_retained = { .open = true };
static uint32_t bus_dropped[NODE_COUNT];

bool bus_init(void)
{
    bus_mtx = xSemaphoreCreateMutex();
    bus_inbox[NODE_ENTRANCE] = xQueueCreate(24, sizeof(bus_msg_t));
    bus_inbox[NODE_ROOM1] = xQueueCreate(8, sizeof(bus_msg_t));
    bus_inbox[NODE_ROOM2] = xQueueCreate(8, sizeof(bus_msg_t));
    return bus_mtx && bus_inbox[0] && bus_inbox[1] && bus_inbox[2];
}

bool bus_send(bus_node_t dst, const bus_msg_t *msg, TickType_t wait)
{
    if (dst >= NODE_COUNT || !bus_inbox[dst]) return false;
    bus_msg_t m = *msg;
    m.sent_ms = esp_timer_get_time() / 1000;
    if (xQueueSend(bus_inbox[dst], &m, wait) == pdTRUE) return true;
    xSemaphoreTake(bus_mtx, portMAX_DELAY);
    bus_dropped[dst]++;
    xSemaphoreGive(bus_mtx);
    return false;
}

bool bus_receive(bus_node_t node, bus_msg_t *out, TickType_t wait)
{
    return node < NODE_COUNT && bus_inbox[node] && xQueueReceive(bus_inbox[node], out, wait) == pdTRUE;
}

void bus_publish_building(const bus_building_t *b)
{
    xSemaphoreTake(bus_mtx, portMAX_DELAY);
    bus_retained = *b;
    xSemaphoreGive(bus_mtx);
    bus_msg_t m = { .kind = MSG_BUILDING, .src = NODE_ENTRANCE, .u.building = *b };
    bus_send(NODE_ROOM1, &m, 0);
    bus_send(NODE_ROOM2, &m, 0);
}

bus_building_t bus_get_building(void)
{
    xSemaphoreTake(bus_mtx, portMAX_DELAY);
    bus_building_t b = bus_retained;
    xSemaphoreGive(bus_mtx);
    return b;
}

uint32_t bus_drops(bus_node_t node)
{
    if (node >= NODE_COUNT) return 0;
    xSemaphoreTake(bus_mtx, portMAX_DELAY);
    uint32_t d = bus_dropped[node];
    xSemaphoreGive(bus_mtx);
    return d;
}
