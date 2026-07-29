# Ultrasonic Cavitation Generator Control System

An open-source, high-performance dual-controller system designed for precise control, telemetry, and digital signal processing (DSP) of a water-cooled magnetostrictive ultrasonic transducer.

## 🛠 System Architecture

The project is split into two tightly coupled subsystems operating over a high-speed isolated UART telemetry link:

1. **STM32F446 Subprocessor (`firmware_stm32`)**:
   - Manages high-frequency PWM generation for the transducer driver stage.
   - Executes real-time ADC sampling of hydrophone sensor feedback.
   - Processes signals locally using the hardware FPU and **CMSIS-DSP** library (Notch filtering and RMS calculation).
2. **ESP32 Communication Node (`firmware_esp32`)**:
   - Hosts an autonomous, standalone asynchronous Web Server.
   - Provides a responsive HTML5 user interface with a high-performance, native SVG-based chart renderer (no external internet dependencies like Chart.js).
   - Manages parameters (power levels, scan execution commands) and streams real-time DSP telemetry over WebSockets.

## 📡 Hardware Wiring & Pinout

### Telemetry Link (Cross-Wired UART)

| STM32F446RET6 (UART4) | ESP-WROOM-32 (UART2) | Signal Description |
|-----------------------|----------------------|--------------------|
| **PC10 (TX)**         | **GPIO16 (RX2)**     | Telemetry Data Stream |
| **PC11 (RX)**         | **GPIO17 (TX2)**     | Configuration Commands |
| **GND**               | **GND**              | Common Ground Reference |

### STM32 Peripherals
- **PA1**: ADC Input (Hydrophone Sensor Feedback)
- **PA13/PA14**: SWDIO/SWCLK Hardware Debugging Interface (DAPLink/OpenOCD)

## 🚀 How to Build and Deploy

This project is built natively using the **PlatformIO** ecosystem inside Visual Studio Code.

### Firmware Compilation
1. Open Visual Studio Code and select `File -> Open Folder...`.
2. Choose either `firmware_stm32` or `firmware_esp32` depending on your target.
3. Click the PlatformIO **Build** icon (checkmark) in the bottom status bar.

### Hardware Debugging (STM32)
A preconfigured `.vscode/launch.json` is provided to enable hardware debugging via **DAPLink** and **Cortex-Debug**:
1. Connect your DAPLink probe to the STM32 target.
2. Select the `DAPLink Hardware Debug` configuration from the Run & Debug panel.
3. Press **F5** to launch the interactive GDB debugging session.

## 📄 License
This project is licensed under the GNU General Public License v3.0 - see the [LICENSE.txt](LICENSE.txt) file for details.
