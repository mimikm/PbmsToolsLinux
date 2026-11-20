# PbmsTools

A Linux command-line tool for communicating with **Pace BMS** hardware over a serial port. Reverse-engineered from `PbmsTools V2.5` (the original Windows GUI).

The focus is on battery health and balancing control.

## Building

Requires CMake ≥ 3.14, a C++17 compiler and the ncurses headers (for the `tui` subcommand).
The graphical interface additionally needs Qt 5 or Qt 6 Widgets; it is built automatically when Qt is found.

On Ubuntu / Debian:

```sh
sudo apt install build-essential cmake libncurses-dev qtbase5-dev
cmake -B pbmstool/build pbmstool && cmake --build pbmstool/build -j
```

Binaries:

- `pbmstool/build/pbmstool` – command-line tool and ncurses TUI
- `pbmstool/build/pbmstool-gui` – graphical interface (only if Qt was found)

To install both system-wide, including a menu entry for the GUI:

```sh
sudo cmake --install pbmstool/build
```

Skip the GUI with `-DPBMS_BUILD_GUI=OFF`.

### Serial port permissions

On Ubuntu your user must be in the `dialout` group to open USB serial adapters:

```sh
sudo usermod -aG dialout $USER   # then log out and back in
```

## Graphical interface

```sh
pbmstool-gui
```

Pick the adapter (e.g. `/dev/ttyUSB0` or a stable `/dev/serial/by-id/...` path) and the baud rate, then click **Connect**. The GUI discovers the packs and has three tabs:

- **Live** – per-cell voltage bars (coloured for balancing, warnings and faults), pack voltage, current, power, SOC, capacity, cycles, cell spread and temperatures. Auto-refreshes every 1–60 s.
- **Alarms** – MOSFET and balancing state, protection events, hardware faults, per-cell alarms and pack/temperature limit states.
- **Parameters** – every protection, balancing and temperature group supported by the CLI. Edited groups are highlighted; **Write** asks for confirmation, writes only the changed groups and reads all values back.

All serial I/O runs on a background thread, so the window stays responsive while waiting for the BMS. Parameter writes use broadcast address 0, exactly like `params set`, so every pack on the bus receives them.

## Usage

```
pbmstool --port <dev> [--baud <N>] [--timeout <ms>] [--pack <N>] <subcommand>
```

**Global flags:**

| Flag | Default | Description |
|---|---|---|
| `--port` | *(required)* | Serial device, e.g. `/dev/ttyUSB0` |
| `--baud` | `9600` | Baud rate |
| `--timeout` | `2000` | Read timeout in milliseconds |
| `--pack` | `1` | Target pack address (1–15); ignored by `discover` |

## Supported Subcommands

| Subcommand | Description |
|---|---|
| `discover` | Query pack count → prints `pack_count=N` |
| `info` | Read firmware version string → prints `pack=N`, `version=...` |
| `analog` | Read live data → cell voltages (mV), temperatures (°C), current (A), total voltage (mV), remaining/full/design capacity (mAh), cycle count |
| `alarm` | Read alarm/status flags → per-cell voltage alarms, temperature alarms, charge/discharge current state, 9 status bytes |
| `params get` | Read balancer parameters → `balance_threshold_v`, `balance_delta_mv` |
| `params set balance-threshold=X.XXX [balance-delta=N]` | Write balancer parameters (reads current values for any omitted param) |
| `tui` | Interactive full-screen terminal UI (live data, alarms, parameter editing) |

All output is plain-text `key=value` lines on stdout. Errors go to stderr with a non-zero exit code.

## Examples

```sh
# Detect how many packs are connected
pbmstool --port /dev/ttyUSB0 discover

# Read live analog data from pack 2
pbmstool --port /dev/ttyUSB0 --pack 2 analog

# Get current balance settings
pbmstool --port /dev/ttyUSB0 params get

# Set balance threshold to 3.400 V, keep current delta
pbmstool --port /dev/ttyUSB0 params set balance-threshold=3.400

# Set both threshold and delta
pbmstool --port /dev/ttyUSB0 params set balance-threshold=3.400 balance-delta=30
```

## Protocol

Pace ASCII protocol over RS-232/RS-485. Frames are delimited by `0x7E` (start) and `0x0D` (end), with all payload bytes hex-encoded as uppercase ASCII pairs. The tool implements checksum validation and retries up to 2 times on timeout or checksum mismatch.
