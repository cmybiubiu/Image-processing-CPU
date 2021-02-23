CC=gcc
GCC_OPT = -O2 -Wall -Werror
PGM_NAME = pgmWidthSize
PGM_TYPE = .txt
WIDTHS = 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768

all: pgm_creator my_pgm main

%.o: %.c
	$(CC) -c -o $@ $< $(GCC_OPT)

main: very_big_sample.o very_tall_sample.o main.c pgm.c filters.c
	$(CC) $(GCC_OPT) main.c pgm.c filters.c very_big_sample.o very_tall_sample.o -o main.out -lpthread
	

pgm_creator:
	$(CC) $(GCC_OPT) pgm_creator.c pgm.c -o pgm_creator.out

my_pgm:
	$(foreach var,$(WIDTHS),./pgm_creator.out $(var) $(var) $(PGM_NAME)$(var)$(PGM_TYPE);)

clean:
	rm *.o *.out

run: all
	python3 perfs_student.py
