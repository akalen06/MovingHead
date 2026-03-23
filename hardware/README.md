# Hardware

> This folder contains all hardware-related files for the project

---

## 📂 Structure

```
hardware/
├── README.md              # This file
└── schematics/           # Schematics
    └── [project].kicad_sch
```

---

## 🧩 List of components

> **Update this list with all hardware components**

| Component | Type/Model | Function | Datasheet | Amount |
|-----------|-----------|---------|-----------|--------|
| Microcontroller | PSoC 6 | Main controller | [Link](/docs/datasheets/infineon-cy8ckit-062-wifi-bt-psoc-6-wifi-bt-pioneer-kit-guide-usermanual-en.pdf) | 1 |
| Microcontroller | Raspberry Pi Pico | DMX Slave | [Link](/docs/datasheets/RP-008307-DS-1-pico-datasheet.pdf) | 1 |
| Module | Pico-2CH-RS485 | UART To RS485 | [Link](/docs/datasheets/Pico2CHRS485.md) | 1 |
| Light | Pico Spot 20 LED | Movinghead | [Link](/docs/datasheets/PicoSpot20LED.md) | 1 |
| LED | WS2812B | LED Strip | [Link] | / |
| ... | ... | ... | ... | ... |



---

## 🔌 Blokdiagram / Schematic

**Schematic overview:**
![](../docs/images/Software%20Diagram%20Music.jpg)

> **Voeg hier een afbeelding of diagram toe waarin de bedrading en verbindingen van de componenten worden weergegeven.**
>
> - Voor week 1-2: Een handmatige schets is OK
> - Later: Fritzing of KiCAD schematic export (PNG of PDF) 

---

## ⚡ Specifications

### Power
- **Voltage:** [bijv. 5V DC via USB]
- **Current:** [bijv. max 500mA]
- **Power supply:** [bijv. USB powerbank]

### Communication
- **Protocollen:** [bijv. UART, I²C, SPI]
- **Pins:** [lijst belangrijke pinnen]
- **Logica level:** [bijv. 3.3V]

---

## 📝 Aantekeningen

**Belangrijke opmerkingen:**
- ...
- ...

**Bekende problemen:**
- ...
- ...

---

## ✅ Checklist

- [ ] Componentenlijst volledig ingevuld
- [ ] Schema/blokdiagram toegevoegd
- [ ] Datasheets lokaal opgeslagen in `/docs/datasheets/`
- [ ] KiCAD project werkend
- [ ] Specificaties gedocumenteerd
