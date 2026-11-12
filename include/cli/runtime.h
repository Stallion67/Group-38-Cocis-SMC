#ifndef CLI_RUNTIME_H
#define CLI_RUNTIME_H

/* Serial management console (115200 baud). Type "help" for the menu. */
#include "esp_err.h"

esp_err_t cli_start(void);

#endif
