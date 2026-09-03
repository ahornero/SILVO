#!/bin/bash
# Vertical-spheroid variant of the vertical-profile example.
# The script can be invoked from any directory.

# expected outputs:
# - output_image.png
# - output.bip (and its .hdr file)
# - vertical_profile.csv

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

../../bin/silvo --scene ../default-spheroids/scene.txt --config ../default/settings.txt --cza 90
convert output_image.ppm output_image.png
rm -f output_image.ppm
