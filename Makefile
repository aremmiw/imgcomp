CC=gcc
CFLAGS=-Wall -Wpedantic -O3 -march=native `pkg-config --cflags MagickWand`
LDLIBS=-lm -lsqlite3 `pkg-config --libs MagickWand`

objs=imgcomp.o hash.o file.o

imgcomp: $(objs)
	$(CC) -o $@ $(objs) $(CFLAGS) $(LDLIBS)

.c.o:
	$(CC) $< -o $@ -c $(CFLAGS)

.PHONY: clean
clean:
	rm imgcomp *.o
