# PicoSpot 20 DMX Control

This module drives a **PicoSpot 20 moving head** via **DMX512** in **9-channel mode**.  
The implementation focuses exclusively on transmitting valid DMX frames to the fixture, with correct break and mark-after-break sequences, and with channel values for pan, tilt, color, gobo, dimmer, and shutter.

## Overview

The PicoSpot 20 is controlled from a **PSoC 6** via a UART output at DMX baud rate.  
Because DMX uses a differential RS485 signal, a **Pico RS485 2CH module** is placed between the microcontroller and the moving head.

In this setup:

- `P9_1` is the **hardware UART TX** of the PSoC 6
- this TX line is connected to the **Pico RS485 2CH module**
- the RS485 module handles the physical DMX signal conversion toward the PicoSpot 20

The relevant defines in the code are:

```c
#define DMX_TX_PIN          P9_1
#define DMX_RX_PIN          P9_0
#define DMX_BAUDRATE        250000
#define DMX_UNIVERSE_SIZE   513
#define DMX_START_CODE      0x00
```

## Hardware Connection

The DMX output uses the PSoC 6 UART at the standard DMX baud rate of **250000 baud**, with **8 data bits**, **no parity**, and **2 stop bits**.  
This matches the classic DMX512 serial format.

The signal path is:

1. PSoC 6 UART TX on `P9_1`
2. Pico RS485 2CH module
3. DMX line to the PicoSpot 20 moving head

`P9_0` is declared as an RX pin in the configuration but is functionally not used for DMX control, since this application only **transmits** frames.

## DMX Frame Structure

A DMX frame in this implementation consists of:

- a **break**
- a **mark after break (MAB)**
- a **start code**
- up to 512 DMX data bytes

The DMX buffer therefore has a size of 513 bytes:

- byte 0: start code (`0x00`)
- byte 1 to 512: channel data

In code:

```c
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];
```

and:

```c
dmx_buffer = DMX_START_CODE;
```

Channel values are then filled in at their DMX address.

## DMX Timings

For a valid DMX frame, the timing requirements for the break and the mark-after-break must be correctly respected.  
In this implementation, these timings are explicitly generated using a hardware timer, after which the UART is used again to transmit the actual frame.

The timing defines used are:

```c
#define DMX_BREAK_US        200
#define DMX_MAB_US          12
#define DMX_REFRESH_MS      20
```

This means:

- **Break**: 200 µs
- **Mark After Break**: 12 µs
- **Frame refresh**: every 20 ms

More detailed explanations of the measured or verified DMX timings are in:

- [`tests/Captures/DMXTimings.md`](../../tests/Captures/DMXTimings.md)

This file documents the break, the period after the break, and further timing characteristics of the transmitted DMX signal.

## How the Break Is Sent

A normal UART cannot transmit the DMX break as a standard data byte, because a break must stay low longer than a normal serial frame.  
Therefore, the software temporarily switches away from the UART and drives the TX pin directly as GPIO.

The sequence is:

1. Free the UART
2. Set `P9_1` as GPIO output low to generate the **break**
3. After `DMX_BREAK_US`, set the line high again for the **MAB**
4. After `DMX_MAB_US`, free the pin again
5. Re-initialize the UART
6. Transmit the full DMX frame

This is implemented in the functions:

- `dmx_send_break_mab()`
- `dmx_send_frame()`

## PicoSpot 20 in 9-Channel Mode

The fixture is used in **9-channel mode**.  
The channel mapping is:

| Channel | Function |
|---|---|
| 1 | Pan |
| 2 | Tilt |
| 3 | Pan fine |
| 4 | Tilt fine |
| 5 | Pan/Tilt speed |
| 6 | Color wheel |
| 7 | Gobo wheel |
| 8 | Dimmer |
| 9 | Shutter / Strobe |

A more complete description of these channels is in:

- [`docs/PicoSpot20.md`](../../docs/PicoSpot20.md)

This documentation is the reference for the meaning of DMX values per channel.

