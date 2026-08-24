default: src/silvo.cpp
	c++ -o bin/silvo -O3 -Wall -fopenmp src/silvo.cpp

test: tests/test_scene_geometry.cpp
	c++ -std=c++17 -O2 -Wall -fopenmp -I. tests/test_scene_geometry.cpp -o bin/test_scene_geometry
	./bin/test_scene_geometry

clean:
	rm -f bin/test_scene_geometry output_image.ppm output_image.png output.bip output.hdr vertical_profile.csv gap_fraction_profile.csv

pedantic: src/silvo.cpp
	g++ -o bin/silvo -O3 -Wall -fopenmp -Wextra -Wpedantic -Wshadow -Wconversion -Wuninitialized -Wmaybe-uninitialized -Wfloat-equal -Wstrict-aliasing -Werror src/silvo.cpp