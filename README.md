# RadeonMon

A lightweight Windows monitoring application for AMD Radeon GPUs, built using the native Win32 API and AMD Display Library (ADLX).

Designed for low-overhead real-time monitoring with minimal CPU and GPU resource usage, keeping performance impact as low as possible.

Monitor GPU metrics such as temperatures, VRAM temp and power consumption. RadeonMon is read-only and does not modify GPU settings or apply configuration changes. 

Includes a lightweight local web interface for remote monitoring from a phone or a tablet on your local network.

This is an independent personal project and is not affiliated with AMD Corporation.

---

## Overview

# Windows Desktop Application

<table align="center">
    <tr>
          <td align="center">
            <img width="500" alt="Windows Main App" src="https://github.com/user-attachments/assets/f362581a-8627-4b04-b0ee-24113aa52e1a" />
            <br />
            <sub><b>Desktop App</b> — Normal usage</sub>
          </td>
          <td align="center">
            <img width="500" alt="Warning and alert triggered" src="https://github.com/user-attachments/assets/1dba23ea-a731-408d-af93-2778db80c18a" />
            <br />
            <sub><b>Desktop App</b> — Warning and alert triggered</sub>
          </td>
    </tr>
</table>

# Web Interface
<table align="center">
  <tr>
    <td align="center">
      <img height="400" alt="Mobile Web Interface" src="https://github.com/user-attachments/assets/2c376468-a5b2-4e0a-adff-104ed4530304" />
      <br />
      <sub><b>Web Interface</b> — Ultra-lightweight template optimized for mobile devices</sub>
    </td>
    <td align="center">
     <img width="500" alt="Dashboard Web Interface" src="https://github.com/user-attachments/assets/51f5bfb0-3d6f-42f0-9a4f-c1d340ac6e2c" />
      <br />
      <sub><b>Web Interface</b> — Full-featured HTML dashboard with advanced CSS styling</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="middle">
    <img width="1025" height="453" alt="image" src="https://github.com/user-attachments/assets/75247373-1633-4f5b-8e1b-ccb4693615ce" />
      <br />
      <sub><b>FPS meter</b></sub>
    </td>
    <td colspan="2" align="center" valign="middle">
      <img width="600" alt="image" src="https://github.com/user-attachments/assets/1e4000f0-481e-44d9-9ba8-9ad76031e6df" />
      <br />
      <sub><b>Top Processes</b> — Highlights resource-heavy processes to quickly identify potential performance bottlenecks</sub>
    </td>
  </tr>
</table>

## Features

- GPU Temperature sensor
- GPU hotspot sensor with delta calculation
- GPU Memory sensor
- Fan Speed
- Total Power usage
- CPU metrics (Ryzen support only for now)
- Display information, current resolution and refresh rate
- Low overhread FPS meter (metrics provided by AMD ADLX)
- Highlights high temperatures with a warm color
- Built-in local web server running on port `9090`
- Additional metrics available through the web interface

## How to use the local Web Server

Right-click the application to open the context menu, open the Web Server submenu, and select the network interface you want to bind to.

Administrator privileges are required to start the local web server.

Once enabled, the web interface URL will be displayed in the application.

<img width="466" alt="Web Server" src="https://github.com/user-attachments/assets/4ed7b779-a5d6-4b29-8bd1-96faa22fe1e4" />


To access the web interface from another device on the network (for example, a smartphone), you must allow incoming connections through Windows Firewall.

Open Windows Terminal with administrator privileges and run the following command:

```powershell
New-NetFirewallRule -DisplayName "RadeonMon Web Server" -Direction Inbound -Protocol Tcp -LocalPort 9090 -Action Allow
```

> [!NOTE]
> The web interface is only accessible on your local network. No internet connection or external service is required.

## Releases

[Download latest release](https://github.com/amu2mod/RadeonMon/releases/latest)

## Requirements

- Windows 10/11 64-bit
- AMD GPU supported by ADLX
- Recent AMD graphics drivers
- [AMD Ryzen™ Master Monitoring SDK](https://www.amd.com/en/developer/ryzen-master-monitoring-sdk.html) installed (required only for CPU metrics). 
- Administrator privileges (required for CPU metrics and the local web server)

Tested with an RX 9070 XT running Windows 10.

## How to Build

This project uses CMake and can be built using MSVC with Ninja.

### Prerequisites

- ADLX 1.5 SDK extracted in the folder /third_party/AMD/ADLX-1.5/
- CMake (≥ 3.25)
- MSVC (Visual Studio Build Tools)
- Ninja build system
- VS Code (optional)
- AMD Ryzen™ Master Monitoring SDK extracted in the folder /third_party/AMD/RyzenMasterMonitoringSDK/ (include and lib)

### Build steps

cmake --preset msvc-release

cmake --build build/release
