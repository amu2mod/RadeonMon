# RadeonMon

A very lightweight Windows monitoring application for AMD Radeon RDNA4 graphics cards on Windows.

This is a personal project developed independently, with no affiliation with AMD Corporation.

---

## Overview
<div align="center">
<img width="555" height="570" alt="image" src="https://github.com/user-attachments/assets/3de05f73-60b1-4d97-b34e-0adc1299a445" />
</div>

RadeonMon is a lightweight C++ utility that uses AMD’s ADLX SDK to provide essential metrics for Radeon RDNA 4 graphics cards.

It is designed to have a very low overhead with minimal impact on low fps. 

## Features

- GPU Temperature sensor
- GPU hotspot sensor with delta calculation
- GPU Memory sensor
- Fan Speed
- Total Power usage
- CPU metrics (Ryzen support only for now)
- Highlights high temperatures with a warm color

## Releases

[Download latest release](https://github.com/amu2mod/RadeonMon/releases/latest)

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
