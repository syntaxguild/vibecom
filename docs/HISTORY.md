# Design history

[Current project](../README.md) · [Changelog](../CHANGELOG.md)

| Milestone | Design change | Git snapshot |
| --- | --- | --- |
| v0.1 | Initial enclosure and selected reference hardware | [84d022a](https://github.com/syntaxguild/vibecom/tree/84d022a) |
| v0.2 | Larger two-inch LCD and softer proportions | [eaf60a8](https://github.com/syntaxguild/vibecom/tree/eaf60a8) |
| v0.3 | Concealed joint; head sits close to body | [a1484a8](https://github.com/syntaxguild/vibecom/tree/a1484a8) |
| v0.4 | Compact VL53L1X replaces the twin ultrasonic transducers | [d05f112](https://github.com/syntaxguild/vibecom/tree/d05f112) |
| v0.5 | Cream Mochi shell, wider face and revised feet | [a9654f5](https://github.com/syntaxguild/vibecom/tree/a9654f5) |
| v0.6 | Corrected hardware envelopes, board mounts and assembly paths | [bba391c](https://github.com/syntaxguild/vibecom/tree/bba391c) |
| v0.6.1 | Additional motion, offset and screw-entry checks | [14bf42d](https://github.com/syntaxguild/vibecom/tree/14bf42d) |
| v0.7.0 | Separate rear service cover and electronics tray; straight LCD access | [Current mechanical guide](../mechanical/README.md) |

## Earlier concept images

The old `output/imagegen/` folder contained two AI-generated 4K presentation images of **v0.3 with HC-SR04**. They were removed from the active tree because they do not represent the current hardware or printable geometry. The original images, prompts and provenance remain recoverable in Git:

- [Concept-image archive and generation notes](https://github.com/syntaxguild/vibecom/tree/b8518c2/output/imagegen)
- [Earlier blueprint](https://github.com/syntaxguild/vibecom/blob/b8518c2/output/imagegen/vibecom-blueprint-16x9-v2.png)
- [Earlier product presentation](https://github.com/syntaxguild/vibecom/blob/b8518c2/output/imagegen/vibecom-presentation-16x9-v1.png)

Those images illustrate a historical concept and are not fabrication drawings. The current cover and studio renders come from the editable Blender assembly; animation previews come from the firmware renderer. The new [v0.7.0 blueprint](assets/vibecom-blueprint.png) uses current CAD renders as AI references, with its [prompt and fingerprints recorded](assets/vibecom-blueprint.provenance.json). It remains presentation artwork.
