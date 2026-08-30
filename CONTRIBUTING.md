# Contributing to MAKO

Thank you for contributing to MAKO. Contributions should preserve MAKO's open-source licensing, upstream lineage, protected-input boundary, and evidence-driven engineering workflow.

## Contribution license

Unless a contribution explicitly identifies compatible third-party material and its license, submitting a contribution to MAKO means that you license it under GPL-3.0-or-later, the same outbound license used by the project. You represent that you have the right to submit the contribution under those terms and that it does not knowingly include material you are not permitted to distribute.

Existing third-party notices remain applicable to inherited code. Do not remove or replace copyright, license, authorship, or attribution records. Identify newly incorporated third-party material in `THIRD_PARTY_NOTICES.md` and include its required license text in the relevant package.

## Protected and proprietary inputs

Never commit or attach `Lossless.dll`, extracted Lossless Scaling models or shaders, licensed application files, credentials, game assets, ROMs, crash dumps containing private data, or other protected binary payloads. MAKO's tests use synthetic unlicensed fixtures. Run `./scripts/test-protected-inputs.sh` before submitting changes that affect packaging, model inspection, shaders, or test data.

## Visual and generated material

For artwork, icons, screenshots, audio, generated media, and other non-code contributions, record the creator, source, creation method, modifications, and license in `ASSET_PROVENANCE.md`. Disclose AI-assisted creation and any input assets. Do not submit third-party marks or source material without permission or a documented legal basis.

Generated files must be updated through their owning generator. Do not edit generated configuration bindings, localization bundles, embedded SPIR-V, or build outputs independently.

## Validation

Follow [AGENTS.md](AGENTS.md) and [TESTING.md](TESTING.md) for architecture, formatting, test selection, package verification, and real-hardware evidence. A contribution should state what passed and what was not tested without presenting skipped hardware coverage as evidence.
