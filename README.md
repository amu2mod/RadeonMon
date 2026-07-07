# RadeonMon

An ultra-lightweight Windows monitoring application for AMD Radeon RDNA4 graphics cards on Windows.

This is a personal project developed independently, with no affiliation with AMD Corporation.

---

## Overview

<img width="386" height="393" alt="image" src="https://github.com/user-attachments/assets/cf773ba5-3434-4298-9f29-45bb64086653" />


RadeonMon is a lightweight C++ utility that uses AMD’s ADLX SDK to provide critical metrics for Radeon RDNA4 graphics cards.

It is designed to be very low-overhead with minimal impact on low fps. 

## Features

- GPU Temperature sensor
- GPU Hotspot sensor + delta
- GPU Memory sensor
- Fan Speed
- Total Power usage
- CPU metrics (Ryzen support only for now)

## Releases

- [radeonmon-1.0.0_x64.zip](https://github.com/amu2mod/RadeonMon/releases/download/1.0.0/radeonmon-1.0.0_x64.zip) → Main application (no console)
- [radeonmon-1.0.0_x64_debug.zip](https://github.com/amu2mod/RadeonMon/releases/download/1.0.0/radeonmon-1.0.0_x64_debug.zip) → Debug build with console output (for logs and user feedback)


## Requirements

- Windows 10 / 11 64-bit
- AMD Radeon GPU (RDNA4 targeted)
- Recent AMD drivers
- AMD Ryzen™ Master Monitoring SDK (for CPU metrics) installed. Get installer [here](https://www.amd.com/en/developer/ryzen-master-monitoring-sdk.html)

Tested with a RX 9070 XT on Windows 10.

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
