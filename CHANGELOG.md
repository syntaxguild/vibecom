# Changelog

Project versions use `MAJOR.MINOR.PATCH`. `VERSION` records the current release family; `mechanical/reference.json` adds a CAD revision descriptor. The current release is marked **prerelease** while physical assembly remains unverified.

## 0.6.1 — 2026-10-16

### Hardware and validation

- Opened the complete entry path for candidate base screw heads.
- Added lower-header and servo-case clearance after position-sensitivity checks.
- Checked sampled head rotation from −60° to +60°, including a 0.4 mm downward offset.
- Added fingerprinted robustness evidence alongside the assembly audit.

### Project presentation

- Added an English portfolio overview, Thai guide, real-model cover and organized documentation.
- Added architecture, validation matrix, versioned downloads and release checksums.
- Removed outdated AI concept assets from the active tree while retaining Git history links.

## Earlier milestones

These milestones are recorded from Git history; they were not published GitHub releases.

### 0.6

Corrected the full ESP32 outline, moved enclosure mounts, reserved connector space, revised the servo support and validated sampled insertion paths.

### 0.5

Introduced the cream Mochi enclosure, wider face, revised feet and current editable Blender workflow.

### 0.4

Moved to Pololu VL53L1X, added the compact optical aperture and integrated a custom driver-compatible I²C model into Wokwi.

### 0.1–0.3

Established the reference enclosure, enlarged the display and concealed the rotating joint. Firmware development added gestures, personality settings and persistent calibration.

[Design snapshots and earlier artwork](docs/HISTORY.md)
