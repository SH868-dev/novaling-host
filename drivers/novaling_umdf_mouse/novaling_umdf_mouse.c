/*
 * novaling_umdf_mouse.c
 *
 * Minimal UMDF 2.15 + Virtual HID Framework (VHF) virtual mouse driver.
 * Runs entirely in user mode (WUDFHost.exe) on top of Microsoft inbox
 * WUDFRd.sys + vhf.sys + hidclass.sys + mouhid.sys + mouclass.sys so a
 * headless Windows 10 host has an active HID-compliant mouse device without
 * requiring kernel-mode test signing (bcdedit /set testsigning off).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wdf.h>
#include <vhf.h>

typedef struct _DEVICE_CONTEXT {
  WDFIOTARGET VhfIoTarget;
  VHFHANDLE VhfHandle;
  BOOLEAN VhfStarted;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

/* Composite HID descriptor:
 *   Collection 1 (Report ID 1): Standard 5-button relative mouse with wheel -> activates mouse.sys
 *   Collection 2 (Report ID 2): Standard 104-key boot keyboard -> activates keyboard.sys
 * Having both TLCs guarantees interception_create_context() (which opens \\.\interception00..19)
 * succeeds on headless PCs with zero physical mouse/keyboard attached, while keeping testsigning=No.
 */
static UCHAR g_MouseReportDescriptor[] = {
  /* --- TLC 1: Mouse (Report ID 1) --- */
  0x05, 0x01,        /* Usage Page (Generic Desktop Controls) */
  0x09, 0x02,        /* Usage (Mouse) */
  0xA1, 0x01,        /* Collection (Application) */
  0x85, 0x01,        /*   Report ID (1) */
  0x09, 0x01,        /*   Usage (Pointer) */
  0xA1, 0x00,        /*   Collection (Physical) */
  0x05, 0x09,        /*     Usage Page (Button) */
  0x19, 0x01,        /*     Usage Minimum (Button 1) */
  0x29, 0x05,        /*     Usage Maximum (Button 5) */
  0x15, 0x00,        /*     Logical Minimum (0) */
  0x25, 0x01,        /*     Logical Maximum (1) */
  0x95, 0x05,        /*     Report Count (5) */
  0x75, 0x01,        /*     Report Size (1) */
  0x81, 0x02,        /*     Input (Data, Variable, Absolute) */
  0x95, 0x01,        /*     Report Count (1) */
  0x75, 0x03,        /*     Report Size (3) */
  0x81, 0x01,        /*     Input (Constant) - 3-bit padding */
  0x05, 0x01,        /*     Usage Page (Generic Desktop Controls) */
  0x09, 0x30,        /*     Usage (X) */
  0x09, 0x31,        /*     Usage (Y) */
  0x09, 0x38,        /*     Usage (Wheel) */
  0x15, 0x81,        /*     Logical Minimum (-127) */
  0x25, 0x7F,        /*     Logical Maximum (127) */
  0x75, 0x08,        /*     Report Size (8) */
  0x95, 0x03,        /*     Report Count (3) */
  0x81, 0x06,        /*     Input (Data, Variable, Relative) */
  0xC0,              /*   End Collection */
  0xC0,              /* End Collection */

  /* --- TLC 2: Keyboard (Report ID 2) --- */
  0x05, 0x01,        /* Usage Page (Generic Desktop Controls) */
  0x09, 0x06,        /* Usage (Keyboard) */
  0xA1, 0x01,        /* Collection (Application) */
  0x85, 0x02,        /*   Report ID (2) */
  0x05, 0x07,        /*   Usage Page (Keyboard/Keypad) */
  0x19, 0xE0,        /*   Usage Minimum (Keyboard LeftControl) */
  0x29, 0xE7,        /*   Usage Maximum (Keyboard Right GUI) */
  0x15, 0x00,        /*   Logical Minimum (0) */
  0x25, 0x01,        /*   Logical Maximum (1) */
  0x75, 0x01,        /*   Report Size (1) */
  0x95, 0x08,        /*   Report Count (8) */
  0x81, 0x02,        /*   Input (Data, Variable, Absolute) - Modifier byte */
  0x95, 0x01,        /*   Report Count (1) */
  0x75, 0x08,        /*   Report Size (8) */
  0x81, 0x01,        /*   Input (Constant) - Reserved byte */
  0x95, 0x06,        /*   Report Count (6) */
  0x75, 0x08,        /*   Report Size (8) */
  0x15, 0x00,        /*   Logical Minimum (0) */
  0x25, 0x65,        /*   Logical Maximum (101) */
  0x05, 0x07,        /*   Usage Page (Keyboard/Keypad) */
  0x19, 0x00,        /*   Usage Minimum (0) */
  0x29, 0x65,        /*   Usage Maximum (101) */
  0x81, 0x00,        /*   Input (Data, Array) - 6 keycodes */
  0xC0               /* End Collection */
};

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT EvtDeviceSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP EvtDeviceSelfManagedIoCleanup;

