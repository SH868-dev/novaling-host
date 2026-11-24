/**
 * @file src/platform/windows/vhid_mouse.h
 * @brief Windows user-mode client for the novaling KMDF + VHF virtual HID mouse driver.
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
   * @brief Manages communication with the novaling KMDF + VHF virtual HID mouse driver.
   */
  class vhid_mouse_client_t {
  public:
    vhid_mouse_client_t();
    ~vhid_mouse_client_t();

    vhid_mouse_client_t(const vhid_mouse_client_t &) = delete;
    vhid_mouse_client_t &operator=(const vhid_mouse_client_t &) = delete;

    /**
     * @brief Check whether a handle to the virtual mouse driver is currently open
     *        (attempting a rate-limited reconnect if disconnected).
     */
    bool is_connected();

    /**
     * @brief Submit a relative mouse movement via the VHF Relative Mouse TLC.
     * @return True when submitted to the driver; false if unavailable or failed.
     */
    bool move_relative(int delta_x, int delta_y);

    /**
     * @brief Submit an absolute mouse position via the VHF Absolute Mouse TLC.
     * @param touch_port Streamed display and virtual-desktop geometry.
     * @param x Native desktop X coordinate.
     * @param y Native desktop Y coordinate.
     * @param allow_multimonitor_fallback When true, returns false if coordinates fall
     *        outside the primary monitor so callers may fall back to virtual-desktop injection.
     * @return True when submitted to the driver; false if unavailable, out-of-range, or failed.
     */
    bool move_absolute(const touch_port_t &touch_port, float x, float y, bool allow_multimonitor_fallback);

    /**
     * @brief Submit a mouse button press or release via the VHF Relative Mouse TLC.
     * @return True when submitted to the driver; false if unavailable or failed.
     */
    bool button(int button, bool release);

    /**
     * @brief Submit a vertical high-resolution wheel scroll (120 = 1 notch).
     * @return True when submitted to the driver; false if unavailable or failed.
     */
    bool scroll(int high_res_distance);

    /**
     * @brief Submit a horizontal high-resolution wheel scroll (120 = 1 notch).
     * @return True when submitted to the driver; false if unavailable or failed.
     */
    bool hscroll(int high_res_distance);

    /**
     * @brief Release all buttons and reset driver state.
     */
    void reset();

  private:
    bool ensure_connected_locked();
    void disconnect_locked();
    bool send_rel_locked(std::int16_t dx, std::int16_t dy, std::int16_t vwheel, std::int16_t hwheel, std::uint8_t buttons);
    bool send_abs_locked(std::uint16_t abs_x, std::uint16_t abs_y, std::uint8_t buttons);

    std::mutex mutex_;
    HANDLE handle_ {INVALID_HANDLE_VALUE};
    std::uint8_t buttons_ {0};
    bool last_was_absolute_ {false};
    std::uint16_t last_abs_x_ {0};
    std::uint16_t last_abs_y_ {0};
    bool logged_unavailable_ {false};
    bool logged_multimonitor_fallback_ {false};
    std::chrono::steady_clock::time_point last_connect_attempt_ {};
  };

}  // namespace platf::windows