## Start Address and Channel Range

The PicoSpot 20 uses start address **1** in this implementation.  
Because the fixture uses 9 channels, it occupies DMX channels **1 to 9**.

The relevant defines are:

```c
#define PICOSPOT_ADDRESS    1
#define PICOSPOT_CHANNELS   9
```

The software then writes the 9 channel values starting from that address into the DMX buffer.

## Channel Meaning in Code

The code models a fixture command as a block of 9 bytes:

```c
typedef struct
{
    uint16_t address;
    uint8_t channel_values[PICOSPOT_CHANNELS];
} dmx_command_t;
```

A scene is then converted into these 9 DMX channels via `build_scene_command()`.

The mapping is:

- `channel_values[0]` → Pan
- `channel_values[1]` → Tilt
- `channel_values[2]` → Pan fine
- `channel_values[3]` → Tilt fine
- `channel_values[4]` → Pan/Tilt speed
- `channel_values[5]` → Color wheel
- `channel_values[6]` → Gobo wheel
- `channel_values[7]` → Dimmer
- `channel_values[8]` → Shutter/Strobe

## Example Static Scene

A scene contains all values needed to set the moving head to a certain position or look, such as pan/tilt position, color, gobo, and light output.

The software defines fixed scenes such as:

```c
{ .pan=128,.tilt=128,.pan_fine=0,.tilt_fine=0,.move_speed=0,
  .color=COLOR_CYAN,  .gobo=GOBO_OPEN,.dimmer=250,.shutter=SHUTTER_OPEN }
```

Such a scene is converted into a DMX command and then periodically resent, so the fixture remains stably in that state.

## Example Sweep Movement

In addition to static scenes, the controller also supports a sweep effect.  
During this sweep, pan and tilt change step by step, while color cycles cyclically.

During the sweep, these fields are dynamically adjusted:

- pan
- tilt
- color wheel
- dimmer
- shutter

The function `sweep_state_tick()` builds a new 9-channel DMX command for each step.

## Issues & Lessons Learned

### UART CLI and Hardware UART Conflict

During development, I encountered a conflict between the CLI UART (debug console) and the hardware UART used for DMX.

**Problem:**  
Initially, I connected `P5_0` to the Pico RS485 2CH module for DMX transmission. However, this broke the CLI UART console because the debug TX definition (`CYBSP_DEBUG_UART_TX`) was already in use by the same pin. As a result, the CLI terminal stopped working.

**Solution:**  
The solution was to move the DMX UART to `P9_1` (with `P9_0` as the unused RX pin). This freed up the debug TX definition for the CLI, allowing the CLI UART to work via the KitProg3 USB-UART bridge while the DMX UART operated independently on Port 9.

In the final configuration:

- CLI UART: uses `CYBSP_DEBUG_UART_TX` / `CYBSP_DEBUG_UART_RX` via KitProg3 (no extra wiring needed)
- DMX UART: uses `P9_1` (TX) connected to the Pico RS485 2CH module

### CapSense Integration

**Problem:**  
CapSense integration was never successfully implemented in this project. Despite multiple attempts, I could not get CapSense working alongside the DMX and FreeRTOS tasks. The exact cause remains unclear — it may be related to timing conflicts, GPIO resource contention, or interrupt priority issues with the DMX break timer and FreeRTOS scheduler.

**Status:**
CapSense is not functional in the current version. This remains an open issue for future work.


## Summary

This implementation correctly drives the PicoSpot 20 via DMX512 by:

- UART configuration at **250000 baud, 8N2**
- an explicitly generated **break** and **mark after break**
- a DMX buffer with **start code + 512 channels**
- a **9-channel mapping** specific to the **PicoSpot 20**
- output via the **Pico RS485 2CH module** from `P9_1` as hardware UART TX

For more detail on timings and channel meanings:

- [`tests/Captures/DMXTimings.md`](../../tests/Captures/DMXTimings.md)
- [`docs/PicoSpot20.md`](../../docs/PicoSpot20.md)