CC=gcc
CFLAGS=-Wall -Wpedantic -O3 -march=native `pkg-config --cflags MagickWand`
LDLIBS=-lm `pkg-config --libs MagickWand`

imgcomp: imgcomp.c
	$(CC) imgcomp.c -o imgcomp $(CFLAGS) $(LDLIBS)

.PHONY: clean
clean:
	rm imgcomp
