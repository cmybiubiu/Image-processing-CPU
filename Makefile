CC=gcc
GCC_OPT = -O2 -Wall -Werror
PGM_NAME = pgmWidthSize
PGM_TYPE = .txt

TOTAL_SIZE = 1048576

all: pgm_creator create_pgms main

%.o: %.c
	$(CC) -c -o $@ $< $(GCC_OPT)

main: very_big_sample.o very_tall_sample.o main.c pgm.c filters.c
	$(CC) $(GCC_OPT) main.c pgm.c filters.c very_big_sample.o very_tall_sample.o -o main.out -lpthread

pgm_creator:
	$(CC) $(GCC_OPT) pgm_creator.c pgm.c -o pgm_creator.out

create_pgms: my_pgm1 my_pgm2 my_pgm3 my_pgm4 my_pgm5 my_pgm6 my_pgm7 my_pgm8

my_pgm1:
	./pgm_creator.out 1 $(TOTAL_SIZE) pgmWidthSize1.txt

my_pgm2:
	./pgm_creator.out 8 131072 pgmWidthSize8.txt

my_pgm3:
	./pgm_creator.out 16 65536 pgmWidthSize16.txt

my_pgm4:
	./pgm_creator.out 64 16384 pgmWidthSize64.txt

my_pgm5:
	./pgm_creator.out 512 2048 pgmWidthSize512.txt

my_pgm6:
	./pgm_creator.out 1024 1024 pgmWidthSize1024.txt

my_pgm7:
	./pgm_creator.out 4096 256 pgmWidthSize4096.txt

my_pgm8:
	./pgm_creator.out 32768 32 pgmWidthSize32768.txt

clean:
	rm *.o *.out

clean_txt:
	rm pgmWidthSize*.txt

run: all
	python3 perfs_student.py
