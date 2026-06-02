# LED Wall DMX Control

This module controls an LED strip by receiving DMX512 data and translating it into LED output values. The system reads DMX frames via UART and maps selected channels to RGB values on the LED strip.

## Overview

The controller runs on a raspberry pi pico and receives DMX through an RS485 interface. The incoming DMX signal is decoded and stored in a buffer, after which relevant channels are used to drive the LED strip.

## Hardware Setup

The signal chain is:

```text
DMX input → RS485 receiver module
RS485 module → UART RX pin
Microcontroller → LED strip (WS2812B)
```

UART is configured for standard DMX:

- 250000 baud
- 8 data bits
- 2 stop bits

## DMX Frame Handling

Each DMX frame consists of:

- Break and mark-after-break (handled by hardware/UART detection)
- Start code (ignored if not `0x00`)
- Up to 512 channel values

The data is stored in a buffer:

- Byte 0: Start code
- Byte 1–512: Channel data

## Channel Mapping

A fixed range of DMX channels is mapped to the LED strip. For example:

| DMX Channel | Function |
|------------|----------|
| 1 | Red |
| 2 | Green |
| 3 | Blue |

This mapping can be extended for multiple LEDs or segments.

## LED Output

After receiving a valid DMX frame:

1. Channel values are converted to RGB data.
2. The LED strip is updated accordingly.
3. Updates happen continuously to reflect live DMX input.

## Example

10 Channel mode (We will use this as our primary mode)
This mode controls the individual color of each square with a master dimmer control.

|   Channel   |   Value   |   Function   |
|:------------|:---------:|:-------------|
|       1     | 0...255   | 000...010   White
|             |           | 011...021 Red
|             |           | 022...032 Orange
|             |           | 033...043 Yellow
|             |           | 044...054 Green
|             |           | 055...065 Blue
|             |           | 066...076 Cyan
|             |           | 077...087 Purple
|       2     | 0...255   | RGB Value Square 2
|       3     | 0...255   | RGB Value Square 3
|       4     | 0...255   | RGB Value Square 4
|       5     | 0...255   | RGB Value Square 5
|       6     | 0...255   | RGB Value Square 6
|       7     | 0...255   | RGB Value Square 7
|       8     | 0...255   | RGB Value Square 8
|       9     | 0...255   | RGB Value Square 9
|       10    | 0...255   | Master dimmer 0 - 100%


## Summary

The system acts as a DMX-to-LED converter by:

- Receiving DMX via UART + RS485
- Storing channel data in a buffer
- Mapping channels to RGB values
- Continuously updating the LED strip output