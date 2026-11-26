/**
 * @file src/platform/windows/interception_mouse.h
 * @brief Direct Win32 IOCTL client for the Interception kernel mouse filter (mouse.sys).
 *
 * Talks directly to \\.\interception10..19 so mouse injection works even on
 * headless hosts where keyboard.sys (\\.\interception00..09) has no device
 * attached and interception.dll's interception_create_context() would fail.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <mutex>

// platform includes
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// local includes
#include "src/platform/common.h"

namespace platf::windows {

  /**
   * @brief Submits mouse input directly to Oblita Interception's mouse.sys filter driver.
   */
  class interception_mouse_client_t {
  public:
    interception_mouse_client_t();
    ~interception_mouse_client_t();

    interception_mouse_client_t(const interception_mouse_client_t &) = delete;
    interception_mouse_client_t &operator=(const interception_mouse_client_t &) = delete;

    bool is_available();
    bool move_relative(int delta_x, int delta_y);
    bool move_absolute(const touch_port_t &touch_port, float x, float y);
    bool button(int button, bool release);
    bool scroll(int high_res_distance);
    bool hscroll(int high_res_distance);

  private:
    struct mouse_input_data_t {
      std::uint16_t unit_id;
      std::uint16_t flags;
      std::uint16_t button_flags;
      std::uint16_t button_data;
      std::uint32_t raw_buttons;
      std::int32_t last_x;
      std::int32_t last_y;
      std::uint32_t extra_information;
    };

    bool ensure_connected_locked();
    void disconnect_locked();
    bool write_mouse_data_locked(const mouse_input_data_t &data);

    std::mutex mutex_;
    HANDLE handle_ {INVALID_HANDLE_VALUE};
    HANDLE event_ {nullptr};
    int slot_ {-1};
    bool logged_unavailable_ {false};
    std::chrono::steady_clock::time_point last_connect_attempt_ {};
  };

  /**
   * @brief Submits keyboard input to Interception's keyboard.sys filter (\\.\interception00..09).
   *
   * Strokes sent this way are not flagged as injected, matching the mouse client.
   * Returns false when no keyboard slot exists (e.g. headless host) so callers can fall back.
   */
  class interception_keyboard_client_t {
  public:
    interception_keyboard_client_t();
    ~interception_keyboard_client_t();

    interception_keyboard_client_t(const interception_keyboard_client_t &) = delete;
    interception_keyboard_client_t &operator=(const interception_keyboard_client_t &) = delete;

    /** @param modcode Windows virtual-key code as received from the client. */
    bool key(std::uint16_t modcode, bool release, bool extended);

  private:
    struct keyboard_input_data_t {
      std::uint16_t unit_id;
      std::uint16_t make_code;
      std::uint16_t flags;
      std::uint16_t reserved;
      std::uint32_t extra_information;
    };

    bool ensure_connected_locked();
    void disconnect_locked();

    std::mutex mutex_;
    HANDLE handle_ {INVALID_HANDLE_VALUE};
    HANDLE event_ {nullptr};
    int slot_ {-1};
    bool logged_unavailable_ {false};
    std::chrono::steady_clock::time_point last_connect_attempt_ {};
  };

}  // namespace platf::windows