static VOID
CleanupVhf(_In_ PDEVICE_CONTEXT ctx)
{
  if (ctx->VhfHandle != NULL) {
    VhfDelete(ctx->VhfHandle, TRUE);
    ctx->VhfHandle = NULL;
    ctx->VhfStarted = FALSE;
  }
  if (ctx->VhfIoTarget != NULL) {
    WdfIoTargetClose(ctx->VhfIoTarget);
    WdfObjectDelete(ctx->VhfIoTarget);
    ctx->VhfIoTarget = NULL;
  }
}

NTSTATUS
EvtDeviceSelfManagedIoInit(_In_ WDFDEVICE Device)
{
  PDEVICE_CONTEXT ctx = DeviceGetContext(Device);
  WDF_IO_TARGET_OPEN_PARAMS openParams;
  VHF_CONFIG vhfConfig;
  HANDLE targetFileHandle = NULL;
  NTSTATUS status;

  if (ctx->VhfStarted) {
    return STATUS_SUCCESS;
  }

  status = WdfIoTargetCreate(Device, WDF_NO_OBJECT_ATTRIBUTES, &ctx->VhfIoTarget);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&openParams, NULL);
  status = WdfIoTargetOpen(ctx->VhfIoTarget, &openParams);
  if (!NT_SUCCESS(status)) {
    CleanupVhf(ctx);
    return status;
  }

  targetFileHandle = WdfIoTargetWdmGetTargetFileHandle(ctx->VhfIoTarget);
  if (targetFileHandle == NULL || targetFileHandle == INVALID_HANDLE_VALUE) {
    CleanupVhf(ctx);
    return STATUS_INVALID_HANDLE;
  }

  VHF_CONFIG_INIT(
    &vhfConfig,
    targetFileHandle,
    (USHORT)sizeof(g_MouseReportDescriptor),
    g_MouseReportDescriptor
  );
  vhfConfig.VendorID = 0x1209;
  vhfConfig.ProductID = 0x4E4D;
  vhfConfig.VersionNumber = 0x0100;
  vhfConfig.VhfClientContext = ctx;

  status = VhfCreate(&vhfConfig, &ctx->VhfHandle);
  if (!NT_SUCCESS(status)) {
    CleanupVhf(ctx);
    return status;
  }

  status = VhfStart(ctx->VhfHandle);
  if (!NT_SUCCESS(status)) {
    CleanupVhf(ctx);
    return status;
  }

  ctx->VhfStarted = TRUE;
  return STATUS_SUCCESS;
}

VOID
EvtDeviceSelfManagedIoCleanup(_In_ WDFDEVICE Device)
{
  PDEVICE_CONTEXT ctx = DeviceGetContext(Device);
  CleanupVhf(ctx);
}

NTSTATUS
EvtDeviceAdd(
  _In_ WDFDRIVER Driver,
  _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
  WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
  WDF_OBJECT_ATTRIBUTES deviceAttributes;
  WDFDEVICE device;
  PDEVICE_CONTEXT ctx;
  NTSTATUS status;

  UNREFERENCED_PARAMETER(Driver);

  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
  pnpCallbacks.EvtDeviceSelfManagedIoInit = EvtDeviceSelfManagedIoInit;
  pnpCallbacks.EvtDeviceSelfManagedIoCleanup = EvtDeviceSelfManagedIoCleanup;
  WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

  status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  ctx = DeviceGetContext(device);
  RtlZeroMemory(ctx, sizeof(*ctx));
  return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
  _In_ PDRIVER_OBJECT DriverObject,
  _In_ PUNICODE_STRING RegistryPath
)
{
  WDF_DRIVER_CONFIG config;

  WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
  return WdfDriverCreate(
    DriverObject,
    RegistryPath,
    WDF_NO_OBJECT_ATTRIBUTES,
    &config,
    WDF_NO_HANDLE
  );
}
