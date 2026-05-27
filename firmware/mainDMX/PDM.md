# PDM Microphone

## Overview

The PDM microphone on the PSoC 6 is a digital microphone interface that uses:
- 1 clock line
- 1 data line

The PSoC contains a hardware PDM/PCM converter that converts the incoming PDM bitstream into PCM audio samples.

---

# Hardware Pins

| Signal | Pin |
|--------|-----|
| PDM Clock | P10_4 |
| PDM Data  | P10_5 |

---

# Main Components

- PDM microphone
- PDM/PCM hardware block
- Audio clock
- DMA/interrupt system
- Audio processing task

---

# Audio Settings

| Setting | Value |
|---------|------|
| Sample Rate | 16000 Hz |
| Decimation Rate | 64 |
| Word Length | 16-bit |
| Mode | Stereo |

---

# Audio Flow

```
Microphone
    ↓
PDM Bitstream
    ↓
PDM/PCM Converter
    ↓
PCM Audio Samples
    ↓
Audio Processing
    ↓
Beat Detection / DSP
```