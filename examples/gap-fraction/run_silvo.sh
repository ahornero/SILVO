#!/bin/bash
# Spherical-crown example with gap_fraction_profile_enabled set to 1.
# This process takes longer than the default example.
# The script can be invoked from any directory.

# expected outputs:
# - output_image.png
# - output.bip (and its companion .hdr file)
# - gap_fraction_profile.csv

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

../../bin/silvo --scene ../default/scene.txt --config settings.txt
convert output_image.ppm output_image.png
rm -f output_image.ppm
