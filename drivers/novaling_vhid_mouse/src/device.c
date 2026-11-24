/**
 * @file device.c
 * @brief WDF device lifecycle, VHF virtual HID initialization, and report submission.
 */
#define NOVALING_MOUSE_DEFINE_GUID
#include "driver.h"
#include "hid_descriptor.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, NovalingCreateDevice)
  #pragma alloc_text(PAGE, NovalingEvtDeviceSelfManagedIoInit)
  #pragma alloc_text(PAGE, NovalingEvtDeviceSelfManagedIoCleanup)
  #pragma alloc_text(PAGE, NovalingEvtFileClose)
#endif

static INT16 ClampInt16(_In_ INT32 Value) {
  if (Value > 32767) {
    return 32767;
  }
  if (Value < -32767) {
    return -32767;
  }
  return (INT16) Value;
}

static NTSTATUS NovalingCreateVhfDevice(_In_ WDFDEVICE Device) {
  PNOVALING_DEVICE_CONTEXT ctx = NovalingGetDeviceContext(Device);
  VHF_CONFIG vhfConfig;
  NTSTATUS status;

  VHF_CONFIG_INIT(
    &vhfConfig,
    WdfDeviceWdmGetDeviceObject(Device),
    sizeof(g_NovalingMouseReportDescriptor),
    (PUCHAR) g_NovalingMouseReportDescriptor
  );

  vhfConfig.VendorID = NOVALING_VHID_VENDOR_ID;
  vhfConfig.ProductID = NOVALING_VHID_PRODUCT_ID;
  vhfConfig.VersionNumber = NOVALING_VHID_VERSION_NUM;
  vhfConfig.VhfClientContext = ctx;
  vhfConfig.EvtVhfAsyncOperationGetFeature = NovalingVhfGetFeature;
  vhfConfig.EvtVhfAsyncOperationSetFeature = NovalingVhfSetFeature;

  status = VhfCreate(&vhfConfig, &ctx->VhfHandle);
  if (!NT_SUCCESS(status)) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "novaling_vhid_mouse: VhfCreate failed (0x%08X)\n", status));
    return status;
  }

  return STATUS_SUCCESS;
}

NTSTATUS NovalingCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit) {
  WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
  WDF_FILEOBJECT_CONFIG fileConfig;
  WDF_OBJECT_ATTRIBUTES deviceAttributes;
  WDF_OBJECT_ATTRIBUTES lockAttributes;
  WDFDEVICE device;
  PNOVALING_DEVICE_CONTEXT ctx;
  NTSTATUS status;

  PAGED_CODE();

  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
  pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = NovalingEvtDeviceSelfManagedIoInit;
  pnpPowerCallbacks.EvtDeviceSelfManagedIoCleanup = NovalingEvtDeviceSelfManagedIoCleanup;
  WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

  WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, WDF_NO_EVENT_CALLBACK, NovalingEvtFileClose, WDF_NO_EVENT_CALLBACK);
  WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, NOVALING_DEVICE_CONTEXT);

  status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
  if (!NT_SUCCESS(status)) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "novaling_vhid_mouse: WdfDeviceCreate failed (0x%08X)\n", status));
    return status;
  }

  ctx = NovalingGetDeviceContext(device);
  RtlZeroMemory(ctx, sizeof(*ctx));
  ctx->Device = device;

  WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
  lockAttributes.ParentObject = device;
  status = WdfSpinLockCreate(&lockAttributes, &ctx->StateLock);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_NOVALING_MOUSE, NULL);
  if (!NT_SUCCESS(status)) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "novaling_vhid_mouse: WdfDeviceCreateDeviceInterface failed (0x%08X)\n", status));
    return status;
  }

  status = NovalingQueueInitialize(device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status = NovalingCreateVhfDevice(device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  return STATUS_SUCCESS;
}

NTSTATUS NovalingEvtDeviceSelfManagedIoInit(_In_ WDFDEVICE Device) {
  PNOVALING_DEVICE_CONTEXT ctx = NovalingGetDeviceContext(Device);
  NTSTATUS status;

  PAGED_CODE();

  if (ctx->VhfHandle == NULL) {
    return STATUS_INVALID_DEVICE_STATE;
  }

  status = VhfStart(ctx->VhfHandle);
  if (!NT_SUCCESS(status)) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "novaling_vhid_mouse: VhfStart failed (0x%08X)\n", status));
    return status;
  }

  WdfSpinLockAcquire(ctx->StateLock);
  ctx->VhfStarted = TRUE;
  WdfSpinLockRelease(ctx->StateLock);

  return STATUS_SUCCESS;
}

VOID NovalingEvtDeviceSelfManagedIoCleanup(_In_ WDFDEVICE Device) {
  PNOVALING_DEVICE_CONTEXT ctx = NovalingGetDeviceContext(Device);

  PAGED_CODE();

  WdfSpinLockAcquire(ctx->StateLock);
  ctx->VhfStarted = FALSE;
  WdfSpinLockRelease(ctx->StateLock);

  if (ctx->VhfHandle != NULL) {
    VhfDelete(ctx->VhfHandle, TRUE);
    ctx->VhfHandle = NULL;
  }
}

