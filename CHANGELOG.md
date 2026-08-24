# Changelog

## [1.0.0] - 2026-03-04
- Initial release.
- Spherical crown model for canopy scene parsing.
- Gap fraction, light partitioning, and canopy density metrics.
- PPM and ENVI BIP/HDR outputs.
- Settings file and CLI configuration.
- Example workflows for default, gap-fraction, and vertical-profile cases.

## [1.1.0] - 2026-08-24
- Added vertical spheroidal crowns via `x y z horizontalRadius verticalRadius`.
- Kept legacy `x y z radius` entries fully compatible.
- Split example folders into `*-spheres` and `*-spheroids`.
- Added scene parsing and geometry regression checks.
- Added `make clean` for generated output cleanup.
