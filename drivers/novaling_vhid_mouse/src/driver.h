/**
 * @file driver.h
 * @brief Internal definitions for the novaling KMDF + VHF virtual HID mouse driver.
 */
#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#include "../shared/novaling_mouse_protocol.h"

#define NOVALING_VHID_VENDOR_ID   0x1209u  // pid.codes community VID range
#define NOVALING_VHID_PRODUCT_ID  0x4E4Cu  // "NL" (novaling)
#define NOVALING_VHID_VERSION_NUM 0x0100u

#define NOVALING_WHEEL_DELTA      120

typedef struct _NOVALING_DEVICE_CONTEXT {
  WDFDEVICE Device;
  VHFHANDLE VhfHandle;
  WDFSPINLOCK StateLock;
  BOOLEAN VhfStarted;

  // Resolution Multiplier feature report state (Report ID 3)
  // Bits 0..1: vertical wheel (0 or 1), Bits 2..3: horizontal wheel (0 or 1)
  UCHAR ResolutionMultipliers;

  // Fallback sub-notch accumulators used only if OS has not enabled high-res wheel
  INT32 PendingVWheelDelta;
  INT32 PendingHWheelDelta;

  // Last reported button bitmasks per TLC so EvtFileClose can cleanly release held buttons
  UCHAR LastRelButtons;
  UCHAR LastAbsButtons;
  UINT16 LastAbsX;
  UINT16 LastAbsY;
} NOVALING_DEVICE_CONTEXT, *PNOVALING_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NOVALING_DEVICE_CONTEXT, NovalingGetDeviceContext)

// driver.c
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NovalingEvtDeviceAdd;

// device.c
NTSTATUS NovalingCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT NovalingEvtDeviceSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP NovalingEvtDeviceSelfManagedIoCleanup;
EVT_WDF_FILE_CLOSE NovalingEvtFileClose;

EVT_VHF_ASYNC_OPERATION NovalingVhfGetFeature;
EVT_VHF_ASYNC_OPERATION NovalingVhfSetFeature;

NTSTATUS NovalingSubmitRelativeReport(
  _In_ PNOVALING_DEVICE_CONTEXT Context,
  _In_ const NOVALING_MOUSE_REL_INPUT *Input
);

NTSTATUS NovalingSubmitAbsoluteReport(
  _In_ PNOVALING_DEVICE_CONTEXT Context,
  _In_ const NOVALING_MOUSE_ABS_INPUT *Input
);

NTSTATUS NovalingSubmitReset(_In_ PNOVALING_DEVICE_CONTEXT Context);

// queue.c
NTSTATUS NovalingQueueInitialize(_In_ WDFDEVICE Device);
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL NovalingEvtIoDeviceControl;
