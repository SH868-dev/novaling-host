/**
 * @file src/platform/windows/vhid_mouse.cpp
 * @brief Windows user-mode client for the novaling KMDF + VHF virtual HID mouse driver.
 */
#define NOVALING_MOUSE_DEFINE_GUID

// local includes (include vhid_mouse.h first so Windows.h is loaded before SetupAPI.h)
#include "vhid_mouse.h"

// standard includes
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

// platform includes
#include <setupapi.h>

// local includes
#include "drivers/novaling_vhid_mouse/shared/novaling_mouse_protocol.h"
#include "src/logging.h"

using namespace std::literals;

namespace platf::windows {
  namespace {
    constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(2);
    constexpr int MAX_HID_REL_STEP = 32767;

    std::uint8_t to_driver_button_bit(int button) {
      switch (button) {
        case BUTTON_LEFT:
          return NOVALING_MOUSE_BTN_LEFT;
        case BUTTON_RIGHT:
          return NOVALING_MOUSE_BTN_RIGHT;
        case BUTTON_MIDDLE:
          return NOVALING_MOUSE_BTN_MIDDLE;
        case BUTTON_X1:
          return NOVALING_MOUSE_BTN_X1;
        case BUTTON_X2:
          return NOVALING_MOUSE_BTN_X2;
        default:
          return 0;
      }
    }

    std::wstring find_driver_device_path() {
      HDEVINFO dev_info = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_NOVALING_MOUSE,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
      );
      if (dev_info == INVALID_HANDLE_VALUE) {
        return {};
      }

      SP_DEVICE_INTERFACE_DATA iface_data {};
      iface_data.cbSize = sizeof(iface_data);

