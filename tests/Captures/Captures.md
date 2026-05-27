# DMX Music Project README

## DMX Signal Timings

DMX512 signals follow a standard asynchronous serial protocol at **250 kbps** (250,000 bits per second).

### Key Timings

- **Bit time:**  
  4 μs per bit (1 / 250,000 seconds)

- **Break:**  
  Minimum **92 μs** (typically **176 μs**)  
  Marks the start of a packet

- **Mark After Break (MAB):**  
  Minimum **12 μs** (typically **16 μs**)

- **Start Code:**  
  8 bits  
  Usually `0x00` for standard DMX

- **Data frame:**  
  - Up to **512 channels**  
  - Each channel: 8 data bits + start/stop bits  
  - **11 bits total per channel**  
  - ≈ **44 μs per channel**

- **Packet refresh rate:**  
  - Typically **44 Hz** (full universe in ~23 ms)  
  - Can vary up to **1000 Hz** for smaller packets

These timings ensure reliable lighting control and synchronization with music.

---

## Capture Files

This project includes **three capture files** for analysis:

- **Two captures** from a *Chamsys MQ70* console (real-world production use) - [Capture 1 ](./chamsystest.sal) - [Capture 2](./chamsystestlocateµ.sal)
- **One capture** from custom test code (validation and comparison) [Capture](./Captures.md)

### Analysis Tools

You can inspect timings using a logic analyser or on oscilloscope

---