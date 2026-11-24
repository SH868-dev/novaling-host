/**
 * @file src/platform/windows/interception_mouse.cpp
 * @brief Direct Win32 IOCTL client for the Interception kernel mouse filter (mouse.sys).
 */

// local includes (include header first so Windows.h is loaded cleanly)
#include "interception_mouse.h"

// standard includes
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

// platform includes
#include <winioctl.h>

// local includes
#include "src/logging.h"
#include "utf_utils.h"

using namespace std::literals;

namespace platf::windows {
  namespace {
    constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(2);

    // Interception mouse slots: \\.\interception10 .. \\.\interception19
    constexpr int INTERCEPTION_MOUSE_SLOT_FIRST = 10;
    constexpr int INTERCEPTION_MOUSE_SLOT_LAST = 19;

    // Official Interception IOCTLs (from Oblita/Interception library/interception.c)
    constexpr DWORD IC_IOCTL_SET_EVENT = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS);
    constexpr DWORD IC_IOCTL_WRITE = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS);
    constexpr DWORD IC_IOCTL_GET_HARDWARE_ID = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS);

    // MOUSE_INPUT_DATA ButtonFlags
    constexpr std::uint16_t IC_MOUSE_LEFT_DOWN = 0x001;
    constexpr std::uint16_t IC_MOUSE_LEFT_UP = 0x002;
    constexpr std::uint16_t IC_MOUSE_RIGHT_DOWN = 0x004;
    constexpr std::uint16_t IC_MOUSE_RIGHT_UP = 0x008;
    constexpr std::uint16_t IC_MOUSE_MIDDLE_DOWN = 0x010;
    constexpr std::uint16_t IC_MOUSE_MIDDLE_UP = 0x020;
    constexpr std::uint16_t IC_MOUSE_X1_DOWN = 0x040;
    constexpr std::uint16_t IC_MOUSE_X1_UP = 0x080;
    constexpr std::uint16_t IC_MOUSE_X2_DOWN = 0x100;
    constexpr std::uint16_t IC_MOUSE_X2_UP = 0x200;
    constexpr std::uint16_t IC_MOUSE_WHEEL = 0x400;
    constexpr std::uint16_t IC_MOUSE_HWHEEL = 0x800;

    // MOUSE_INPUT_DATA Flags
    constexpr std::uint16_t IC_MOUSE_MOVE_RELATIVE = 0x000;
    constexpr std::uint16_t IC_MOUSE_MOVE_ABSOLUTE = 0x001;
    constexpr std::uint16_t IC_MOUSE_VIRTUAL_DESKTOP = 0x002;

    std::uint16_t to_interception_button_flags(int button, bool release) {
      switch (button) {
        case BUTTON_LEFT:
          return release ? IC_MOUSE_LEFT_UP : IC_MOUSE_LEFT_DOWN;
        case BUTTON_RIGHT:
          return release ? IC_MOUSE_RIGHT_UP : IC_MOUSE_RIGHT_DOWN;
        case BUTTON_MIDDLE:
          return release ? IC_MOUSE_MIDDLE_UP : IC_MOUSE_MIDDLE_DOWN;
        case BUTTON_X1:
          return release ? IC_MOUSE_X1_UP : IC_MOUSE_X1_DOWN;
        case BUTTON_X2:
          return release ? IC_MOUSE_X2_UP : IC_MOUSE_X2_DOWN;
        default:
          return 0;
      }
    }

    std::int32_t normalize_to_65535(float coord, float origin, float span) {
      if (span <= 1.0f) {
        return 0;
      }
      const float norm = std::clamp((coord - origin) / (span - 1.0f), 0.0f, 1.0f);
      const long scaled = std::lround(norm * 65535.0f);
      return static_cast<std::int32_t>(std::clamp<long>(scaled, 0, 65535));
    }

    bool open_interception_slot(int slot, HANDLE &out_handle, HANDLE &out_event, std::wstring &out_hwid) {
      out_handle = INVALID_HANDLE_VALUE;
      out_event = nullptr;
      out_hwid.clear();

      wchar_t device_path[32] {};
      swprintf(device_path, 32, L"\\\\.\\interception%02d", slot);

      HANDLE h = CreateFileW(device_path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (h == INVALID_HANDLE_VALUE) {
        return false;
      }

      HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (!ev) {
        CloseHandle(h);
        return false;
      }

      HANDLE zero_padded[2] = {ev, nullptr};
      DWORD bytes_returned = 0;
      if (!DeviceIoControl(h, IC_IOCTL_SET_EVENT, zero_padded, sizeof(zero_padded), nullptr, 0, &bytes_returned, nullptr)) {
        CloseHandle(ev);
        CloseHandle(h);
        return false;
      }

      std::array<wchar_t, 512> hwid_buf {};
      DWORD hwid_bytes = 0;
      if (DeviceIoControl(h, IC_IOCTL_GET_HARDWARE_ID, nullptr, 0, hwid_buf.data(), static_cast<DWORD>(hwid_buf.size() * sizeof(wchar_t)), &hwid_bytes, nullptr) && hwid_bytes > 0) {
        out_hwid = hwid_buf.data();
      }

      out_handle = h;
      out_event = ev;
      return true;
    }
  }  // namespace

  interception_mouse_client_t::interception_mouse_client_t() {
    std::lock_guard lock {mutex_};
    ensure_connected_locked();
  }

  interception_mouse_client_t::~interception_mouse_client_t() {
    std::lock_guard lock {mutex_};
    disconnect_locked();
  }

  bool interception_mouse_client_t::is_available() {
    std::lock_guard lock {mutex_};
    return ensure_connected_locked();
  }

  void interception_mouse_client_t::disconnect_locked() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
    if (event_ != nullptr) {
      CloseHandle(event_);
      event_ = nullptr;
    }
    slot_ = -1;
  }

  bool interception_mouse_client_t::ensure_connected_locked() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (last_connect_attempt_ != std::chrono::steady_clock::time_point {} &&
        (now - last_connect_attempt_) < RECONNECT_INTERVAL) {
      return false;
    }
    last_connect_attempt_ = now;

    HANDLE best_handle = INVALID_HANDLE_VALUE;
    HANDLE best_event = nullptr;
    int best_slot = -1;
    std::wstring best_hwid;
    int best_score = -1;

    for (int slot = INTERCEPTION_MOUSE_SLOT_FIRST; slot <= INTERCEPTION_MOUSE_SLOT_LAST; ++slot) {
      HANDLE h = INVALID_HANDLE_VALUE;
      HANDLE ev = nullptr;
      std::wstring hwid;
      if (!open_interception_slot(slot, h, ev, hwid)) {
        continue;
      }

      int score = 0;
      if (!hwid.empty()) {
        const bool is_rdp = (hwid.find(L"TERMINPUT_BUS") != std::wstring::npos || hwid.find(L"RDP") != std::wstring::npos);
        const bool is_col01 = (hwid.find(L"COL01") != std::wstring::npos);
        const bool is_vhf = (hwid.find(L"VHF") != std::wstring::npos);
        if (!is_rdp) {
          score = is_col01 ? 4 : (is_vhf ? 3 : 5);  // Physical > VHF COL01 > VHF > RDP
        } else {
          score = 1;
        }
      } else {
        score = 0;
      }

      if (score > best_score) {
        if (best_handle != INVALID_HANDLE_VALUE) {
          CloseHandle(best_handle);
          CloseHandle(best_event);
        }
        best_handle = h;
        best_event = ev;
        best_slot = slot;
        best_hwid = std::move(hwid);
        best_score = score;
      } else {
        CloseHandle(h);
        CloseHandle(ev);
      }
    }

    if (best_handle == INVALID_HANDLE_VALUE) {
      if (!logged_unavailable_) {
        BOOST_LOG(warning) << "Interception mouse driver (\\\\.\\interception10..19) is not available"sv;
        logged_unavailable_ = true;
      }
      return false;
    }

    handle_ = best_handle;
    event_ = best_event;
    slot_ = best_slot;
    logged_unavailable_ = false;
    BOOST_LOG(info) << "Connected to Interception mouse driver (slot="sv << slot_
                    << ", hwid="sv << (best_hwid.empty() ? "<default>"s : utf_utils::to_utf8(best_hwid))
                    << ")"sv;
    return true;
  }

  bool interception_mouse_client_t::write_mouse_data_locked(const mouse_input_data_t &data) {
    if (!ensure_connected_locked()) {
      return false;
    }

    DWORD bytes_written = 0;
    if (!DeviceIoControl(
          handle_,
          IC_IOCTL_WRITE,
          const_cast<mouse_input_data_t *>(&data),
          static_cast<DWORD>(sizeof(mouse_input_data_t)),
          nullptr,
          0,
          &bytes_written,
          nullptr
        ) ||
        bytes_written < sizeof(mouse_input_data_t)) {
      const DWORD err = GetLastError();
      BOOST_LOG(warning) << "Interception IOCTL_WRITE failed on slot "sv << slot_
                         << " (Win32 error "sv << err << ", written="sv << bytes_written << "); reconnecting"sv;
      disconnect_locked();
      return false;
    }

    return true;
  }

  bool interception_mouse_client_t::move_relative(int delta_x, int delta_y) {
    std::lock_guard lock {mutex_};
    mouse_input_data_t data {};
    data.flags = IC_MOUSE_MOVE_RELATIVE;
    data.last_x = delta_x;
    data.last_y = delta_y;
    return write_mouse_data_locked(data);
  }

  bool interception_mouse_client_t::move_absolute(const touch_port_t &touch_port, float x, float y) {
    std::lock_guard lock {mutex_};

    float origin_x = static_cast<float>(GetSystemMetrics(SM_XVIRTUALSCREEN));
    float origin_y = static_cast<float>(GetSystemMetrics(SM_YVIRTUALSCREEN));
    float span_w = static_cast<float>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    float span_h = static_cast<float>(GetSystemMetrics(SM_CYVIRTUALSCREEN));

    if (span_w <= 0.0f || span_h <= 0.0f) {
      origin_x = static_cast<float>(touch_port.env_offset_x);
      origin_y = static_cast<float>(touch_port.env_offset_y);
      span_w = static_cast<float>(touch_port.width);
      span_h = static_cast<float>(touch_port.height);
    }

    mouse_input_data_t data {};
    data.flags = IC_MOUSE_MOVE_ABSOLUTE | IC_MOUSE_VIRTUAL_DESKTOP;
    data.last_x = normalize_to_65535(x, origin_x, span_w);
    data.last_y = normalize_to_65535(y, origin_y, span_h);
    return write_mouse_data_locked(data);
  }

  bool interception_mouse_client_t::button(int button, bool release) {
    const auto flags = to_interception_button_flags(button, release);
    if (flags == 0) {
      return false;
    }

    std::lock_guard lock {mutex_};
    mouse_input_data_t data {};
    data.flags = IC_MOUSE_MOVE_RELATIVE;
    data.button_flags = flags;
    return write_mouse_data_locked(data);
  }

  bool interception_mouse_client_t::scroll(int high_res_distance) {
    std::lock_guard lock {mutex_};
    mouse_input_data_t data {};
    data.flags = IC_MOUSE_MOVE_RELATIVE;
    data.button_flags = IC_MOUSE_WHEEL;
    data.button_data = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(high_res_distance, -32767, 32767)));
    return write_mouse_data_locked(data);
  }

  bool interception_mouse_client_t::hscroll(int high_res_distance) {
    std::lock_guard lock {mutex_};
    mouse_input_data_t data {};
    data.flags = IC_MOUSE_MOVE_RELATIVE;
    data.button_flags = IC_MOUSE_HWHEEL;
    data.button_data = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(high_res_distance, -32767, 32767)));
    return write_mouse_data_locked(data);
  }

}  // namespace platf::windows
