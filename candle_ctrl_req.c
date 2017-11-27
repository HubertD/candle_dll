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

#include "candle_ctrl_req.h"
#include "ch_9.h"

enum {
    CANDLE_BREQ_HOST_FORMAT = 0,
    CANDLE_BREQ_BITTIMING,
    CANDLE_BREQ_MODE,
    CANDLE_BREQ_BERR,
    CANDLE_BREQ_BT_CONST,
    CANDLE_BREQ_DEVICE_CONFIG,
    CANDLE_TIMESTAMP_GET = 0x40,
    CANDLE_TIMESTAMP_ENABLE = 0x41,
};


static bool usb_control_msg(WINUSB_INTERFACE_HANDLE hnd, uint8_t request, uint8_t requesttype, uint16_t value, uint16_t index, void *data, uint16_t size)
{
    WINUSB_SETUP_PACKET packet;
    memset(&packet, 0, sizeof(packet));

    packet.Request = request;
    packet.RequestType = requesttype;
    packet.Value = value;
    packet.Index = index;
    packet.Length = size;

    unsigned long bytes_sent = 0;

	bool rc = WinUsb_ControlTransfer(hnd, packet, (uint8_t*)data, size, &bytes_sent, 0);

	static DWORD last_error = 0;
	if (!rc){
		last_error = GetLastError();
	}
	return rc;
}

bool candle_ctrl_set_host_format(candle_device_t *dev)
{
    candle_host_config_t hconf;
    hconf.byte_order = 0x0000beef;

    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_BREQ_HOST_FORMAT,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        dev->interfaceNumber,
        &hconf,
        sizeof(hconf)
    );

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_SET_HOST_FORMAT;
    return rc;
}

bool candle_ctrl_set_timestamp_mode(candle_device_t *dev, bool enable_timestamps)
{
    uint32_t ts_config = enable_timestamps ? 1 : 0;

    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_TIMESTAMP_ENABLE,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        ts_config,
        NULL,
        0
    );

	static DWORD last_windows_error;
	if (!rc){
		last_windows_error = GetLastError();
	}

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_SET_TIMESTAMP_MODE;
    return rc;
}

bool candle_ctrl_set_device_mode(candle_device_t *dev, uint8_t channel, uint32_t mode, candle_device_mode_flags_t device_mode_flags)
{
    candle_device_mode_t dm;
    dm.mode = mode;
    dm.flags = device_mode_flags;

    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_BREQ_MODE,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        channel,
        dev->interfaceNumber,
        &dm,
        sizeof(dm)
    );

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_SET_DEVICE_MODE;
    return rc;
}


bool candle_ctrl_get_config(candle_device_t *dev, candle_device_config_t *dconf)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_BREQ_DEVICE_CONFIG,
        USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        dev->interfaceNumber,
        dconf,
        sizeof(*dconf)
    );

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_GET_DEVICE_INFO;
    return rc;
}

bool candle_ctrl_get_timestamp(candle_device_t *dev, uint32_t *current_timestamp)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_TIMESTAMP_GET,
        USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        dev->interfaceNumber,
        current_timestamp,
        sizeof(*current_timestamp)
    );

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_GET_TIMESTAMP;
    return rc;
}

bool candle_ctrl_get_capability(candle_device_t *dev, uint8_t channel, candle_capability_t *data)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_BREQ_BT_CONST,
        USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        channel,
        0,
        data,
        sizeof(*data)
    );

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_GET_BITTIMING_CONST;
    return rc;
}

bool candle_ctrl_set_bittiming(candle_device_t *dev, uint8_t channel, candle_bittiming_t *data)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLE_BREQ_BITTIMING,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        channel,
        0,
        data,
        sizeof(*data)
    );

    dev->last_error = rc ? CANDLE_ERR_OK : CANDLE_ERR_SET_BITTIMING;
    return rc;
}
