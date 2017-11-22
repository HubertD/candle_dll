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

#include "candle.h"
#include <stdlib.h>

#include "candle_defs.h"
#include "candle_ctrl_req.h"
#include "ch_9.h"

static bool candle_read_di(HDEVINFO hdi, SP_DEVICE_INTERFACE_DATA interfaceData, candle_device_t *dev)
{
    /* get required length first (this call always fails with an error) */
    ULONG requiredLength=0;
    SetupDiGetDeviceInterfaceDetail(hdi, &interfaceData, NULL, 0, &requiredLength, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        dev->last_error = CANDLE_ERR_SETUPDI_IF_DETAILS;
        return false;
    }

    PSP_DEVICE_INTERFACE_DETAIL_DATA detail_data =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LMEM_FIXED, requiredLength);

    if (detail_data != NULL) {
        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    } else {
        dev->last_error = CANDLE_ERR_MALLOC;
        return false;
    }

    bool retval = true;
    ULONG length = requiredLength;
    if (!SetupDiGetDeviceInterfaceDetail(hdi, &interfaceData, detail_data, length, &requiredLength, NULL) ) {
        dev->last_error = CANDLE_ERR_SETUPDI_IF_DETAILS2;
        retval = false;
    } else if (FAILED(StringCchCopy(dev->path, sizeof(dev->path), detail_data->DevicePath))) {
        dev->last_error = CANDLE_ERR_PATH_LEN;
        retval = false;
    }

    LocalFree(detail_data);

    if (!retval) {
        return false;
    }

    /* try to open to read device infos and see if it is avail */
    if (candle_dev_open(dev)) {
        dev->state = CANDLE_DEVSTATE_AVAIL;
        candle_dev_close(dev);
    } else {
        dev->state = CANDLE_DEVSTATE_INUSE;
    }

    dev->last_error = CANDLE_ERR_OK;
    return true;
}

bool __stdcall candle_list_scan(candle_list_handle *list)
{
    if (list==NULL) {
        return false;
    }

    candle_list_t *l = (candle_list_t *)calloc(1, sizeof(candle_list_t));
    *list = l;
    if (l==NULL) {
        return false;
    }

    GUID guid;
    if (CLSIDFromString(L"{c15b4308-04d3-11e6-b3ea-6057189e6443}", &guid) != NOERROR) {
        l->last_error = CANDLE_ERR_CLSID;
        return false;
    }

    HDEVINFO hdi = SetupDiGetClassDevs(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdi == INVALID_HANDLE_VALUE) {
        l->last_error = CANDLE_ERR_GET_DEVICES;
        return false;
    }

    bool rv = false;
    for (unsigned i=0; i<CANDLE_MAX_DEVICES; i++) {

        SP_DEVICE_INTERFACE_DATA interfaceData;
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(hdi, NULL, &guid, i, &interfaceData)) {

            if (!candle_read_di(hdi, interfaceData, &l->dev[i])) {
                l->last_error = l->dev[i].last_error;
                rv = false;
                break;
            }

        } else {

            DWORD err = GetLastError();
            if (err==ERROR_NO_MORE_ITEMS) {
                l->num_devices = i;
                l->last_error = CANDLE_ERR_OK;
                rv = true;
            } else {
                l->last_error = CANDLE_ERR_SETUPDI_IF_ENUM;
                rv = false;
            }
            break;

        }

    }

    SetupDiDestroyDeviceInfoList(hdi);

    return rv;

}

DLL bool __stdcall candle_list_free(candle_list_handle list)
{
    free(list);
    return true;
}

DLL bool __stdcall candle_list_length(candle_list_handle list, uint8_t *len)
{
	candle_list_t *l = (candle_list_t *)list;
	*len = l->num_devices;
    return true;
}

