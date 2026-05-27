# DMX Mapping for the DMX led wall
## 2 Channel mode
This mode controls the color and dimmer of the entire LED wall.

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
|       2     | 0...255   | Master dimmer 0 - 100%

## 10 Channel mode (We will use this as our primary mode)
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

## 19 Channel mode
This mode controls the individual color and dimmer of each square. It also provides a master dimmer.

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
|       2     | 0...255   | Dimmer value square 1
|       3     | 0...255   | RGB Value Square 2
|       4     | 0...255   | Dimmer square 2
|       5     | 0...255   | RGB Value Square 3
|       6     | 0...255   | Dimmer square 3
|       7     | 0...255   | RGB Value Square 4
|       8     | 0...255   | Dimmer square 4
|       9     | 0...255   | RGB Value Square 5
|       10     | 0...255   | Dimmer square 5
|       11    | 0...255   | RGB Value Square 6
|       12    | 0...255   | Dimmer square 6
|       13    | 0...255   | RGB Value Square 7
|       14    | 0...255   | Dimmer square 7
|       15    | 0...255   | RGB Value Square 8
|       16    | 0...255   | Dimmer square 8
|       17    | 0...255   | RGB Value Square 9
|       18    | 0...255   | Dimmer square 9
|       19    | 0...255   | Master dimmer 0 - 100%
