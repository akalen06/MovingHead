# Functions in 9-channel DMX mode (16-bit)

|   Channel   |   Value   |   Function   |
|:------------|:---------:|:-------------|
|      1      | 0...255   | Rotation (pan) (0° up to the maximum value of the Pan range of 540°)|
|       2     | 0...255   | Inclination (tilt) (0° up to the maximum value of the Tilt range of 200°)|
|       3     | 0...255   | Fine adjustment rotation (Pan) |
|       4     | 0...255   | Fine adjustment inclination (tilt) |
|       5     | 0...255   | Motor speed (fast to slow)
|       6     | 0...255   | 000...010   White
|             |           | 011...021 Red
|             |           | 022...032 Orange
|             |           | 033...043 Yellow
|             |           | 044...054 Green
|             |           | 055...065 Blue
|             |           | 066...076 Cyan
|             |           | 077...087 Purple
|             |           | 088...175 Colour selection 6+
|             |           | 176...255 Colour wheel rotation
|      7      | 0...255   | 000...124 Gobo selection
|             |           | 125...249 Gobo shake selection
|             |           | 250...255 Gobo wheel selection
|        8    | 0...255   | Master dimmer 0 - 100%
|        9    | 0...255   | 000...009 No function (open)
|             |           | 010...255 Strobe effect (1 Hz...25 Hz)