VOID NovalingEvtFileClose(_In_ WDFFILEOBJECT FileObject) {
  WDFDEVICE device = WdfFileObjectGetDevice(FileObject);
  PNOVALING_DEVICE_CONTEXT ctx = NovalingGetDeviceContext(device);

  PAGED_CODE();

  // Release any stuck mouse buttons when the host service closes its handle
  // or terminates unexpectedly.
  (VOID) NovalingSubmitReset(ctx);
}

VOID NovalingVhfGetFeature(
  _In_ PVOID VhfClientContext,
  _In_ VHFOPERATIONHANDLE VhfOperationHandle,
  _In_opt_ PVOID VhfOperationContext,
  _In_ PHID_XFER_PACKET HidTransferPacket
) {
  PNOVALING_DEVICE_CONTEXT ctx = (PNOVALING_DEVICE_CONTEXT) VhfClientContext;
  NTSTATUS status = STATUS_INVALID_PARAMETER;

  UNREFERENCED_PARAMETER(VhfOperationContext);

  if (HidTransferPacket != NULL &&
      HidTransferPacket->reportId == NOVALING_HID_REPORT_ID_RES_MULT &&
      HidTransferPacket->reportBuffer != NULL &&
      HidTransferPacket->reportBufferLen >= sizeof(NOVALING_HID_RES_MULT_FEATURE)) {
    PNOVALING_HID_RES_MULT_FEATURE feature = (PNOVALING_HID_RES_MULT_FEATURE) HidTransferPacket->reportBuffer;
    UCHAR multipliers;

    WdfSpinLockAcquire(ctx->StateLock);
    multipliers = ctx->ResolutionMultipliers;
    WdfSpinLockRelease(ctx->StateLock);

    feature->ReportId = NOVALING_HID_REPORT_ID_RES_MULT;
    feature->Multipliers = multipliers;
    status = STATUS_SUCCESS;
  }

  VhfAsyncOperationComplete(VhfOperationHandle, status);
}

VOID NovalingVhfSetFeature(
  _In_ PVOID VhfClientContext,
  _In_ VHFOPERATIONHANDLE VhfOperationHandle,
  _In_opt_ PVOID VhfOperationContext,
  _In_ PHID_XFER_PACKET HidTransferPacket
) {
  PNOVALING_DEVICE_CONTEXT ctx = (PNOVALING_DEVICE_CONTEXT) VhfClientContext;
  NTSTATUS status = STATUS_INVALID_PARAMETER;

  UNREFERENCED_PARAMETER(VhfOperationContext);

  if (HidTransferPacket != NULL &&
      HidTransferPacket->reportId == NOVALING_HID_REPORT_ID_RES_MULT &&
      HidTransferPacket->reportBuffer != NULL &&
      HidTransferPacket->reportBufferLen >= sizeof(NOVALING_HID_RES_MULT_FEATURE)) {
    const NOVALING_HID_RES_MULT_FEATURE *feature = (const NOVALING_HID_RES_MULT_FEATURE *) HidTransferPacket->reportBuffer;
    // Mask to valid 2-bit vertical (0..1) and 2-bit horizontal (2..3) multiplier fields
    UCHAR vMult = feature->Multipliers & 0x03u;
    UCHAR hMult = (feature->Multipliers >> 2) & 0x03u;
    if (vMult > 1u) {
      vMult = 1u;
    }
    if (hMult > 1u) {
      hMult = 1u;
    }

    WdfSpinLockAcquire(ctx->StateLock);
    ctx->ResolutionMultipliers = (UCHAR) (vMult | (hMult << 2));
    ctx->PendingVWheelDelta = 0;
    ctx->PendingHWheelDelta = 0;
    WdfSpinLockRelease(ctx->StateLock);

    status = STATUS_SUCCESS;
  }

  VhfAsyncOperationComplete(VhfOperationHandle, status);
}

