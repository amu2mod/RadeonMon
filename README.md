# RadeonMon

A lightweight Windows monitoring application for AMD Radeon GPUs, built using the native Win32 API and AMD Display Library (ADLX).

Designed for low-overhead real-time monitoring with minimal CPU and GPU resource usage, keeping performance impact as low as possible.

The interface is fully resizable, with large, easy-to-read text and DPI awareness for each monitor, ensuring crisp and sharp display across different screen sizes and scaling settings.

Monitor GPU metrics such as temperatures, VRAM temp and power consumption. RadeonMon is read-only and does not modify GPU settings or apply configuration changes. 

Includes a lightweight local web interface for remote monitoring from a phone or a tablet on your local network.

This is an independent personal project and is not affiliated with AMD Corporation.

---

## Overview

# Windows Desktop Application

<table align="center">
    <tr>
          <td align="center">
            <img height="596" alt="idle state" src="https://github.com/user-attachments/assets/5ceec5cb-60bc-4a6c-ab6a-44f50bcbca43" />
            <br />
             <sub><b>Idle GPU state showing minimal power consumption</b></sub>
          </td>
          <td align="center">
            <img height="596" alt="while gaming" src="https://github.com/user-attachments/assets/5e9e1b93-7bad-4c04-ab43-26e16cc2c0d8" />
            <br />
             <sub><b>Metrics while gaming</b></sub>
          </td>
          <td align="center">
            <img height="596" alt="Warning and alert triggered" src="https://github.com/user-attachments/assets/0653da69-feb9-4119-aa86-9014999624b1" />
            <br />
            <sub><b>Warning and alert triggered</b></sub>
          </td>
    </tr>
    <tr>
      <td colspan="3" align="center">
          <br /> <!-- Top padding -->
        <img width="600" alt="Advanced CPU metrics" src="https://github.com/user-attachments/assets/1010a7a3-5471-449d-b21d-3ef2b9dc1d09" />
        <br />
        <sub><b>CPU graph after clicking the CPU label</b></sub>
      </td>
    </tr>
    <tr>
      <td colspan="3" align="center">
          <br /> <!-- Top padding -->
          <img width="581" alt="GPU metrics panel showing all GPU statistics" src="https://github.com/user-attachments/assets/0e42c333-51be-43c0-b295-6a8c57056d58" />
        <br />
       <sub><b>GPU metrics panel displayed after clicking the GPU Temperature label</b></sub>
      </td>
    </tr>
</table>

# Web Interface
<table align="center">
  <tr>
    <td align="center">
      <img height="400" alt="Mobile Web Interface" src="https://github.com/user-attachments/assets/2c376468-a5b2-4e0a-adff-104ed4530304" />
      <br />
      <sub>Ultra-lightweight template optimized for mobile devices</sub>
    </td>
    <td align="center">
     <img width="500" alt="Dashboard Web Interface" src="https://github.com/user-attachments/assets/51f5bfb0-3d6f-42f0-9a4f-c1d340ac6e2c" />
      <br />
      <sub>Full-featured HTML dashboard with advanced CSS styling</sub>
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
- Intel PresentMon Service enabled (optional)

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
- Intel PresentMon SDK with the following folder copied /third_party/Intel/PresentMon-2.5.1/IntelPresentMon/

### Build steps

cmake --preset msvc-release

cmake --build build/release
