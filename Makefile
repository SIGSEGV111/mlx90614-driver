.PHONY: clean all

all: mlx90614-csv

mlx90614-csv: mlx90614-csv.cpp mlx90614.cpp mlx90614.hpp Makefile
	g++ -Wall -Wextra -flto -O3 -march=native -fdata-sections -ffunction-sections -Wl,--gc-sections mlx90614-csv.cpp mlx90614.cpp -o mlx90614-csv

clean:
	rm -vf mlx90614-csv
