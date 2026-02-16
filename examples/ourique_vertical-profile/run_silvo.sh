# !/bin/bash
# This sample script assumes that the silvo executable is located in the ../../bin/ directory relative to the current directory.
# It runs the same settings as the ourique_default example, but it generates a vertical profile. The only difference is the 
# camera_zenith parameter set to 90 degrees but overwriting the config file from the command line parameter, which enables 
# the vertical profile generation.

# expected outputs:
# - output_image.ppm (converted to png with ImageMagick's convert tool)
# - output.bip (and its companion .hdr file)
# - vertical_profile.csv

../../bin/silvo --scene ../ourique_default/scene.txt --config ../ourique_default/settings.txt --cza 90
convert output_image.ppm output_image.png
rm output_image.ppm


