<p align="center">
  <img src="assets/ridux-banner.png" alt="RiduxOS Banner" width="100%">
</p>

<h1 align="center">RiduxOS</h1>

<p align="center">
  <b>A modern operating system built from scratch.</b>
  <br>
  Independent kernel • Native desktop • Linux compatibility • Vulkan-first design
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-active-red">
  <img src="https://img.shields.io/badge/arch-x86__64-blue">
  <img src="https://img.shields.io/badge/kernel-monolithic%20modular-green">
  <img src="https://img.shields.io/badge/license-MIT-orange">
</p>

---

# What is RiduxOS?

RiduxOS is a modern operating system developed from scratch by **Korinel Software**.

The project aims to provide a complete desktop operating system with its own kernel, desktop environment, graphical stack, compatibility layers, development tools, and cloud ecosystem.

Unlike traditional Linux distributions, RiduxOS is designed as an independent platform where every major component is built specifically for the system.

---

# Vision

RiduxOS is built around five principles:

- 🚀 Performance First
- 🎨 Modern User Experience
- 🔒 Security by Design
- ⚡ Vulkan Everywhere
- 🧩 Compatibility Without Compromise

The goal is to create a system capable of running modern applications while maintaining a clean and coherent architecture.

---

# Core Components

| Component | Description |
|------------|------------|
| Ridux Kernel | Custom operating system kernel |
| Injury | Desktop Environment |
| RedGirl | Wayland compositor |
| Maze | Linux compatibility layer |
| Gate | Planned Windows compatibility layer |
| Seage | Native web browser |
| Warehouse | Cloud ecosystem |
| Harvey | AI assistant |
| Scarlet | Native UI toolkit |
| Red Moon | Game engine |

---

# Features

## Kernel

- x86_64 architecture
- Virtual memory manager
- SMP support
- Ring 3 user processes
- System call interface
- APIC / IOAPIC
- Process scheduler
- PCI subsystem
- ATA storage drivers
- Networking stack

## Graphics

- Vulkan-first architecture
- Wayland support
- GPU acceleration
- HDR pipeline research
- Atomic display management
- Multi-monitor support

## Compatibility

### Maze (Linux Compatibility)

- ELF64 support
- Dynamic linking
- pthreads
- epoll
- futex
- signals
- sockets
- Wayland applications
- X11 applications
- XWayland

Applications tested include:

- Firefox
- Chromium
- KDE Applications
- Qt Applications
- Hyprland Components
- Wayland Clients

### Gate (Planned)

Future compatibility subsystem designed for Windows applications.

---

# Desktop Environment

## Injury

Injury is the native desktop environment of RiduxOS.

Features include:

- Native panels
- Notification system
- Application launcher
- Window management
- Custom themes
- Native widgets
- Vulkan rendering

---

# RedGirl

RedGirl is the native Wayland compositor.

Features:

- Window tiling
- Floating windows
- Layer-shell support
- XDG-shell support
- Snap layouts
- Multi-monitor support
- Custom scene graph

---

# Native Applications

Current ecosystem includes:

- Seage Browser
- Terminal
- File Manager
- Settings
- Calendar
- Calculator
- Notification Center
- System Monitor

More applications are under active development.

---

# Architecture

```text
┌────────────────────────────┐
│      User Applications     │
└─────────────┬──────────────┘
              │
┌─────────────▼──────────────┐
│          Injury            │
│        RedGirl WM          │
└─────────────┬──────────────┘
              │
┌─────────────▼──────────────┐
│      Ridux Userland        │
└─────────────┬──────────────┘
              │
┌─────────────▼──────────────┐
│       Ridux Kernel         │
└─────────────┬──────────────┘
              │
┌─────────────▼──────────────┐
│         Hardware           │
└────────────────────────────┘
Current Status

RiduxOS is currently under active development.

Implemented:

Kernel boot
Memory management
SMP
Process execution
Networking
Filesystems
Wayland stack
Linux compatibility layer
Native desktop environment

In Progress:

Hardware acceleration improvements
Package management
Installer
Native SDK
Additional desktop applications

Research:

Windows compatibility
Advanced GPU stack
Cloud integration
AI integration
Screenshots
[ Screenshots coming soon ]
Building
git clone https://github.com/KorinelSoftware/RiduxOS.git
cd RiduxOS

mkdir build
cd build

cmake ..
cmake --build . -j$(nproc)
Roadmap
Phase 1
Kernel foundation
Basic desktop
Linux compatibility
Phase 2
GPU acceleration
Native applications
Installer
Phase 3
Public alpha release
SDK
Documentation
Phase 4
Full desktop experience
Cloud ecosystem
Advanced compatibility
Contributing

RiduxOS is currently maintained primarily by its creator.

Contributions, testing, bug reports and discussions are welcome.

License

MIT License

Copyright (c) Korinel Software

<p align="center"> Built with caffeine, curiosity and an unreasonable amount of C++. </p> ```
