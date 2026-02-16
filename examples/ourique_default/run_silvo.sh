# !/bin/bash
# This sample script assumes that the silvo executable is located in the ../../bin/ directory relative to the current directory, 
# and that it generates an output image named output_image.ppm. The script then uses the convert command (from ImageMagick) 
# to convert the PPM image to PNG format, and finally removes the original PPM file.

# expected outputs:
# - output_image.ppm (converted to output_image.png in this script)
# - output.bip (and its companion .hdr file)

../../bin/silvo
convert output_image.ppm output_image.png
rm output_image.ppm


