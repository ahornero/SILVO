# SILVO — a Simplified Light–Vegetation Overlay model
![CI](https://github.com/ahornero/SILVO/actions/workflows/cpp.yml/badge.svg)
[![DOI](https://img.shields.io/badge/DOI-10.1016/j.jag.2026.105178-lightgrey)](https://doi.org/10.1016/j.jag.2026.105178)
[![License](https://img.shields.io/badge/License-AGPLv3-blue.svg)](LICENSE)
![OpenMP](https://img.shields.io/badge/OpenMP-enabled-success.svg)
<!-- 
![Release](https://img.shields.io/github/v/release/ahornero/SILVO)
-->

![SILVO model visualization](./assets/output_images.png)

**SILVO** is a lightweight 3D ray-tracing model to characterise the **structure and illumination of heterogeneous vegetation canopies**
(e.g., orchards, open woodlands, agroforestry systems) from **minimal inputs**: tree crown position/size plus sun/view geometry.

It is designed to be fast and reproducible, and it is parallelised with **OpenMP**.

## Table of contents

- [What SILVO produces](#what-silvo-produces)
- [Quick start (Linux)](#quick-start-linux)
- [Usage](#usage)
- [Help](#help)
- [Inputs](#inputs)
- [Outputs](#outputs)
- [How to cite](#how-to-cite)

## What SILVO produces

- **Gap fraction** and sunlit/shaded fractions (soil & vegetation)
- **Vertical structural proxies** (density / profiles) that can constrain 1D RTMs (e.g., SAIL/SCOPE-type workflows)
- A quick-look render (**PPM**) and analysis-ready rasters (**ENVI BIP + HDR**)

## Quick start (Linux)

### 1) Install build tools (Debian-based OS)
```bash
sudo apt update
sudo apt install -y build-essential
```
### 2) Clone the repository
```bash
git clone https://github.com/ahornero/silvo.git
cd silvo
```
### 3) Build
From the repository root:
```bash
make
```
The executable is created at (already precompiled in the repo for amd64)
```bash
./bin/silvo
```
### 4) Run an example
```bash
cd examples/ourique_default
./run_silvo.sh
```
Outputs are written in the folder where you run SILVO (e.g., inside the example directory) and from the previous example should generate a BIL (ENVI) file (with its header, hdr companion file) containing all the output as layers and a PNG image as preview.

## Usage
![SILVO workflow](./assets/workflow.png)
By default, SILVO reads the following files from the current working directory:
- scene.txt
- settings.txt

But they can be chosen from the command line as arguments:
```bash
./bin/silvo --scene path/to/scene.txt --config path/to/settings.txt
```
Same applies for the solar and camera settings. Command-line values take precedence over settings.txt:
```bash
./bin/silvo --scene scene.txt --config settings.txt \
  --sza 35 --saa 120 --cza 0 --caa 0 --cds 600 --fov 30
```
## Help
```bash
./bin/silvo -h
Usage: ./bin/silvo
        [-s|--scene scene_file (scene.txt)]
        [-c|--config config_file (settings.txt)]
        [--sza value] [--saa value] [--cza value] [--caa value] [--cds value] [--fov value]
        [-h] (show this)
```
## Inputs
### _scene.txt_ (vegetation crowns)
Plain text. Each non-comment (#...) line contains:
```text
x  y  z  radius
```
- Lines starting with # are ignored.
- z is typically 0 for flat terrain (SILVO places spheres sitting on the ground).
Example:
```text
# x   y   z   radius
0.0  0.0  0.0  2.5
8.0  0.0  0.0  2.0
0.0  8.0  0.0  3.0
```
### _settings.txt_ (camera + sun + flags)
Plain text key/value pairs, e.g.:
```text
solar_zenith = 35
solar_azimuth = 120

camera_zenith = 0
camera_azimuth = 0
camera_distance = 600
camera_fov = 30

debug = 0
gap_fraction_profile_enabled = 0
```
## Outputs
![SILVO outputs](./assets/output_profiles.png)
SILVO writes outputs to the current working directory:

- _output_image.ppm_ — RGB quick-look render
- _output.bip_ + _output.hdr_ — ENVI BIP (15 bands, float32) + header
- Console metrics (stdout): gap fraction, sunlit/shaded fractions, normalised density
- Optional CSV profiles:
    - _vertical_profile.csv_ (generated when camera_zenith == 90)
    - _gap_fraction_profile.csv_ (generated when gap_fraction_profile_enabled = 1 and camera_zenith != 90)

## How to cite

If you use SILVO in academic work, please cite:

```text
Hornero, A., Prikaziuk, E., Hernandez-Clemente, R., van der Tol, C. (2026).
SILVO, a lightweight 3D illumination model to characterise the spatial structure of heterogeneous vegetation canopies.
International Journal of Applied Earth Observation and Geoinformation, 147, 105178. https://doi.org/10.1016/j.jag.2026.105178
```
```bibtex
@article{Hornero2026SILVO,
  title   = {SILVO, a lightweight 3D illumination model to characterise the spatial structure of heterogeneous vegetation canopies},
  author  = {Hornero, A. and Prikaziuk, E. and Hernandez-Clemente, R. and van der Tol, C.},
  journal = {International Journal of Applied Earth Observation and Geoinformation},
  volume  = {147},
  pages   = {105178},
  year    = {2026},
  issn    = {1569-8432},
  doi     = {10.1016/j.jag.2026.105178},
  url     = {https://doi.org/10.1016/j.jag.2026.105178}
}
```
