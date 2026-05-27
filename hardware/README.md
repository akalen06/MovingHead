# Hardware

> This folder contains all hardware-related files for the project

---

## 🧩 List of components

| Component | Type/Model | Function | Datasheet | Amount |
|-----------|-----------|---------|-----------|--------|
| Microcontroller | PSoC 6 | Main controller | [Link](/docs/datasheets/infineon-cy8ckit-062-wifi-bt-psoc-6-wifi-bt-pioneer-kit-guide-usermanual-en.pdf) | 1 |
| Microcontroller | Raspberry Pi Pico | DMX Slave | [Link](/docs/datasheets/RP-008307-DS-1-pico-datasheet.pdf) | 1 |
| Module | Pico-2CH-RS485 | UART To RS485 | [Link](/docs/datasheets/DOC035253319.pdf) | 2 |
| Light | Pico Spot 20 LED | Movinghead | [Link](/docs/datasheets/c_372642_v2_r1_en_online.pdf) | 1 |
| LED | WS2812B | LED Strip | [Link] | ~10m |



---

## 🔌 Blokdiagram / Schematic

**Schematic overview:**
![](../docs/images/Software%20Diagram%20Music.jpg)

---

## ⚡ Specifications (LED Wall)

### Power
- **Voltage:** [5V DC]
- **Current:** [19A]
- **Power supply:** [External power supply]

### Communication
- **Protocollen:** [UART, SPI]
- **Pins:** [lijst belangrijke pinnen]
- **Logica level:** [3.3V]

## ⚡ Specifications (DMX Controller)

### Power
- **Voltage:** [5V DC via USB]
- **Current:** [max 500mA]
- **Power supply:** [PC USB port]

### Communication
- **Protocollen:** [UART]
- **Pins:** [P5.0 (Default TX), P9.0(Free GPIO TX), P10.5(PDM data), P10.4(PDM CLK)]
- **Logica level:** [3.3V]

---
