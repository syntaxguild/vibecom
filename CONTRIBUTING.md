# Contributing

Start with the [architecture](docs/ARCHITECTURE.md) and [development guide](docs/DEVELOPMENT.md). Keep hardware-specific I/O separate from the shared behavior and renderer so native checks continue to exercise production logic.

Before submitting a change:

1. Run the checks relevant to the changed layer. CI builds all five firmware profiles and checks host behavior, rendering and shipped CAD evidence.
2. If changing CAD or custom-chip sources, regenerate their exports and reports together. Do not hand-edit a report to make it pass.
3. Update affected documentation and the changelog. State whether a result came from native tests, Wokwi, geometry inspection or actual hardware.

Use a focused pull request explaining the problem, resulting behavior and validation. For hardware issues, include the exact board/module model, build environment and relevant wiring. Keep credentials and local tool caches out of commits.
