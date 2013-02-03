# Pnum Makefile

CC=gcc
CFLAGS=-Wall -Werror -O3 -march=corei7-avx -mfpmath=sse -mavx -static

all: pnum

pnum: pnum.c
	$(CC) $(CFLAGS) pnum.c -o pnum -pthread -lm -lgmp

clean:
	rm -f pnum
