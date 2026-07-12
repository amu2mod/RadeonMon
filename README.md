# RadeonMon

A lightweight Windows monitoring application for AMD Radeon GPUs, built using the native Win32 API and AMD Display Library (ADLX).

Designed for low-overhead real-time monitoring with minimal CPU and GPU resource usage, keeping performance impact as low as possible.

Monitor GPU metrics such as temperatures, VRAM temp and power consumption. RadeonMon is read-only and does not modify GPU settings or apply configuration changes. 

Includes a lightweight local web interface for remote monitoring from a phone or a tablet on your local network.

This is an independent personal project and is not affiliated with AMD Corporation.

---

## Overview
<table align="center">
  <tr>
    <td align="center">
      <img width="453" alt="image" src="https://github.com/user-attachments/assets/0ecdefb5-eed7-4b38-a024-942b534c633f" />
      <br />
      <sub><b>Desktop App</b> — Main Application Window</sub>
    </td>
    <td align="center">
     <img width="450"  alt="image" src="https://github.com/user-attachments/assets/bc160339-45b8-4e67-91fb-56cca84e7900" />
      <br />
      <sub><b>Web Interface</b> — HTML Dashboard</sub>
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
- Highlights high temperatures with a warm color
- Local Web Server on port 9090

## How To use the local Web Server

Right-click the application to open the context menu, navigate to the Web Server submenu, and select the network interface you want to bind to.

The application must be run with administrator privileges in order to start the local Web Server.

The URL will be displayed in the application in green.

To access the web interface from another device on the network (for example, a smartphone), you need to add a Windows Firewall rule.

Open Windows Terminal with administrator privileges and run the following command:

```powershell
New-NetFirewallRule -DisplayName "RadeonMon Web Server" -Direction Inbound -Protocol Tcp -LocalPort 9090 -Action Allow
```

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
