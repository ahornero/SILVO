#!/bin/bash
# This example uses the default scene and settings, overriding
# camera_zenith to 90 degrees to generate a vertical profile.
# The script can be invoked from any directory.

# expected outputs:
# - output_image.png
# - output.bip (and its companion .hdr file)
# - vertical_profile.csv

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

../../bin/silvo --scene ../default/scene.txt --config ../default/settings.txt --cza 90
convert output_image.ppm output_image.png
rm -f output_image.ppm