NTSTATUS NovalingSubmitRelativeReport(
  _In_ PNOVALING_DEVICE_CONTEXT Context,
  _In_ const NOVALING_MOUSE_REL_INPUT *Input
) {
  NOVALING_HID_REL_REPORT report;
  HID_XFER_PACKET packet;
  BOOLEAN vHighRes;
  BOOLEAN hHighRes;
  INT16 vWheelOut = 0;
  INT16 hWheelOut = 0;

  WdfSpinLockAcquire(Context->StateLock);
  if (!Context->VhfStarted || Context->VhfHandle == NULL) {
    WdfSpinLockRelease(Context->StateLock);
    return STATUS_DEVICE_NOT_READY;
  }

  vHighRes = ((Context->ResolutionMultipliers & 0x03u) != 0u) ? TRUE : FALSE;
  hHighRes = (((Context->ResolutionMultipliers >> 2) & 0x03u) != 0u) ? TRUE : FALSE;

  if (Input->VWheel != 0) {
    if (vHighRes) {
      vWheelOut = ClampInt16(Input->VWheel);
    } else {
      Context->PendingVWheelDelta += Input->VWheel;
      INT32 notches = Context->PendingVWheelDelta / NOVALING_WHEEL_DELTA;
      Context->PendingVWheelDelta -= notches * NOVALING_WHEEL_DELTA;
      vWheelOut = ClampInt16(notches);
    }
  }

  if (Input->HWheel != 0) {
    if (hHighRes) {
      hWheelOut = ClampInt16(Input->HWheel);
    } else {
      Context->PendingHWheelDelta += Input->HWheel;
      INT32 notches = Context->PendingHWheelDelta / NOVALING_WHEEL_DELTA;
      Context->PendingHWheelDelta -= notches * NOVALING_WHEEL_DELTA;
      hWheelOut = ClampInt16(notches);
    }
  }

  Context->LastRelButtons = (UCHAR) (Input->Buttons & NOVALING_MOUSE_BTN_MASK);
  WdfSpinLockRelease(Context->StateLock);

  RtlZeroMemory(&report, sizeof(report));
  report.ReportId = NOVALING_HID_REPORT_ID_REL;
  report.Buttons = (UCHAR) (Input->Buttons & NOVALING_MOUSE_BTN_MASK);
  report.DeltaX = ClampInt16(Input->DeltaX);
  report.DeltaY = ClampInt16(Input->DeltaY);
  report.VWheel = vWheelOut;
  report.HWheel = hWheelOut;

  packet.reportBuffer = (PUCHAR) &report;
  packet.reportBufferLen = sizeof(report);
  packet.reportId = NOVALING_HID_REPORT_ID_REL;

  return VhfReadReportSubmit(Context->VhfHandle, &packet);
}

NTSTATUS NovalingSubmitAbsoluteReport(
  _In_ PNOVALING_DEVICE_CONTEXT Context,
  _In_ const NOVALING_MOUSE_ABS_INPUT *Input
) {
  NOVALING_HID_ABS_REPORT report;
  HID_XFER_PACKET packet;
  UCHAR buttons = (UCHAR) (Input->Buttons & NOVALING_MOUSE_BTN_MASK);
  UINT16 clampedX = (Input->X > NOVALING_MOUSE_ABS_MAX) ? NOVALING_MOUSE_ABS_MAX : Input->X;
  UINT16 clampedY = (Input->Y > NOVALING_MOUSE_ABS_MAX) ? NOVALING_MOUSE_ABS_MAX : Input->Y;

  WdfSpinLockAcquire(Context->StateLock);
  if (!Context->VhfStarted || Context->VhfHandle == NULL) {
    WdfSpinLockRelease(Context->StateLock);
    return STATUS_DEVICE_NOT_READY;
  }
  Context->LastAbsButtons = buttons;
  Context->LastAbsX = clampedX;
  Context->LastAbsY = clampedY;
  WdfSpinLockRelease(Context->StateLock);

  RtlZeroMemory(&report, sizeof(report));
  report.ReportId = NOVALING_HID_REPORT_ID_ABS;
  report.Buttons = buttons;
  report.X = clampedX;
  report.Y = clampedY;

  packet.reportBuffer = (PUCHAR) &report;
  packet.reportBufferLen = sizeof(report);
  packet.reportId = NOVALING_HID_REPORT_ID_ABS;

  return VhfReadReportSubmit(Context->VhfHandle, &packet);
}

NTSTATUS NovalingSubmitReset(_In_ PNOVALING_DEVICE_CONTEXT Context) {
  NOVALING_MOUSE_REL_INPUT zeroRel;
  NOVALING_MOUSE_ABS_INPUT zeroAbs;
  BOOLEAN hadRelButtons = FALSE;
  BOOLEAN hadAbsButtons = FALSE;
  UINT16 lastAbsX = 0;
  UINT16 lastAbsY = 0;
  NTSTATUS status = STATUS_SUCCESS;

  WdfSpinLockAcquire(Context->StateLock);
  if (!Context->VhfStarted || Context->VhfHandle == NULL) {
    Context->LastRelButtons = 0;
    Context->LastAbsButtons = 0;
    Context->PendingVWheelDelta = 0;
    Context->PendingHWheelDelta = 0;
    WdfSpinLockRelease(Context->StateLock);
    return STATUS_SUCCESS;
  }
  hadRelButtons = (Context->LastRelButtons != 0u) ? TRUE : FALSE;
  hadAbsButtons = (Context->LastAbsButtons != 0u) ? TRUE : FALSE;
  lastAbsX = Context->LastAbsX;
  lastAbsY = Context->LastAbsY;
  Context->PendingVWheelDelta = 0;
  Context->PendingHWheelDelta = 0;
  WdfSpinLockRelease(Context->StateLock);

  if (hadRelButtons) {
    RtlZeroMemory(&zeroRel, sizeof(zeroRel));
    status = NovalingSubmitRelativeReport(Context, &zeroRel);
  }
  if (hadAbsButtons) {
    RtlZeroMemory(&zeroAbs, sizeof(zeroAbs));
    zeroAbs.X = lastAbsX;
    zeroAbs.Y = lastAbsY;
    zeroAbs.Buttons = 0;
    status = NovalingSubmitAbsoluteReport(Context, &zeroAbs);
  }
  return status;
}