DLL bool __stdcall candle_dev_get(candle_list_handle list, uint8_t dev_num, candle_handle *hdev)
{
    candle_list_t *l = (candle_list_t *)list;
    if (l==NULL) {
        return false;
    }

    if (dev_num >= CANDLE_MAX_DEVICES) {
        l->last_error = CANDLE_ERR_DEV_OUT_OF_RANGE;
        return false;
    }

    candle_device_t *dev = calloc(1, sizeof(candle_device_t));
    *hdev = dev;
    if (dev==NULL) {
        l->last_error = CANDLE_ERR_MALLOC;
        return false;
    }

    memcpy(dev, &l->dev[dev_num], sizeof(candle_device_t));
    l->last_error = CANDLE_ERR_OK;
    dev->last_error = CANDLE_ERR_OK;
    return true;
}


DLL bool __stdcall candle_dev_get_state(candle_handle hdev, candle_devstate_t *state)
{
    if (hdev==NULL) {
        return false;
    } else {
        candle_device_t *dev = (candle_device_t*)hdev;
        *state = dev->state;
        return true;
    }
}

DLL wchar_t * __stdcall candle_dev_get_path(candle_handle hdev)
{
    if (hdev==NULL) {
        return NULL;
    } else {
        candle_device_t *dev = (candle_device_t*)hdev;
        return dev->path;
    }
}

