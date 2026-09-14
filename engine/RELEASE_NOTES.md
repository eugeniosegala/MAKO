## What's new in MAKO Renderer v3.3.0

### Release codename: neptune-fury

> **Draft — unreleased.** Release validation, final copy, and banner artwork are pending.

<!-- Before publication: add the shared assets/neptune-fury.png banner to both component notes, record its provenance in ASSET_PROVENANCE.md, and remove the draft notice. -->

### Scaling with Gamescope WSI off

- **Preserve the game's render resolution:** On supported Gamescope X11 windows, MAKO keeps the game's original resolution as the scaling source and uses the confirmed output target when the full Gamescope WSI layer is disabled. This supports both 64-bit and 32-bit game processes.
- **Keep compatibility optional:** Scaling can run alone or with Frame Generation through the combined Renderer. The full Gamescope WSI path remains an optional compatibility choice for supported 64-bit launches.

### More resilient Lossless Scaling model loading

- **Recognize supported model layouts after resource IDs move:** LS1 and LSFG loading can locate a complete, compatible model table when its internal resource IDs have shifted together. Incomplete, ambiguous, or incompatible layouts remain rejected; this does not guarantee support for every future Lossless Scaling update.
- **Consistent inspection and loading:** Model inspection and rendering use the same resource checks, so availability reports reflect the models the Renderer can actually load. Restart the game after updating Lossless Scaling.

### Consistent precision defaults

- **FP16 when supported:** Missing `allow_fp16` settings now use the same enabled default as a newly generated configuration. Existing explicit choices are preserved, and GPUs without FP16 support use FP32.
- **Explicit CLI precision:** Benchmark, debug, and LSFG quality commands allow FP16 by default. Use `--no-fp16` to select FP32 or `--allow-fp16` to allow FP16 explicitly.

### Setup and diagnostics

- **Clearer standalone and Flatpak instructions:** Updated guides explain the required Steam launch option, profile matching, and separate Flatpak preparation.
- **Better scaling diagnostics:** Reports include the Gamescope scaling-surface association, helping distinguish active scaling from a native-resolution fallback when WSI is off.
