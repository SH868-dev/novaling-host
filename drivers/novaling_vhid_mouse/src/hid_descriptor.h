/**
 * @file hid_descriptor.h
 * @brief Multi-TLC USB HID Report Descriptor for the novaling virtual mouse.
 *
 * Exposes two Top-Level Collections (TLCs) under one VHF virtual HID device:
 *   1. Report ID 1: Relative Mouse (5 buttons, 16-bit dX/dY, high-res VWheel & AC Pan)
 *      + Feature Report ID 3: Resolution Multiplier (120 units/notch for VWheel & HWheel)
 *   2. Report ID 2: Absolute Mouse (5 buttons, 0..32767 X/Y)
 */
#pragma once

#include <ntddk.h>
#include "../shared/novaling_mouse_protocol.h"

static const UCHAR g_NovalingMouseReportDescriptor[] = {
  // =========================================================================
  // Top-Level Collection 1: Relative Mouse (Report ID 1) + Res Mult (ID 3)
  // =========================================================================
  0x05, 0x01,                                // Usage Page (Generic Desktop)
  0x09, 0x02,                                // Usage (Mouse)
  0xA1, 0x01,                                // Collection (Application)
    0x09, 0x01,                              //   Usage (Pointer)
    0xA1, 0x00,                              //   Collection (Physical)
      0x85, NOVALING_HID_REPORT_ID_REL,      //     Report ID (1)

      // 5 Buttons (Left, Right, Middle, X1, X2)
      0x05, 0x09,                            //     Usage Page (Button)
      0x19, 0x01,                            //     Usage Minimum (Button 1)
      0x29, 0x05,                            //     Usage Maximum (Button 5)
      0x15, 0x00,                            //     Logical Minimum (0)
      0x25, 0x01,                            //     Logical Maximum (1)
      0x95, 0x05,                            //     Report Count (5)
      0x75, 0x01,                            //     Report Size (1)
      0x81, 0x02,                            //     Input (Data, Var, Abs)

      // 3-bit Padding -> Byte aligned (1 byte total)
      0x95, 0x01,                            //     Report Count (1)
      0x75, 0x03,                            //     Report Size (3)
      0x81, 0x03,                            //     Input (Const, Var, Abs)

      // Relative X, Y (16-bit signed: -32767..32767)
      0x05, 0x01,                            //     Usage Page (Generic Desktop)
      0x09, 0x30,                            //     Usage (X)
      0x09, 0x31,                            //     Usage (Y)
      0x16, 0x01, 0x80,                      //     Logical Minimum (-32767)
      0x26, 0xFF, 0x7F,                      //     Logical Maximum (32767)
      0x75, 0x10,                            //     Report Size (16)
      0x95, 0x02,                            //     Report Count (2)
      0x81, 0x06,                            //     Input (Data, Var, Rel)

      // Vertical Wheel + Resolution Multiplier Feature (120x high-res wheel)
      0xA1, 0x02,                            //     Collection (Logical)
        0x85, NOVALING_HID_REPORT_ID_RES_MULT, //   Report ID (3)
        0x05, 0x01,                          //     Usage Page (Generic Desktop)
        0x09, 0x48,                          //     Usage (Resolution Multiplier)
        0x15, 0x00,                          //     Logical Minimum (0)
        0x25, 0x01,                          //     Logical Maximum (1)
        0x35, 0x01,                          //     Physical Minimum (1)
        0x45, 0x78,                          //     Physical Maximum (120)
        0x75, 0x02,                          //     Report Size (2)
        0x95, 0x01,                          //     Report Count (1)
        0xB1, 0x02,                          //     Feature (Data, Var, Abs)

        0x85, NOVALING_HID_REPORT_ID_REL,    //     Report ID (1)
        0x09, 0x38,                          //     Usage (Wheel)
        0x16, 0x01, 0x80,                    //     Logical Minimum (-32767)
        0x26, 0xFF, 0x7F,                    //     Logical Maximum (32767)
        0x35, 0x00,                          //     Physical Minimum (0)
        0x45, 0x00,                          //     Physical Maximum (0)
        0x75, 0x10,                          //     Report Size (16)
        0x95, 0x01,                          //     Report Count (1)
        0x81, 0x06,                          //     Input (Data, Var, Rel)
      0xC0,                                  //     End Collection (Logical)

      // Horizontal Wheel (AC Pan) + Resolution Multiplier Feature (120x)
      0xA1, 0x02,                            //     Collection (Logical)
        0x85, NOVALING_HID_REPORT_ID_RES_MULT, //   Report ID (3)
        0x05, 0x01,                          //     Usage Page (Generic Desktop)
        0x09, 0x48,                          //     Usage (Resolution Multiplier)
        0x15, 0x00,                          //     Logical Minimum (0)
        0x25, 0x01,                          //     Logical Maximum (1)
        0x35, 0x01,                          //     Physical Minimum (1)
        0x45, 0x78,                          //     Physical Maximum (120)
        0x75, 0x02,                          //     Report Size (2)
        0x95, 0x01,                          //     Report Count (1)
        0xB1, 0x02,                          //     Feature (Data, Var, Abs)

        // 4-bit Feature Padding (2 + 2 + 4 = 8 bits for Report ID 3)
        0x75, 0x04,                          //     Report Size (4)
        0x95, 0x01,                          //     Report Count (1)
        0xB1, 0x03,                          //     Feature (Const, Var, Abs)

        0x85, NOVALING_HID_REPORT_ID_REL,    //     Report ID (1)
        0x05, 0x0C,                          //     Usage Page (Consumer)
        0x0A, 0x38, 0x02,                    //     Usage (AC Pan)
        0x16, 0x01, 0x80,                    //     Logical Minimum (-32767)
        0x26, 0xFF, 0x7F,                    //     Logical Maximum (32767)
        0x35, 0x00,                          //     Physical Minimum (0)
        0x45, 0x00,                          //     Physical Maximum (0)
        0x75, 0x10,                          //     Report Size (16)
        0x95, 0x01,                          //     Report Count (1)
        0x81, 0x06,                          //     Input (Data, Var, Rel)
      0xC0,                                  //     End Collection (Logical)
    0xC0,                                    //   End Collection (Physical)
  0xC0,                                      // End Collection (Application)

  // =========================================================================
  // Top-Level Collection 2: Absolute Mouse (Report ID 2)
  // =========================================================================
  0x05, 0x01,                                // Usage Page (Generic Desktop)
  0x09, 0x02,                                // Usage (Mouse)
  0xA1, 0x01,                                // Collection (Application)
    0x09, 0x01,                              //   Usage (Pointer)
    0xA1, 0x00,                              //   Collection (Physical)
      0x85, NOVALING_HID_REPORT_ID_ABS,      //     Report ID (2)

      // 5 Buttons (kept 0 by host client to avoid cross-TLC stuck buttons)
      0x05, 0x09,                            //     Usage Page (Button)
      0x19, 0x01,                            //     Usage Minimum (Button 1)
      0x29, 0x05,                            //     Usage Maximum (Button 5)
      0x15, 0x00,                            //     Logical Minimum (0)
      0x25, 0x01,                            //     Logical Maximum (1)
      0x95, 0x05,                            //     Report Count (5)
      0x75, 0x01,                            //     Report Size (1)
      0x81, 0x02,                            //     Input (Data, Var, Abs)

      // 3-bit Padding -> Byte aligned (1 byte total)
      0x95, 0x01,                            //     Report Count (1)
      0x75, 0x03,                            //     Report Size (3)
      0x81, 0x03,                            //     Input (Const, Var, Abs)

      // Absolute X, Y (0..32767)
      0x05, 0x01,                            //     Usage Page (Generic Desktop)
      0x09, 0x30,                            //     Usage (X)
      0x09, 0x31,                            //     Usage (Y)
      0x15, 0x00,                            //     Logical Minimum (0)
      0x26, 0xFF, 0x7F,                      //     Logical Maximum (32767)
      0x75, 0x10,                            //     Report Size (16)
      0x95, 0x02,                            //     Report Count (2)
      0x81, 0x02,                            //     Input (Data, Var, Abs)
    0xC0,                                    //   End Collection (Physical)
  0xC0                                       // End Collection (Application)
};
