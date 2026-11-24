# novaling Virtual HID Mouse Driver (KMDF + VHF)

Windows 10/11 kernel-mode virtual HID mouse driver built on **KMDF 1.15** and Microsoft's **Virtual HID Framework (VHF, `vhfkm.lib`)**.

## Features
* **Single VHF HID device with two Top-Level Collections (TLCs)**:
  * **Report ID 1 (Relative Mouse)**: 5 buttons (L/R/M/X1/X2), 16-bit signed `dX`/`dY` (`-32767..32767`), high-resolution Vertical Wheel + Horizontal Wheel (`AC Pan`) with HID Resolution Multiplier (`Report ID 3`, 120 units/notch = Windows `WHEEL_DELTA`).
  * **Report ID 2 (Absolute Mouse)**: 16-bit absolute `X`/`Y` (`0..32767`) for pointer positioning. Button state is driven exclusively through the Relative TLC to prevent cross-collection stuck buttons.
* **Admin/SYSTEM-only device interface** (`SDDL: D:P(A;;GA;;;SY)(A;;GA;;;BA)`), matching `NovalingService` running as `SYSTEM`.
* **Automatic button reset on handle close** (`EvtFileClose`) so buttons never stay stuck if the host restarts or crashes.

## Build Requirements (Windows)
1. Visual Studio 2022 with **Desktop development with C++**
2. **Windows Driver Kit (WDK) 10/11** matching your installed Windows SDK

## Build & Test-Sign Steps
```powershell
# 1. Build Release|x64 (or ARM64) in Developer PowerShell for VS 2022
msbuild .\novaling_vhid_mouse.vcxproj /p:Configuration=Release /p:Platform=x64

# 2. Enable Windows Test Signing (one-time, requires reboot & Secure Boot off)
bcdedit /set testsigning on

# 3. Test-sign the built driver package
.\scripts\sign-test-driver.ps1 -DriverPackageDir .\x64\Release\novaling_vhid_mouse

# 4. Install the Root\NovalingVhidMouse virtual device (Run as Administrator)
.\scripts\install-mouse-driver.ps1 -InfPath .\x64\Release\novaling_vhid_mouse\novaling_vhid_mouse.inf
```

## Uninstall
```powershell
.\scripts\uninstall-mouse-driver.ps1
```

## Verification
* Open **Device Manager → Human Interface Devices** and confirm **Novaling Virtual HID Mouse** and its child **HID-compliant mouse** devices are running with no warning icon.
* Start `novaling.exe` (or `NovalingService`) with `mouse_driver = auto` (default) or `mouse_driver = novaling_vhid` in `novaling.conf`.
* Check `novaling.log` for: `Connected to novaling Virtual HID Mouse driver`.
