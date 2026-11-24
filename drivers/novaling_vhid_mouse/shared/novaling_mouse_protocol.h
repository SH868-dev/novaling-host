/**
 * @file novaling_mouse_protocol.h
 * @brief Shared IOCTL protocol between the novaling host service and the
 *        KMDF + VHF virtual HID mouse driver.
 */
#pragma once

#ifdef _KERNEL_MODE
  #include <ntddk.h>
#else
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>
  #include <winioctl.h>
  #include <stdint.h>
#endif

#ifdef NOVALING_MOUSE_DEFINE_GUID
  #include <initguid.h>
#else
  #include <guiddef.h>
#endif

// {7E4B6F91-2C8A-4D53-9F14-8B3D6A2E5C10}
DEFINE_GUID(
  GUID_DEVINTERFACE_NOVALING_MOUSE,
  0x7e4b6f91,
  0x2c8a,
  0x4d53,
  0x9f,
  0x14,
  0x8b,
  0x3d,
  0x6a,
  0x2e,
  0x5c,
  0x10
);

#define NOVALING_MOUSE_PROTOCOL_VERSION 1u

#define FILE_DEVICE_NOVALING_MOUSE 0x8000u

#define IOCTL_NOVALING_MOUSE_GET_INFO \
  CTL_CODE(FILE_DEVICE_NOVALING_MOUSE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_NOVALING_MOUSE_REL_REPORT \
  CTL_CODE(FILE_DEVICE_NOVALING_MOUSE, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_NOVALING_MOUSE_ABS_REPORT \
  CTL_CODE(FILE_DEVICE_NOVALING_MOUSE, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_NOVALING_MOUSE_RESET \
  CTL_CODE(FILE_DEVICE_NOVALING_MOUSE, 0x803, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// Button bitmask (matches USB HID Usage Page 0x09 Buttons 1..5)
#define NOVALING_MOUSE_BTN_LEFT   0x01u
#define NOVALING_MOUSE_BTN_RIGHT  0x02u
#define NOVALING_MOUSE_BTN_MIDDLE 0x04u
#define NOVALING_MOUSE_BTN_X1     0x08u
#define NOVALING_MOUSE_BTN_X2     0x10u
#define NOVALING_MOUSE_BTN_MASK   0x1Fu

// Absolute coordinate logical range (HID Logical Min 0 .. Logical Max 32767)
#define NOVALING_MOUSE_ABS_MAX 32767u

// HID Report IDs
#define NOVALING_HID_REPORT_ID_REL      0x01u
#define NOVALING_HID_REPORT_ID_ABS      0x02u
#define NOVALING_HID_REPORT_ID_RES_MULT 0x03u

#pragma pack(push, 1)

typedef struct _NOVALING_MOUSE_INFO {
  UINT32 ProtocolVersion;
  UINT32 Capabilities;  // Reserved capability flags
  UINT8 HighResVWheelEnabled;
  UINT8 HighResHWheelEnabled;
  UINT16 Reserved;
} NOVALING_MOUSE_INFO, *PNOVALING_MOUSE_INFO;

/**
 * @brief Payload for IOCTL_NOVALING_MOUSE_REL_REPORT.
 *
 * Wheel values are always passed in high-resolution WHEEL_DELTA units
 * (120 = 1 standard notch). If Windows has enabled the HID Resolution
 * Multiplier feature, the driver submits them verbatim; otherwise the driver
 * accumulates sub-notch deltas and submits whole notches.
 */
typedef struct _NOVALING_MOUSE_REL_INPUT {
  UINT8 Buttons;   // Bitmask of NOVALING_MOUSE_BTN_*
  UINT8 Reserved;  // Must be 0
  INT16 DeltaX;    // -32767..32767
  INT16 DeltaY;    // -32767..32767
  INT16 VWheel;    // High-res vertical scroll delta (120 = 1 notch)
  INT16 HWheel;    // High-res horizontal scroll delta (120 = 1 notch)
} NOVALING_MOUSE_REL_INPUT, *PNOVALING_MOUSE_REL_INPUT;

/**
 * @brief Payload for IOCTL_NOVALING_MOUSE_ABS_REPORT.
 *
 * Coordinates are normalized to [0, NOVALING_MOUSE_ABS_MAX] across the
 * Windows pointer coordinate space. Button state is driven exclusively via
 * the Relative TLC to prevent cross-collection stuck button states.
 */
typedef struct _NOVALING_MOUSE_ABS_INPUT {
  UINT16 X;        // 0..32767
  UINT16 Y;        // 0..32767
  UINT8 Buttons;   // Reserved / kept 0 on Absolute TLC
  UINT8 Reserved[3];
} NOVALING_MOUSE_ABS_INPUT, *PNOVALING_MOUSE_ABS_INPUT;

// Raw HID Input/Feature reports submitted through VHF
typedef struct _NOVALING_HID_REL_REPORT {
  UINT8 ReportId;  // NOVALING_HID_REPORT_ID_REL
  UINT8 Buttons;   // 5 bits + 3 padding bits
  INT16 DeltaX;
  INT16 DeltaY;
  INT16 VWheel;
  INT16 HWheel;
} NOVALING_HID_REL_REPORT, *PNOVALING_HID_REL_REPORT;

typedef struct _NOVALING_HID_ABS_REPORT {
  UINT8 ReportId;  // NOVALING_HID_REPORT_ID_ABS
  UINT8 Buttons;   // 5 bits + 3 padding bits (always 0 to avoid split button state)
  UINT16 X;        // 0..32767
  UINT16 Y;        // 0..32767
} NOVALING_HID_ABS_REPORT, *PNOVALING_HID_ABS_REPORT;

typedef struct _NOVALING_HID_RES_MULT_FEATURE {
  UINT8 ReportId;    // NOVALING_HID_REPORT_ID_RES_MULT
  UINT8 Multipliers; // bits 0..1: vertical (0|1), bits 2..3: horizontal (0|1), bits 4..7: padding
} NOVALING_HID_RES_MULT_FEATURE, *PNOVALING_HID_RES_MULT_FEATURE;

#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(NOVALING_MOUSE_INFO) == 12, "NOVALING_MOUSE_INFO size mismatch");
static_assert(sizeof(NOVALING_MOUSE_REL_INPUT) == 10, "NOVALING_MOUSE_REL_INPUT size mismatch");
static_assert(sizeof(NOVALING_MOUSE_ABS_INPUT) == 8, "NOVALING_MOUSE_ABS_INPUT size mismatch");
static_assert(sizeof(NOVALING_HID_REL_REPORT) == 10, "NOVALING_HID_REL_REPORT size mismatch");
static_assert(sizeof(NOVALING_HID_ABS_REPORT) == 6, "NOVALING_HID_ABS_REPORT size mismatch");
static_assert(sizeof(NOVALING_HID_RES_MULT_FEATURE) == 2, "NOVALING_HID_RES_MULT_FEATURE size mismatch");
#elif defined(_KERNEL_MODE)
C_ASSERT(sizeof(NOVALING_MOUSE_INFO) == 12);
C_ASSERT(sizeof(NOVALING_MOUSE_REL_INPUT) == 10);
C_ASSERT(sizeof(NOVALING_MOUSE_ABS_INPUT) == 8);
C_ASSERT(sizeof(NOVALING_HID_REL_REPORT) == 10);
C_ASSERT(sizeof(NOVALING_HID_ABS_REPORT) == 6);
C_ASSERT(sizeof(NOVALING_HID_RES_MULT_FEATURE) == 2);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(NOVALING_MOUSE_INFO) == 12, "NOVALING_MOUSE_INFO size mismatch");
_Static_assert(sizeof(NOVALING_MOUSE_REL_INPUT) == 10, "NOVALING_MOUSE_REL_INPUT size mismatch");
_Static_assert(sizeof(NOVALING_MOUSE_ABS_INPUT) == 8, "NOVALING_MOUSE_ABS_INPUT size mismatch");
_Static_assert(sizeof(NOVALING_HID_REL_REPORT) == 10, "NOVALING_HID_REL_REPORT size mismatch");
_Static_assert(sizeof(NOVALING_HID_ABS_REPORT) == 6, "NOVALING_HID_ABS_REPORT size mismatch");
_Static_assert(sizeof(NOVALING_HID_RES_MULT_FEATURE) == 2, "NOVALING_HID_RES_MULT_FEATURE size mismatch");
#endif
