# Changelog

## [1.2.0] - 2026-09-02
- Added orthographic camera projection (`camera_projection`), independent from the existing perspective mode.
- Orthographic frame auto-fits and centres on the real vegetation bounding box (instead of assuming it is centred on the world origin), with configurable `camera_size`, `camera_margin`, and a `camera_fill` (contain vs. cover) mode.
- Fixed the sun to a physically constant Earth-Sun distance, independent from `camera_distance` (previously coupled, causing incorrect illumination parallax on close-range renders).
- Added the `orthographic` example, reusing the `default` scene.
- `run_silvo.sh` scripts now work from any invocation directory and consistently convert `output_image.ppm` to `.png`, including the gap-fraction example.
- `gap-fraction` example no longer duplicates `scene.txt` (reuses `default`'s).
- Renamed example folders, dropping the `ourique_` prefix (e.g. `ourique_default` → `default`).
- Makefile: added `clean-examples` target, `clean` now also removes `bin/silvo`, and declared `.PHONY` targets.

## [1.1.0] - 2026-08-24
- Added vertical spheroidal crowns via `x y z horizontalRadius verticalRadius`.
- Kept legacy `x y z radius` entries fully compatible.
- Added scene parsing and geometry regression checks.
- Added `make clean` for generated output cleanup.
- More examples added.

## [1.0.0] - 2026-03-04
- Initial release.
- Spherical crown illumination model.
- Gap fraction, light partitioning, and canopy density metrics.
- PPM image and ENVI multilayer outputs.
- Settings file and CLI configuration.
- Example workflows.


