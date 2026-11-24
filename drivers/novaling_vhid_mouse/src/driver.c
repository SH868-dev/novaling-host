/**
 * @file driver.c
 * @brief DriverEntry and EvtDeviceAdd for the novaling KMDF + VHF mouse driver.
 */
#include "driver.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(INIT, DriverEntry)
  #pragma alloc_text(PAGE, NovalingEvtDeviceAdd)
#endif

NTSTATUS DriverEntry(
  _In_ PDRIVER_OBJECT DriverObject,
  _In_ PUNICODE_STRING RegistryPath
) {
  WDF_DRIVER_CONFIG config;
  NTSTATUS status;

  WDF_DRIVER_CONFIG_INIT(&config, NovalingEvtDeviceAdd);

  status = WdfDriverCreate(
    DriverObject,
    RegistryPath,
    WDF_NO_OBJECT_ATTRIBUTES,
    &config,
    WDF_NO_HANDLE
  );

  if (!NT_SUCCESS(status)) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "novaling_vhid_mouse: WdfDriverCreate failed (0x%08X)\n", status));
  }

  return status;
}

NTSTATUS NovalingEvtDeviceAdd(
  _In_ WDFDRIVER Driver,
  _Inout_ PWDFDEVICE_INIT DeviceInit
) {
  UNREFERENCED_PARAMETER(Driver);
  PAGED_CODE();

  return NovalingCreateDevice(DeviceInit);
}
