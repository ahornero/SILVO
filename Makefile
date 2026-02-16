default: src/silvo.cpp
	c++ -o bin/silvo -O3 -Wall -fopenmp src/silvo.cpp

pedantic: src/silvo.cpp
	g++ -o bin/silvo -O3 -Wall -fopenmp -Wextra -Wpedantic -Wshadow -Wconversion -Wuninitialized -Wmaybe-uninitialized -Wfloat-equal -Wstrict-aliasing -Werror src/silvo.cpp