#!/bin/bash
# Standard example with spherical crowns.
# The script can be invoked from any directory.

# expected outputs:
# - output_image.png
# - output.bip (and its companion .hdr file)

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

../../bin/silvo
convert output_image.ppm output_image.png
rm -f output_image.ppm
