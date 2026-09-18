# NoCapKVM

NoCapKVM is a C++20 software KVM for a small, trusted local network. One **Master** computer owns the physical keyboard and mouse. One or more **Slave** computers receive the input, return system audio to the Master, and can synchronize clipboard content.

Move the pointer across the edge of the Master's configured virtual screen layout to transfer control to a Slave. The application is intended for personally controlled, physically adjacent devices on a private LAN; it is not a public-network remote-desktop product.

## Current capabilities

- UDP LAN discovery for Masters and Slaves.
- First-use pairing with a short visual fingerprint and persisted peer trust.
- Noise IK authenticated key exchange and ChaCha20-Poly1305 authenticated encryption for control traffic.
- Reliable TCP delivery for keyboard, mouse, monitor, clipboard, and control messages.
- Native input capture and injection: Win32 hooks and `SendInput` on Windows; libevdev input handling on Linux.
- Configurable virtual screen arrangement and pointer-edge handoff.
- Slave-to-Master system-audio streaming over encrypted UDP with a jitter buffer.
- Text and image clipboard synchronization.
- Desktop UI built with GLFW and Dear ImGui, plus tray integration.
- Runtime telemetry, rotating logs, and CTest coverage for core modules.

## Architecture

```text
Keyboard / mouse on Master
  -> input capture -> encrypted TCP control channel -> input injection on Slave

Slave display geometry -> encrypted TCP -> Master screen arrangement -> pointer handoff

Slave system audio -> encrypted UDP -> jitter buffer -> Master playback

Clipboard changes <-> encrypted TCP control channel <-> clipboard backend
```

The application executable in `src/app/` coordinates reusable libraries:

| Area | Responsibility |
| --- | --- |
| `discovery` | LAN announcements, identity, pairing, Noise IK, secure TCP sessions, and settings |
| `topology` | Screen placement, bounds, and boundary-crossing math |
| `input` | Native capture/injection and input message codecs |
| `audio` | Capture, encrypted UDP packets, jitter buffering, and playback |
| `clipboard` | Native clipboard access plus text and image conversion |
| `display` | Local monitor enumeration |
| `telemetry` | Counters, process samples, formatting, and rotating logs |

## Requirements

- CMake 3.20 or newer.
- A C++20 compiler.
- Git and network access during the first configure step. CMake downloads GLFW, Dear ImGui, libsodium, miniaudio, and stb with `FetchContent`.

### Windows

Use a C++20-capable Visual Studio or another CMake-supported toolchain. The build links the required Win32, Winsock, and OpenGL system libraries.

### Linux

The current Linux build targets X11 and requires development packages for X11/XrandR, libevdev, D-Bus, OpenGL, and pkg-config. On Debian or Ubuntu, install:

```sh
sudo apt install build-essential cmake git pkg-config libx11-dev libxrandr-dev libevdev-dev libdbus-1-dev libgl1-mesa-dev
```

Audio backends are selected by miniaudio at runtime. Install and run the appropriate local audio service or libraries for the system configuration.

## Build

Configure and build from the repository root:

```sh
cmake -S . -B build
cmake --build build --config Release
```

For a single-configuration generator such as Ninja or Unix Makefiles, `--config Release` is ignored and can be omitted.

Run the test suite:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The application target is named `nockvm_app`. With a Visual Studio generator, its executable is normally under `build/src/app/Release/`; with a single-configuration generator, it is normally under `build/src/app/`.

## First run

1. Build and start NoCapKVM on each computer connected to the same trusted LAN.
2. Select **Master** on the computer with the physical keyboard and mouse; select **Slave** on each controlled computer.
3. Discover the other device and approve pairing after visually comparing the displayed fingerprint on both machines.
4. On the Master, place each trusted Slave in the screen-arrangement view.
5. Move the pointer through the configured edge to transfer input. Configure audio and clipboard behavior from the application UI.

The first pairing records a peer's public key locally. Review and remove trusted peers through the Manage Devices view when a device is no longer trusted.

## Local data and logs

NoCapKVM stores its device identity, static keypair, known peers, user settings, screen arrangement, and logs outside the repository:

- Windows: `%APPDATA%/NoCapKVM`
- Linux: `$XDG_CONFIG_HOME/nockvm`, or `$HOME/.config/nockvm`

Set `NOCKVM_HOME` to override this directory. This is useful for isolated local testing, because each override has its own identity and trust store.

## Project layout

```text
include/nockvm/  Public library interfaces
src/             Library implementations and the desktop application
tests/           CTest executables for core behavior and regressions
res/             Application icons and Linux desktop-entry resources
docs/            Design and coding guidance
```

Read [the design document](docs/project-brief.md) for the trusted-LAN model, protocol choices, and intended roadmap. The ignored `tmp/` directory may contain local, generated code-review notes when working in this repository.

## Security scope

NoCapKVM authenticates paired devices and encrypts application traffic, but it assumes that the local machines and private LAN are trusted. Do not expose its discovery or control traffic to untrusted networks, and do not treat it as a hardened public remote-access solution.
