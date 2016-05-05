#pragma once

#include <stdint.h>
#include <windows.h>
#include <winbase.h>
#include <winusb.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>

#undef __CRT__NO_INLINE
#include <strsafe.h>
#define __CRT__NO_INLINE

#include "candle.h"

#define CANDLE_MAX_DEVICES 32
#define CANDLE_URB_COUNT 30

#pragma pack(push,1)

typedef struct {
    uint32_t byte_order;
} candle_host_config_t;

typedef struct {
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t icount;
    uint32_t sw_version;
    uint32_t hw_version;
} candle_device_config_t;

typedef struct {
    uint32_t mode;
    uint32_t flags;
} candle_device_mode_t;

#pragma pack(pop)


typedef struct {
    OVERLAPPED ovl;
    uint8_t buf[64];
} canlde_rx_urb;

typedef struct {
    wchar_t path[256];
    candle_devstate_t state;
    candle_err_t last_error;

    HANDLE deviceHandle;
    WINUSB_INTERFACE_HANDLE winUSBHandle;
    UCHAR interfaceNumber;
    UCHAR bulkInPipe;
    UCHAR bulkOutPipe;

    candle_device_config_t dconf;
    candle_capability_t bt_const;
    canlde_rx_urb rxurbs[CANDLE_URB_COUNT];
    HANDLE rxevents[CANDLE_URB_COUNT];
} candle_device_t;

typedef struct {
    uint8_t num_devices;
    candle_err_t last_error;
    candle_device_t dev[CANDLE_MAX_DEVICES];
} candle_list_t;
