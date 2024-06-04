imgcomp: imgcomp.c
	gcc imgcomp.c -o imgcomp `pkg-config --cflags --libs MagickWand` -lm -O3 -Wall -Wpedantic
clean:
	rm imgcomp
