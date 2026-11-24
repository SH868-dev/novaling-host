/**
 * @file queue.c
 * @brief WDF I/O queue and IOCTL dispatch for the novaling virtual HID mouse driver.
 */
#include "driver.h"

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, NovalingQueueInitialize)
#endif

NTSTATUS NovalingQueueInitialize(_In_ WDFDEVICE Device) {
  WDF_IO_QUEUE_CONFIG queueConfig;
  NTSTATUS status;

  PAGED_CODE();

  // Parallel dispatch keeps mouse report submission low-latency;
  // shared state inside NOVALING_DEVICE_CONTEXT is protected by StateLock.
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
  queueConfig.EvtIoDeviceControl = NovalingEvtIoDeviceControl;

  status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
  if (!NT_SUCCESS(status)) {
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "novaling_vhid_mouse: WdfIoQueueCreate failed (0x%08X)\n", status));
  }

  return status;
}

VOID NovalingEvtIoDeviceControl(
  _In_ WDFQUEUE Queue,
  _In_ WDFREQUEST Request,
  _In_ size_t OutputBufferLength,
  _In_ size_t InputBufferLength,
  _In_ ULONG IoControlCode
) {
  WDFDEVICE device = WdfIoQueueGetDevice(Queue);
  PNOVALING_DEVICE_CONTEXT ctx = NovalingGetDeviceContext(device);
  NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
  size_t bytesReturned = 0;

  UNREFERENCED_PARAMETER(OutputBufferLength);
  UNREFERENCED_PARAMETER(InputBufferLength);

  switch (IoControlCode) {
    case IOCTL_NOVALING_MOUSE_GET_INFO:
      {
        PNOVALING_MOUSE_INFO info = NULL;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(NOVALING_MOUSE_INFO), (PVOID *) &info, NULL);
        if (NT_SUCCESS(status)) {
          UCHAR multipliers;
          WdfSpinLockAcquire(ctx->StateLock);
          multipliers = ctx->ResolutionMultipliers;
          WdfSpinLockRelease(ctx->StateLock);

          RtlZeroMemory(info, sizeof(*info));
          info->ProtocolVersion = NOVALING_MOUSE_PROTOCOL_VERSION;
          info->Capabilities = 0x03u;  // Bit 0: Relative, Bit 1: Absolute
          info->HighResVWheelEnabled = (UCHAR) ((multipliers & 0x03u) != 0u ? 1u : 0u);
          info->HighResHWheelEnabled = (UCHAR) (((multipliers >> 2) & 0x03u) != 0u ? 1u : 0u);
          bytesReturned = sizeof(NOVALING_MOUSE_INFO);
        }
        break;
      }

    case IOCTL_NOVALING_MOUSE_REL_REPORT:
      {
        PNOVALING_MOUSE_REL_INPUT input = NULL;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(NOVALING_MOUSE_REL_INPUT), (PVOID *) &input, NULL);
        if (NT_SUCCESS(status)) {
          status = NovalingSubmitRelativeReport(ctx, input);
        }
        break;
      }

    case IOCTL_NOVALING_MOUSE_ABS_REPORT:
      {
        PNOVALING_MOUSE_ABS_INPUT input = NULL;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(NOVALING_MOUSE_ABS_INPUT), (PVOID *) &input, NULL);
        if (NT_SUCCESS(status)) {
          status = NovalingSubmitAbsoluteReport(ctx, input);
        }
        break;
      }

    case IOCTL_NOVALING_MOUSE_RESET:
      status = NovalingSubmitReset(ctx);
      break;

    default:
      status = STATUS_INVALID_DEVICE_REQUEST;
      break;
  }

  WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
