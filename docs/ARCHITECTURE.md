# Architecture and design decisions

[Project overview](../README.md) · [Firmware details](FIRMWARE.md) · [Validation](VALIDATION.md)

## Design brief

Make a small desktop companion readable through its eyes and timing. Distance should suggest awareness; touch should take priority; idle time should feel quieter. The design avoids speech, a mouth and pupil dots, using shape, color, blinking and restrained head motion instead.

## System boundaries

| Layer | Responsibility | Implementation |
| --- | --- | --- |
| Hardware profiles | Display geometry, servo limits and settings namespace | [hardware.h](../src/hardware.h) |
| Input and device service | I²C readings, touch GPIO, bounded bus work and SPI transfers | [main.cpp](../src/main.cpp) |
| Distance conversion | Reject invalid status, bus errors and out-of-range readings | [distance_reading.h](../src/distance_reading.h) |
| Companion behavior | Filtered distance, emotional state, gestures, timers and settings | [behavior.h](../src/behavior.h) |
| Face rendering | Shared indexed canvas, poses, palette and animation | [face.h](../src/face.h) |
| Simulation | Driver-compatible VL53L1X continuous-ranging subset | [custom chip](../sim/README.md) |
| Mechanical design | Parameterized enclosure, exports and assembly evidence | [mechanical](../mechanical/README.md) |

## Why these choices

**Keep the behavior independent of device drivers.** The same behavior and renderer can run on ESP32 or in native tests. Tests exercise the real state transitions and drawing code rather than a separate recreation of them.

**Validate distance before assigning emotion.** Invalid optical measurements must not become a zero-centimeter reading. The ToF path accepts valid readings, converts millimeters to centimeters, then uses the behavior's filter. Stale input expires rather than leaving the bot permanently happy or angry.

**Separate drawing from display transfer.** Two indexed canvases let a complete image remain stable while the next is prepared. Palette-preserving RGB565 conversion happens in small bands. This reduces transfer buffer size and avoids clearing the entire screen before redrawing it.

**Bound motion and preserve calibration.** Reference servo profiles cap their command range. Settings are validated before use and saved explicitly to NVS. The two-inch ToF and sonar profiles share the compatible settings namespace so a sensor change does not discard calibration.

**Use the real driver in the simulator.** The VL53L1X chip model implements the register interactions used by Pololu 1.3.1. A host adapter connects its C callbacks to the actual library and companion behavior. It is a focused functional model, not an optical simulator.

**Treat the enclosure as an engineering artifact.** A render alone cannot prove assembly. The project keeps the generator, reference dimensions, eight printable pieces, editable assembly and reports together. Mesh checks, sampled insertion paths, optical rays and clearance scenarios describe different parts of the evidence.

## Constraints that shaped the result

- The real two-inch LCD active area is 40.8 × 30.6 mm; the renderer does not pretend the entire black face is an active screen.
- The 320 × 214 face region needs about 27.39 ms of wire time for a full update at 40 MHz, before overhead. A 60 Hz logical drawing target is not a promise of 60 physical FPS.
- Moving from HC-SR04 to a compact VL53L1X allowed a smaller optical opening, while keeping the selected controller, display and positional servo.
- The head/body seam is 0.8 mm. Cable slack, horn stack height and real printed tolerances still require a physical fit test.

## What remains

Measure the purchased hardware, print fit samples, confirm connector and horn dimensions, check wire routing during motion, and record physical sensor/display/servo results. The [validation matrix](VALIDATION.md) distinguishes digital evidence from those remaining tasks.
