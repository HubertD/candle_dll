#pragma once

#include "candle_defs.h"

enum {
    CANDLE_DEVMODE_RESET = 0,
    CANDLE_DEVMODE_START = 1
};

bool candle_ctrl_set_host_format(candle_device_t *dev);
bool candle_ctrl_set_timestamp_mode(candle_device_t *dev, bool enable_timestamps);
bool candle_ctrl_set_device_mode(candle_device_t *dev, uint8_t channel, uint32_t mode, uint32_t flags);
bool candle_ctrl_get_config(candle_device_t *dev, candle_device_config_t *dconf);
bool candle_ctrl_get_capability(candle_device_t *dev, uint8_t channel, candle_capability_t *data);
bool candle_ctrl_set_bittiming(candle_device_t *dev, uint8_t channel, candle_bittiming_t *data);
