# desktop-robot

Firmware GPIO table for the desk companion. This is **not** a new PCB. Hardware is already wired per the docs repo `pin-config.md`. Do not edit `bread-compact-wifi-lcd` in place.

| Item | Value |
|---|---|
| Chip | ESP32-S3 N16R8 |
| LCD | ST7789, RST **GPIO39**. Q1: 320×240, `SWAP_XY true`, `MIRROR_X true`, `MIRROR_Y false`, `INVERT_COLOR true` |
| Audio | Simplex I2S (mic 4/5/6, speaker 7/15/16). Amp `SD` → that board's `Vin` |
| Motors | TB6612 on GPIO 8–14. Default duty **50%** (`duty_percent` 25–80). Cap **8000 ms**. STBY high only while moving. Invert: `MOTOR_LEFT_INVERT 1`, `MOTOR_CHASSIS_INVERT 1` |
| Battery ADC | GPIO1, 100 kΩ / 100 kΩ. `BATTERY_MONITOR_ENABLE` / `BATTERY_LOW_MOTOR_LOCK` stay **0** until the divider is soldered |
| Desk power | DevKit **USB plugged** (logic / LCD) + **Type-C rewired as 5 V** into `VM`. Pack-only flickers the LCD |

21 emotion PNGs in `main/assets/faces/` are embedded and shown via `SetEmotion`. Chat text and the status bar stay visible. The ST7789 invert is on, so the PNGs on disk look inverted (light gray + red eyes); the LCD should show dark gray + teal. Check `_preview_on_glass.png` for the intended look.

MCP tools: `self.robot.move`, `stop`, `quiet`, `standby`. Emotion→motion fallback is **off**. Do not firmware-swap L/R wheels — use the role-prompt map.
