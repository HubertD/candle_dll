/*

  Copyright (c) 2016 Hubert Denkmair <hubert@denkmair.de>

  This file is part of the candle windows API.
  
  This library is free software: you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.
 
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with this library.  If not, see <http://www.gnu.org/licenses/>.

*/

#pragma once

#include "candle_defs.h"

enum {
    CANDLE_DEVMODE_RESET = 0,
    CANDLE_DEVMODE_START = 1
};

bool candle_ctrl_set_host_format(candle_device_t *dev);
bool candle_ctrl_set_timestamp_mode(candle_device_t *dev, bool enable_timestamps);
bool candle_ctrl_set_device_mode(candle_device_t *dev, uint8_t channel, uint32_t mode,candle_device_mode_flags_t device_mode_flags );
bool candle_ctrl_get_config(candle_device_t *dev, candle_device_config_t *dconf);
bool candle_ctrl_get_capability(candle_device_t *dev, uint8_t channel, candle_capability_t *data);
bool candle_ctrl_set_bittiming(candle_device_t *dev, uint8_t channel, candle_bittiming_t *data);
bool candle_ctrl_get_timestamp(candle_device_t *dev, uint32_t *current_timestamp);

