#ifndef COMMS_BUS_H
#define COMMS_BUS_H

/*
 * Terminal-to-terminal messaging.
 *
 * The three control terminals (Entrance, Room 1, Room 2) run as separate
 * FreeRTOS tasks. Each terminal owns an inbox queue; nothing else touches
 * another terminal's variables. This keeps the design the same as three
 * separate ESP32 boards linked by a network: only messages cross the
 * boundary.
 *
 *   Room  -> Entrance : ROOM_STATE (heartbeat every 2 s), ROOM_EVENT (seq no.)
 *   Entrance -> Room  : ROOM_CMD (light on/off/auto)
 *   Entrance -> all   : BUILDING (open, emergency, people inside), retained
 *
 * "Retained" works like an MQTT retained message: the latest BUILDING
 * message is kept under a mutex so a room that (re)starts reads it at once.
 */
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

typedef enum { NODE_ENTRANCE = 0, NODE_ROOM1 = 1, NODE_ROOM2 = 2, NODE_COUNT = 3 } bus_node_t;

typedef enum { MSG_ROOM_STATE = 1, MSG_ROOM_EVENT, MSG_ROOM_CMD, MSG_BUILDING } bus_kind_t;

typedef struct {
    bool occupied;
    uint8_t level;      /* % */
    uint8_t mode;       /* room_mode_t */
    int lux;
    double wh, baseline_wh;
    uint32_t on_s;
} bus_room_state_t;

typedef struct {
    uint32_t seq;       /* per-room sequence: gaps reveal lost events */
    char type[12];
    char detail[48];
} bus_room_event_t;

#define BUS_NO_CHANGE 0xFF
typedef struct {
    uint8_t mode;       /* room_mode_t to apply, BUS_NO_CHANGE to keep   */
    uint16_t vacancy_s; /* new vacancy timeout, 0 to keep                */
    uint16_t pause_s;   /* fault drill: stop heartbeats for this long    */
} bus_room_cmd_t;

typedef struct {
    bool open;
    bool emergency;
    int inside;
    bool inside_known;
    uint32_t schedule_version;
} bus_building_t;

typedef struct {
    bus_kind_t kind;
    uint8_t src;
    int64_t sent_ms;
    union {
        bus_room_state_t state;
        bus_room_event_t event;
        bus_room_cmd_t cmd;
        bus_building_t building;
    } u;
} bus_msg_t;

bool bus_init(void);
/* Non-blocking by default: a full inbox counts as a drop, never a stall. */
bool bus_send(bus_node_t dst, const bus_msg_t *msg, TickType_t wait);
bool bus_receive(bus_node_t node, bus_msg_t *out, TickType_t wait);
/* Store as retained and push to both rooms. */
void bus_publish_building(const bus_building_t *b);
bus_building_t bus_get_building(void);
uint32_t bus_drops(bus_node_t node);

#endif