      std::wstring result;
      if (SetupDiEnumDeviceInterfaces(dev_info, nullptr, &GUID_DEVINTERFACE_NOVALING_MOUSE, 0, &iface_data)) {
        DWORD required_size = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, nullptr, 0, &required_size, nullptr);
        if (required_size > 0) {
          std::vector<std::uint8_t> buffer(required_size);
          auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
          detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
          if (SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, detail, required_size, nullptr, nullptr)) {
            result = detail->DevicePath;
          }
        }
      }

      SetupDiDestroyDeviceInfoList(dev_info);
      return result;
    }

    std::uint16_t normalize_axis_to_hid(float coord, float origin, float span) {
      if (span <= 1.0f) {
        return 0;
      }

      const float norm = std::clamp((coord - origin) / (span - 1.0f), 0.0f, 1.0f);
      const long scaled = std::lround(norm * static_cast<float>(NOVALING_MOUSE_ABS_MAX));
      return static_cast<std::uint16_t>(std::clamp<long>(scaled, 0, static_cast<long>(NOVALING_MOUSE_ABS_MAX)));
    }
  }  // namespace

  vhid_mouse_client_t::vhid_mouse_client_t() {
    std::lock_guard lock {mutex_};
    ensure_connected_locked();
  }

  vhid_mouse_client_t::~vhid_mouse_client_t() {
    reset();
    std::lock_guard lock {mutex_};
    disconnect_locked();
  }

  bool vhid_mouse_client_t::is_connected() {
    std::lock_guard lock {mutex_};
    return ensure_connected_locked();
  }

  bool vhid_mouse_client_t::ensure_connected_locked() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (last_connect_attempt_ != std::chrono::steady_clock::time_point {} &&
        (now - last_connect_attempt_) < RECONNECT_INTERVAL) {
      return false;
    }
    last_connect_attempt_ = now;

    const auto device_path = find_driver_device_path();
    if (device_path.empty()) {
      if (!logged_unavailable_) {
        BOOST_LOG(info) << "novaling Virtual HID Mouse driver device interface not found; falling back when permitted"sv;
        logged_unavailable_ = true;
      }
      return false;
    }

    HANDLE h = CreateFileW(
      device_path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD err = GetLastError();
      if (!logged_unavailable_) {
        BOOST_LOG(warning) << "Failed to open novaling Virtual HID Mouse driver handle (Win32 error "sv << err << ")"sv;
        logged_unavailable_ = true;
      }
      return false;
    }

    NOVALING_MOUSE_INFO drv_info {};
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(
          h,
          IOCTL_NOVALING_MOUSE_GET_INFO,
          nullptr,
          0,
          &drv_info,
          sizeof(drv_info),
          &bytes_returned,
          nullptr
        ) ||
        bytes_returned < sizeof(drv_info) ||
        drv_info.ProtocolVersion != NOVALING_MOUSE_PROTOCOL_VERSION) {
      BOOST_LOG(warning) << "novaling Virtual HID Mouse driver protocol mismatch or handshake failed"sv;
      CloseHandle(h);
      return false;
    }

    handle_ = h;
    buttons_ = 0;
    logged_unavailable_ = false;
    BOOST_LOG(info) << "Connected to novaling Virtual HID Mouse driver (protocol v"sv
                    << drv_info.ProtocolVersion
                    << ", high-res vwheel="sv << static_cast<int>(drv_info.HighResVWheelEnabled)
                    << ", hwheel="sv << static_cast<int>(drv_info.HighResHWheelEnabled)
                    << ")"sv;
    return true;
  }

  void vhid_mouse_client_t::disconnect_locked() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
    buttons_ = 0;
    last_was_absolute_ = false;
  }

  bool vhid_mouse_client_t::send_rel_locked(std::int16_t dx, std::int16_t dy, std::int16_t vwheel, std::int16_t hwheel, std::uint8_t buttons) {
    if (!ensure_connected_locked()) {
      return false;
    }

    NOVALING_MOUSE_REL_INPUT payload {};
    payload.Buttons = buttons;
    payload.DeltaX = dx;
    payload.DeltaY = dy;
    payload.VWheel = vwheel;
    payload.HWheel = hwheel;

    DWORD bytes_returned = 0;
    if (!DeviceIoControl(
          handle_,
          IOCTL_NOVALING_MOUSE_REL_REPORT,
          &payload,
          sizeof(payload),
          nullptr,
          0,
          &bytes_returned,
          nullptr
        )) {
      const DWORD err = GetLastError();
      BOOST_LOG(warning) << "novaling Virtual HID Mouse REL IOCTL failed (Win32 error "sv << err << "); reconnecting"sv;
      disconnect_locked();
      return false;
    }

    return true;
  }

  bool vhid_mouse_client_t::send_abs_locked(std::uint16_t abs_x, std::uint16_t abs_y, std::uint8_t buttons) {
    if (!ensure_connected_locked()) {
      return false;
    }

    NOVALING_MOUSE_ABS_INPUT payload {};
    payload.X = abs_x;
    payload.Y = abs_y;
    payload.Buttons = buttons;

    DWORD bytes_returned = 0;
    if (!DeviceIoControl(
          handle_,
          IOCTL_NOVALING_MOUSE_ABS_REPORT,
          &payload,
          sizeof(payload),
          nullptr,
          0,
          &bytes_returned,
          nullptr
        )) {
      const DWORD err = GetLastError();
      BOOST_LOG(warning) << "novaling Virtual HID Mouse ABS IOCTL failed (Win32 error "sv << err << "); reconnecting"sv;
      disconnect_locked();
      return false;
    }

    return true;
  }

  bool vhid_mouse_client_t::move_relative(int delta_x, int delta_y) {
    std::lock_guard lock {mutex_};
    if (!ensure_connected_locked()) {
      return false;
    }

    if (last_was_absolute_) {
      if (buttons_ != 0) {
        // Release buttons on the Absolute TLC before transferring ownership to Relative TLC
        send_abs_locked(last_abs_x_, last_abs_y_, 0);
      }
      last_was_absolute_ = false;
    }

    int rem_x = delta_x;
    int rem_y = delta_y;
    do {
      const int step_x = std::clamp(rem_x, -MAX_HID_REL_STEP, MAX_HID_REL_STEP);
      const int step_y = std::clamp(rem_y, -MAX_HID_REL_STEP, MAX_HID_REL_STEP);
      if (!send_rel_locked(static_cast<std::int16_t>(step_x), static_cast<std::int16_t>(step_y), 0, 0, buttons_)) {
        return false;
      }
      rem_x -= step_x;
      rem_y -= step_y;
    } while (rem_x != 0 || rem_y != 0);

    return true;
  }

  bool vhid_mouse_client_t::move_absolute(const touch_port_t &touch_port, float x, float y, bool allow_multimonitor_fallback) {
    std::lock_guard lock {mutex_};
    if (!ensure_connected_locked()) {
      return false;
    }

    const int primary_w = GetSystemMetrics(SM_CXSCREEN);
    const int primary_h = GetSystemMetrics(SM_CYSCREEN);
    const int virt_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virt_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virt_w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virt_h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    const bool is_multimonitor =
      virt_x != 0 || virt_y != 0 ||
      (primary_w > 0 && virt_w > primary_w) ||
      (primary_h > 0 && virt_h > primary_h);

    if (is_multimonitor && allow_multimonitor_fallback && primary_w > 0 && primary_h > 0) {
      if (x < 0.0f || y < 0.0f || x >= static_cast<float>(primary_w) || y >= static_cast<float>(primary_h)) {
        if (!logged_multimonitor_fallback_) {
          BOOST_LOG(info) << "Absolute cursor target is outside the primary display; falling back for multi-monitor virtual desktop coordinates"sv;
          logged_multimonitor_fallback_ = true;
        }
        return false;
      }
    }

    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float span_w = static_cast<float>(primary_w);
    float span_h = static_cast<float>(primary_h);

    if (span_w <= 0.0f || span_h <= 0.0f) {
      origin_x = static_cast<float>(touch_port.env_offset_x);
      origin_y = static_cast<float>(touch_port.env_offset_y);
      span_w = static_cast<float>(touch_port.width);
      span_h = static_cast<float>(touch_port.height);
    }

    const auto abs_x = normalize_axis_to_hid(x, origin_x, span_w);
    const auto abs_y = normalize_axis_to_hid(y, origin_y, span_h);

    if (!last_was_absolute_) {
      if (buttons_ != 0) {
        // Release buttons on the Relative TLC before transferring ownership to Absolute TLC
        send_rel_locked(0, 0, 0, 0, 0);
      }
      last_was_absolute_ = true;
    }

    last_abs_x_ = abs_x;
    last_abs_y_ = abs_y;
    return send_abs_locked(abs_x, abs_y, buttons_);
  }

  bool vhid_mouse_client_t::button(int button, bool release) {
    const auto bit = to_driver_button_bit(button);
    if (bit == 0) {
      return false;
    }

    std::lock_guard lock {mutex_};
    if (!ensure_connected_locked()) {
      return false;
    }

    const auto prev_buttons = buttons_;
    if (release) {
      buttons_ = static_cast<std::uint8_t>(buttons_ & ~bit);
    } else {
      buttons_ = static_cast<std::uint8_t>(buttons_ | bit);
    }

    // Submit buttons on whichever TLC is currently driving pointer coordinates so
    // motion and clicks always originate from the exact same HID mouse device.
    const bool ok = last_was_absolute_
      ? send_abs_locked(last_abs_x_, last_abs_y_, buttons_)
      : send_rel_locked(0, 0, 0, 0, buttons_);

    if (!ok) {
      buttons_ = prev_buttons;
      return false;
    }

    return true;
  }

  bool vhid_mouse_client_t::scroll(int high_res_distance) {
    std::lock_guard lock {mutex_};
    if (!ensure_connected_locked()) {
      return false;
    }

    const std::uint8_t rel_buttons = last_was_absolute_ ? 0 : buttons_;
    int remaining = high_res_distance;
    while (remaining != 0) {
      const int step = std::clamp(remaining, -MAX_HID_REL_STEP, MAX_HID_REL_STEP);
      if (!send_rel_locked(0, 0, static_cast<std::int16_t>(step), 0, rel_buttons)) {
        return false;
      }
      remaining -= step;
    }
    return true;
  }

  bool vhid_mouse_client_t::hscroll(int high_res_distance) {
    std::lock_guard lock {mutex_};
    if (!ensure_connected_locked()) {
      return false;
    }

    const std::uint8_t rel_buttons = last_was_absolute_ ? 0 : buttons_;
    int remaining = high_res_distance;
    while (remaining != 0) {
      const int step = std::clamp(remaining, -MAX_HID_REL_STEP, MAX_HID_REL_STEP);
      if (!send_rel_locked(0, 0, 0, static_cast<std::int16_t>(step), rel_buttons)) {
        return false;
      }
      remaining -= step;
    }
    return true;
  }

  void vhid_mouse_client_t::reset() {
    std::lock_guard lock {mutex_};
    buttons_ = 0;
    if (handle_ == INVALID_HANDLE_VALUE) {
      return;
    }

    DWORD bytes_returned = 0;
    DeviceIoControl(
      handle_,
      IOCTL_NOVALING_MOUSE_RESET,
      nullptr,
      0,
      nullptr,
      0,
      &bytes_returned,
      nullptr
    );
  }

}  // namespace platf::windows
