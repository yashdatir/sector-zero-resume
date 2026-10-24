# ⚡ Sector-Zero-Resume

> A 32-bit x86 bare-metal operating system that turns a developer resume into a bootable floppy disk image running directly on physical hardware and WebAssembly.

---

## 💻 Overview

**Sector-Zero-Resume** is a custom, freestanding operating system kernel built from scratch without standard libraries, runtime environments, or an underlying OS. Booting directly from raw storage sectors, it initializes 32-bit Protected Mode, maps an interactive terminal to the hardware VGA buffer (`0xB8000`), interfaces with the 8254 Programmable Interval Timer (PIT) for PC speaker audio feedback, and features a fully playable Snake mini-game.

The disk image is also configured to run seamlessly inside any modern web browser via WebAssembly (`v86`), bridging low-level systems engineering with the open web.

---

## ✨ Features

- **Direct Hardware Boot:** Custom 16-bit real-mode MBR bootloader transitioning the CPU to 32-bit Protected Mode via GDT.
- **Zero Abstraction UI:** 80×25 text-mode console engine writing directly to memory-mapped VGA RAM at `0xB8000`.
- **Keyboard I/O Driver:** Polling keyboard scan codes directly via hardware port `0x60` for tab switching and navigation.
- **Hardware Sound Effects:** PC speaker frequency synthesis using PIT Channel 2 (ports `0x42`/`0x43`) and Control Port B (`0x61`).
- **Interactive Mini-Game:** Embedded Snake game with real-time frame pacing, dynamic collision logic, and scoring.
- **Web-Ready:** Emulated out of the box in the browser via `v86` WebAssembly with zero host installation required.

---

## 🛠️ Architecture & Toolchain

- **Languages:** C, x86 Assembly (NASM)
- **Target Mode:** 32-bit Protected Mode (i386)
- **Compilers & Linkers:** GCC (`-ffreestanding`, `-m32`, `-fno-pie`), GNU `ld`
- **Emulation & Testing:** QEMU, SeaBIOS, `v86` (WebAssembly)