# Validation and current limits

[Overview](../README.md) · [Run the checks](DEVELOPMENT.md) · [CAD audit](../mechanical/audit/README.md)

Vibecom v0.6.1 is a **digital prototype with functional firmware and simulation**. The table records the scope of evidence; it is not a manufacturing certificate or a probability of physical assembly success.

| Area | Evidence | Status / boundary |
| --- | --- | --- |
| Firmware compatibility | CI builds all five PlatformIO environments | Automated; dependencies pinned |
| Behavior | Shared native tests for filtering, gestures, timing, settings and servo bounds | Automated; runs the actual behavior layer |
| Rendering | Pixel, palette, band, transition and buffer-boundary checks | Automated; host rendering, not physical FPS |
| Driver and custom chip | Actual Pololu 1.3.1 driver connected to C chip callbacks | Automated; functional I²C subset |
| Wokwi VS Code | User confirmed new VL53L1X diagram loads and runs | User-reported runtime confirmation |
| CAD integrity | Six closed, connected positive-volume STL meshes; GLB units; source hashes | Automated artifact checks |
| Assembly geometry | 112 envelope pairs, connector reservations and sampled insertion paths | Digital checks pass within the declared assumptions |
| Head motion | 26 sampled yaw/vertical-offset scenarios | Shell/body clearance passes; no physical servo or wire test |
| Position sensitivity | Axis shifts of selected device envelopes by ±0.2 / ±0.4 mm | Declared scenarios; ToF front-stop contact is expected |
| Enclosure screws | Candidate closure heads, shafts and sampled base-head entry paths | Tested candidate dimensions only; not every M2 screw |
| Physical electronics | Actual LCD startup/FPS, optical ranging and touch through plastic | Pending |
| Printed assembly | Fit, horn/spline, wiring, screw strength and wear | Pending |

## Reproducibility

The [CAD checker](../mechanical/check_exports.py) verifies source and artifact fingerprints, including the current audit scripts and BLEND. The [custom-chip manifest](../sim/vl53l1x.build.json) links C/JSON sources to the shipped WASM. Reports must be regenerated when the associated geometry or scripts change.

- [Assembly report](../mechanical/audit/assembly-audit.json)
- [Robustness report](../mechanical/audit/robustness.json)
- [Readable assembly findings](../mechanical/audit/README.md)
- [Readable motion, tolerance and screw checks](../mechanical/audit/ROBUSTNESS.md)
- [GitHub Actions](https://github.com/syntaxguild/vibecom/actions/workflows/checks.yml)

CI checks the shipped CAD evidence; it does not rerun Blender or recreate a physical experiment. Wokwi's ILI9341 screen is a proxy for visual behavior, and its custom distance model does not simulate reflectance, cover glass or optical crosstalk.

## Next physical milestone

1. Measure the exact LCD stack, connectors, servo horn/spline and selected screw heads.
2. Print fit samples for the display, joint and board mounts before a finished enclosure.
3. Verify wiring slack and low-speed travel with the actual head load.
4. Record distance readings under different targets/light, touch sensitivity, display timing and saved-setting behavior on the actual ESP32.

Passing more digital test cases does not establish an “80%” or “98%” likelihood of physical success. The remaining uncertainties are named explicitly so the next experiment can resolve them.