static bool candle_dev_internal_open(candle_handle hdev)
{
    candle_device_t *dev = (candle_device_t*)hdev;

	memset(dev->txevents, 0, sizeof(dev->txevents));
	memset(dev->txurbs, 0, sizeof(dev->txurbs));
	dev->txurbs_in_use = 0;

	memset(dev->rxevents, 0, sizeof(dev->rxevents));
	memset(dev->rxurbs, 0, sizeof(dev->rxurbs));

	dev->device_mode_flags = 0;

	dev->deviceHandle = CreateFile(
        dev->path,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (dev->deviceHandle == INVALID_HANDLE_VALUE) {
        dev->last_error = CANDLE_ERR_CREATE_FILE;
        return false;
    }

    if (!WinUsb_Initialize(dev->deviceHandle, &dev->winUSBHandle)) {
        dev->last_error = CANDLE_ERR_WINUSB_INITIALIZE;
        goto close_handle;
    }

	USB_INTERFACE_DESCRIPTOR ifaceDescriptor;
    if (!WinUsb_QueryInterfaceSettings(dev->winUSBHandle, 0, &ifaceDescriptor)) {
        dev->last_error = CANDLE_ERR_QUERY_INTERFACE;
        goto winusb_free;
    }

    dev->interfaceNumber = ifaceDescriptor.bInterfaceNumber;
    unsigned pipes_found = 0;

    for (uint8_t i=0; i<ifaceDescriptor.bNumEndpoints; i++) {

        WINUSB_PIPE_INFORMATION pipeInfo;
        if (!WinUsb_QueryPipe(dev->winUSBHandle, 0, i, &pipeInfo)) {
            dev->last_error = CANDLE_ERR_QUERY_PIPE;
            goto winusb_free;
        }

        if (pipeInfo.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_IN(pipeInfo.PipeId)) {
            dev->bulkInPipe = pipeInfo.PipeId;
			dev->bulkInPipeInfo = pipeInfo;
			pipes_found++;
        } else if (pipeInfo.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_OUT(pipeInfo.PipeId)) {
            dev->bulkOutPipe = pipeInfo.PipeId;
			dev->bulkOutPipeInfo = pipeInfo;
			pipes_found++;
        } else {
            dev->last_error = CANDLE_ERR_PARSE_IF_DESCR;
            goto winusb_free;
        }

    }

    if (pipes_found != 2) {
        dev->last_error = CANDLE_ERR_PARSE_IF_DESCR;
        goto winusb_free;
    }

	ULONG param = 1;
	if (!WinUsb_SetPipePolicy(dev->winUSBHandle, dev->bulkInPipe, RAW_IO, sizeof(param), &param)) {
		dev->last_error = CANDLE_ERR_SET_PIPE_RAW_IO;
		goto winusb_free;
	}

	if (!candle_ctrl_set_host_format(dev)) {
        goto winusb_free;
    }

    if (!candle_ctrl_get_config(dev, &dev->dconf)) {
        goto winusb_free;
    }

    if (!candle_ctrl_get_capability(dev, 0, &dev->bt_const)) {
        dev->last_error = CANDLE_ERR_GET_BITTIMING_CONST;
        goto winusb_free;
    }

    dev->last_error = CANDLE_ERR_OK;
    return true;

winusb_free:
    WinUsb_Free(dev->winUSBHandle);
	dev->winUSBHandle = NULL;  // Null usb handle so we don't free it twice

close_handle:
    CloseHandle(dev->deviceHandle);
	dev->deviceHandle = NULL; // Null device handle so we don't free it twice
    return false;

}

static bool candle_prepare_read(candle_device_t *dev, unsigned urb_num)
{
	bool rc = WinUsb_ReadPipe(
		dev->winUSBHandle,
		dev->bulkInPipe,
		dev->rxurbs[urb_num].buf,
		dev->bulkInPipeInfo.MaximumPacketSize,
		NULL,
		&dev->rxurbs[urb_num].ovl
		);

	if (rc || (GetLastError() != ERROR_IO_PENDING)) {
		dev->last_error = CANDLE_ERR_PREPARE_READ;
		return false;
	}
	else {
		dev->last_error = CANDLE_ERR_OK;
		return true;
	}
}

// Close urbs correctly or we get stuck in CloseHandle(dev->deviceHandle)
static void close_urb(candle_device_t *dev,candle_tx_rx_urb *urb)
{
	DWORD result = CancelIoEx(dev->deviceHandle, &urb->ovl);
	if (result || GetLastError() != ERROR_NOT_FOUND){
		DWORD number_of_bytes;
		result = GetOverlappedResult(dev->deviceHandle, &urb->ovl, &number_of_bytes, TRUE);
	}
}

static bool candle_close_tx_rx_urbs(candle_device_t *dev)
{
	for (unsigned i = 0; i<CANDLE_URB_COUNT; i++) {
		if (dev->txevents[i] != NULL) {
			close_urb(dev, &dev->txurbs[i]);
			CloseHandle(dev->txevents[i]);
		}
		if (dev->rxevents[i] != NULL) {
			close_urb(dev, &dev->rxurbs[i]);
			CloseHandle(dev->rxevents[i]);
		}
	}
    return true;
}


DLL bool __stdcall candle_dev_open(candle_handle hdev)
{
    candle_device_t *dev = (candle_device_t*)hdev;

    if (candle_dev_internal_open(dev)) {
		for (unsigned i = 0; i<CANDLE_URB_COUNT; i++) {
			HANDLE ev = CreateEvent(NULL, true, false, NULL);
			dev->txevents[i] = ev;
			dev->txurbs[i].ovl.hEvent = ev;
		}
		for (unsigned i = 0; i<CANDLE_URB_COUNT; i++) {
			HANDLE ev = CreateEvent(NULL, true, false, NULL);
			dev->rxevents[i] = ev;
			dev->rxurbs[i].ovl.hEvent = ev;
			if (!candle_prepare_read(dev, i)) {
				candle_close_tx_rx_urbs(dev);
				return false; // keep last_error from prepare_read call
			}
		}
		dev->last_error = CANDLE_ERR_OK;
        return true;
    } else {
        return false; // keep last_error from open_device call
    }

}

DLL bool __stdcall candle_dev_get_timestamp_us(candle_handle hdev, uint32_t *timestamp_us)
{
	return candle_ctrl_get_timestamp(hdev, timestamp_us);
}

DLL bool __stdcall candle_dev_close(candle_handle hdev)
{
    candle_device_t *dev = (candle_device_t*)hdev;

	if (!dev)
		return true;

    candle_close_tx_rx_urbs(dev);
	
	// Check handle to see if it is initialized before we free it
	if (dev->winUSBHandle)
		WinUsb_Free(dev->winUSBHandle);
    dev->winUSBHandle = NULL;

	// Check handle to see if it is initialized before we free it
	if (dev->deviceHandle)
		CloseHandle(dev->deviceHandle);
    dev->deviceHandle = NULL;

    dev->last_error = CANDLE_ERR_OK;
    return true;
}

DLL bool __stdcall candle_dev_free(candle_handle hdev)
{
    free(hdev);
    return true;
}

candle_err_t DLL __stdcall candle_dev_last_error(candle_handle hdev)
{
    candle_device_t *dev = (candle_device_t*)hdev;
    return dev->last_error;
}

DLL bool __stdcall candle_channel_count(candle_handle hdev, uint8_t *num_channels)
{
    // TODO check if info was already read from device; try to do so; throw error...
    candle_device_t *dev = (candle_device_t*)hdev;
    *num_channels = dev->dconf.icount+1;
    return true;
}

DLL bool __stdcall candle_channel_get_capabilities(candle_handle hdev, uint8_t ch, candle_capability_t *cap)
{
    // TODO check if info was already read from device; try to do so; throw error...
    candle_device_t *dev = (candle_device_t*)hdev;
    memcpy(cap, &dev->bt_const, sizeof(candle_capability_t));
    return true;
}

DLL bool __stdcall candle_channel_set_timing(candle_handle hdev, uint8_t ch, candle_bittiming_t *data)
{
    // TODO ensure device is open, check channel count..
    candle_device_t *dev = (candle_device_t*)hdev;
    return candle_ctrl_set_bittiming(dev, ch, data);
}

DLL bool __stdcall candle_channel_set_bitrate(candle_handle hdev, uint8_t ch, uint32_t bitrate)
{
    // TODO ensure device is open, check channel count..
    candle_device_t *dev = (candle_device_t*)hdev;

    if (dev->bt_const.fclk_can != 48000000) {
        /* this function only works for the candleLight base clock of 48MHz */
        dev->last_error = CANDLE_ERR_BITRATE_FCLK;
        return false;
    }

    candle_bittiming_t t;
    t.prop_seg = 1;
    t.sjw = 1;
    t.phase_seg1 = 13 - t.prop_seg;
    t.phase_seg2 = 2;

    switch (bitrate) {
        case 10000:
            t.brp = 300;
            break;

        case 20000:
            t.brp = 150;
            break;

        case 50000:
            t.brp = 60;
            break;

        case 83333:
            t.brp = 36;
            break;

        case 100000:
            t.brp = 30;
            break;

        case 125000:
            t.brp = 24;
            break;

        case 250000:
            t.brp = 12;
            break;

        case 500000:
            t.brp = 6;
            break;

        case 800000:
            t.brp = 4;
            t.phase_seg1 = 12 - t.prop_seg;
            t.phase_seg2 = 2;
            break;

        case 1000000:
            t.brp = 3;
            break;

        default:
            dev->last_error = CANDLE_ERR_BITRATE_UNSUPPORTED;
            return false;
    }

    return candle_ctrl_set_bittiming(dev, ch, &t);
}

DLL bool __stdcall candle_channel_start(candle_handle hdev, uint8_t ch, candle_device_mode_flags_t device_mode_flags)
{
    // TODO ensure device is open, check channel count..
    candle_device_t *dev = (candle_device_t*)hdev;

	// Make sure we have the features we are trying to use
	if (device_mode_flags & ~dev->bt_const.feature){
		dev->last_error = CANDLE_ERR_DEVICE_FEATURE_UNAVAILABLE;
		return false;
	}

	bool rval = candle_ctrl_set_device_mode(dev, ch, CANDLE_DEVMODE_START, device_mode_flags);

	if (rval)
		dev->device_mode_flags = device_mode_flags;

	return rval;
}

DLL bool __stdcall candle_channel_stop(candle_handle hdev, uint8_t ch)
{
    // TODO ensure device is open, check channel count..
    candle_device_t *dev = (candle_device_t*)hdev;
    return candle_ctrl_set_device_mode(dev, ch, CANDLE_DEVMODE_RESET, 0);
}

static bool check_write_result(candle_device_t *dev, uint32_t urb_num)
{
	bool rc;
	DWORD bytes_transferred;

	rc = WinUsb_GetOverlappedResult(dev->winUSBHandle, &dev->txurbs[urb_num].ovl, &bytes_transferred, true);
	if (!rc){
		dev->last_error = CANDLE_ERR_WRITE_RESULT;
		return false;
	}

	if (bytes_transferred != dev->txurbs[urb_num].send_size){
		dev->last_error = CANDLE_ERR_WRITE_SIZE;
		return false;
	}

	return true;
}

static DWORD find_free_txurb(candle_device_t *dev, uint32_t timeout)
{
	DWORD urb_num;

	if (dev->txurbs_in_use == 0){
		dev->txurbs_in_use++;
		return 0;
	}

	DWORD wait_result = WaitForMultipleObjects(CANDLE_URB_COUNT, dev->txevents, false, 0);
	if (wait_result == WAIT_TIMEOUT) {
		if (dev->txurbs_in_use < CANDLE_URB_COUNT){
			dev->txurbs_in_use++;
			return dev->txurbs_in_use - 1;
		}
		wait_result = WaitForMultipleObjects(CANDLE_URB_COUNT, dev->txevents, false, timeout);
		if (wait_result == WAIT_TIMEOUT) {
			dev->last_error = CANDLE_ERR_WRITE_TIMEOUT;
			return 0xffffffff;
		}
	}

	if ((wait_result < WAIT_OBJECT_0) || (wait_result >= WAIT_OBJECT_0 + CANDLE_URB_COUNT)) {
		dev->last_error = CANDLE_ERR_WRITE_WAIT;
		return 0xffffffff;
	}

	urb_num = wait_result - WAIT_OBJECT_0;

	if (!check_write_result(dev, urb_num)){
		return 0xffffffff;
	}

	return urb_num;
}

DLL bool __stdcall candle_frame_send(candle_handle hdev, uint8_t ch, candle_frame_t *frame,bool wait_send, uint32_t timeout_ms)
{
    // TODO ensure device is open, check channel count..
    candle_device_t *dev = (candle_device_t*)hdev;

    unsigned long bytes_sent = 0;

    frame->echo_id = 0;
    frame->channel = ch;

	DWORD urb_num;
	urb_num = find_free_txurb(dev, timeout_ms);

	if (urb_num == 0xffffffff)
		return false;

	uint32_t send_size = sizeof(*frame);

	dev->txurbs[urb_num].send_size = send_size;

	memcpy(dev->txurbs[urb_num].buf, frame, send_size);

	bool rc = WinUsb_WritePipe(
		dev->winUSBHandle,
		dev->bulkOutPipe,
		dev->txurbs[urb_num].buf,
		send_size,
		&bytes_sent,
		&dev->txurbs[urb_num].ovl
    );

	DWORD err = GetLastError();

	// Overlapped write result returns false with error code ERROR_IO_PENDING even if everything is OK
	if (err == ERROR_IO_PENDING)
		rc = true;

	if (!rc){
		dev->last_error = CANDLE_ERR_SEND_FRAME;
		return rc;
	}

	if (wait_send)
	{
		DWORD result = WaitForSingleObject(dev->txevents[urb_num], timeout_ms);
		if (result == WAIT_TIMEOUT) {
			dev->last_error = CANDLE_ERR_WRITE_TIMEOUT;
			return false;
		}
		if (result != WAIT_OBJECT_0)
		{
			dev->last_error = CANDLE_ERR_WRITE_WAIT;
			return false;
		}

		return check_write_result(dev, urb_num);
	}

    return rc;
}

DLL bool __stdcall candle_frame_read(candle_handle hdev, candle_frame_t *frame, uint32_t timeout_ms)
{
    // TODO ensure device is open..
    candle_device_t *dev = (candle_device_t*)hdev;

	DWORD wait_result = WaitForMultipleObjects(CANDLE_URB_COUNT, dev->rxevents, false, timeout_ms);
	if (wait_result == WAIT_TIMEOUT) {
		dev->last_error = CANDLE_ERR_READ_TIMEOUT;
		return false;
	}

	if ((wait_result < WAIT_OBJECT_0) || (wait_result >= WAIT_OBJECT_0 + CANDLE_URB_COUNT)) {
		dev->last_error = CANDLE_ERR_READ_WAIT;
		return false;
	}

	DWORD urb_num = wait_result - WAIT_OBJECT_0;
	DWORD bytes_transfered;
	DWORD last_error;

    if (!WinUsb_GetOverlappedResult(dev->winUSBHandle, &dev->rxurbs[urb_num].ovl, &bytes_transfered, false)) {
		last_error = GetLastError();
        candle_prepare_read(dev, urb_num);
        dev->last_error = CANDLE_ERR_READ_RESULT;
        return false;
    }

#if 0
	// Check bytes transferred for size of frame and size of frame with no timestamp
	if (bytes_transfered != sizeof(*frame) && bytes_transfered != (sizeof(*frame) - 4)) {
        candle_prepare_read(dev, urb_num);
        dev->last_error = CANDLE_ERR_READ_SIZE;
        return false;
    }
#endif

	memset(frame, 0, sizeof(*frame));

    memcpy(frame, dev->rxurbs[urb_num].buf, bytes_transfered > sizeof(*frame) ? sizeof(*frame) : bytes_transfered);

    return candle_prepare_read(dev, urb_num);
}

candle_frametype_t DLL __stdcall candle_frame_type(candle_frame_t *frame)
{
    if (frame->echo_id != 0xFFFFFFFF) {
        return CANDLE_FRAMETYPE_ECHO;
    };

    if (frame->can_id & 0x20000000) {
        return CANDLE_FRAMETYPE_ERROR;
    }

    return CANDLE_FRAMETYPE_RECEIVE;
}

uint32_t DLL __stdcall candle_frame_id(candle_frame_t *frame)
{
    return frame->can_id & 0x1FFFFFFF;
}

DLL bool __stdcall candle_frame_is_extended_id(candle_frame_t *frame)
{
    return (frame->can_id & 0x80000000) != 0;
}

DLL bool __stdcall candle_frame_is_rtr(candle_frame_t *frame)
{
    return (frame->can_id & 0x40000000) != 0;
}

DLL uint8_t __stdcall candle_frame_dlc(candle_frame_t *frame)
{
    return frame->can_dlc;
}

DLL uint8_t * __stdcall candle_frame_data(candle_frame_t *frame)
{
    return frame->data;
}

DLL uint32_t __stdcall candle_frame_timestamp_us(candle_frame_t *frame)
{
    return frame->timestamp_us;
}

static const char *err_text[]={
	"CANDLE_ERR_OK",
	"CANDLE_ERR_CREATE_FILE",
	"CANDLE_ERR_WINUSB_INITIALIZE",
	"CANDLE_ERR_QUERY_INTERFACE",
	"CANDLE_ERR_QUERY_PIPE",
	"CANDLE_ERR_PARSE_IF_DESCR",
	"CANDLE_ERR_SET_HOST_FORMAT",
	"CANDLE_ERR_GET_DEVICE_INFO",
	"CANDLE_ERR_GET_BITTIMING_CONST",
	"CANDLE_ERR_PREPARE_READ",
	"CANDLE_ERR_SET_DEVICE_MODE",
	"CANDLE_ERR_SET_BITTIMING",
	"CANDLE_ERR_BITRATE_FCLK",
	"CANDLE_ERR_BITRATE_UNSUPPORTED",
	"CANDLE_ERR_SEND_FRAME",
	"CANDLE_ERR_READ_TIMEOUT",
	"CANDLE_ERR_READ_WAIT",
	"CANDLE_ERR_READ_RESULT",
	"CANDLE_ERR_READ_SIZE",
	"CANDLE_ERR_SETUPDI_IF_DETAILS",
	"CANDLE_ERR_SETUPDI_IF_DETAILS2",
	"CANDLE_ERR_MALLOC",
	"CANDLE_ERR_PATH_LEN",
	"CANDLE_ERR_CLSID",
	"CANDLE_ERR_GET_DEVICES",
	"CANDLE_ERR_SETUPDI_IF_ENUM",
	"CANDLE_ERR_SET_TIMESTAMP_MODE",
	"CANDLE_ERR_DEV_OUT_OF_RANGE",
	"CANDLE_ERR_GET_TIMESTAMP",
	"CANDLE_ERR_SET_PIPE_RAW_IO",
	"CANDLE_ERR_DEVICE_CAPABILITY_UNAVAILABLE",
	"CANDLE_ERR_WRITE_WAIT",
	"CANDLE_ERR_WRITE_TIMEOUT",
	"CANDLE_ERR_WRITE_RESULT",
	"CANDLE_ERR_WRITE_SIZE",
};

DLL const char * __stdcall candle_error_text(candle_err_t errnum)
{
  if(errnum >= sizeof(err_text)/sizeof(err_text[0]))
    return("CANDLE_ERR_INVALID_ERROR_CODE");

  return err_text[errnum];
}

DLL candle_err_t __stdcall candle_init_single_device(uint8_t device_num, uint8_t device_channel, uint32_t bitrate, candle_device_mode_flags_t device_mode_flags, candle_list_handle *plist, candle_handle *phdev)
{
	candle_err_t rval;
	uint8_t len;
	candle_devstate_t state;
	uint8_t num_channels;
	candle_capability_t cap;

	if (!plist || !phdev)
		return CANDLE_ERR_MALLOC;

	*plist = NULL;
	*phdev = NULL;

	if (!candle_list_scan(plist)){
		goto done;
	}

	if (!candle_list_length(*plist, &len)){
		goto done;
	}

	if (!candle_dev_get(*plist, device_num, phdev)){
		goto done;
	}

	if (!candle_dev_open(*phdev)){
		goto done;
	}

	if (!candle_dev_get_state(*phdev, &state)){
		goto done;
	}

	if (state != CANDLE_DEVSTATE_AVAIL){
		goto done;
	}

	if (!candle_channel_count(*phdev, &num_channels)){
		goto done;
	}

	if (num_channels <= device_channel){
		goto done;
	}

	if (!candle_channel_get_capabilities(*phdev, device_channel, &cap)){
		goto done;
	}

	if (!candle_channel_set_bitrate(*phdev, device_channel, bitrate))
	{
		goto done;
	}

	if (!candle_channel_start(*phdev, device_channel,device_mode_flags)){
		goto done;
	}

done:

	{
		candle_list_t *l;
		candle_device_t *d;
		l = *plist;
		d = *phdev;

		if (!*plist)
			return CANDLE_ERR_MALLOC;

		if (!d){
			rval = l->last_error;
			candle_list_free(*plist);
			*plist = NULL;
			return rval;
		}

		rval = d->last_error;
		if (rval != CANDLE_ERR_OK){
			candle_dev_close(d);
			candle_dev_free(d);
			candle_list_free(l);
			*phdev = NULL;
			*plist = NULL;
		}

		return rval;
	}
}
