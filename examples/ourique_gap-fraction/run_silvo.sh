# !/bin/bash
# This sample script assumes that the silvo executable is located in the ../../bin/ directory relative to the current directory.
# It runs the same settings as the ourique_default example, but it generates the gap_fraction. The only difference in the settings
# file is the gap_fraction parameter set to 1, which enables this generation. This process takes longer than the default settings, 
# so be patient when running this example.

# expected outputs:
# - output_image.ppm
# - output.bip (and its companion .hdr file)
# - gap_fraction_profile.csv

../../bin/silvo --scene ../ourique_default/scene.txt
