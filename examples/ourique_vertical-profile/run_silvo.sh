# !/bin/bash
# This sample script assumes that the silvo executable is located in the ../../bin/ directory relative to the current directory.
# It runs the same settings as the ourique_default example, but it generates a vertical profile. The only difference is the 
# camera_zenith parameter set to 90 degrees in the settings file, which enables the vertical profile generation.

# expected outputs:
# - output_image.ppm
# - output.bip (and its companion .hdr file)
# - vertical_profile.csv

../../bin/silvo
convert output_image.ppm output_image.png
rm output_image.ppm


