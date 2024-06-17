#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>

#include <MagickWand/MagickWand.h>

#ifndef __has_builtin
  #define __has_builtin(x) 0
#endif

#define PROGRAM_NAME "imgcomp"
#define HASHLENGTH 64

/* "[x]Filter" where x is one of Bessel Blackman Box Catrom Cubic Gaussian
   Hanning Hermite Lanczos Mitchell Point Quadratic Sinc Triangle */
#define SCALER HermiteFilter

typedef struct hashf
{
	uint64_t hash;
	char filename[FILENAME_MAX];
	struct hashf *next;
} hashf;

void usage(void);

uint64_t gethash(char *filename, int hash_algorithm);
void ahash(MagickWand **mw, uint64_t *hash);
void dhash(MagickWand **mw, uint64_t *hash);
void phash(MagickWand **mw, uint64_t *hash);
int hammdist(uint64_t a, uint64_t b);

bool check_extension(char *filename);

enum hash_algorithms {AHASH, DHASH, PHASH};
char *extensions[] = {".jpeg", ".jpg", ".png", ".gif", ".tiff", ".tif", ".webp", ".jxl", ".bmp", ".avif"};

int main(int argc, char **argv)
{
	hashf *hashes = NULL;
	hashf *head = NULL;
	int files, optc;
	int hash_algorithm = DHASH;
	bool print_hashes = false;

	static struct option const longopts[] =
	{
		{"ahash", no_argument, NULL, 'a'},
		{"dhash", no_argument, NULL, 'd'},
		{"phash", no_argument, NULL, 'p'},
		{"show-hashes", no_argument, NULL, 's'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while ((optc = getopt_long(argc, argv, "adpsh", longopts, NULL)) != -1)
	{
		switch (optc)
		{
			case 'a':
				hash_algorithm = AHASH;
				break;
			case 'd':
				hash_algorithm = DHASH;
				break;
			case 'p':
				hash_algorithm = PHASH;
				break;
			case 's':
				print_hashes = true;
				break;
			case 'h':
				usage();
				break;
			default:
				fprintf(stderr, "Run '%s --help' for usage info.\n", PROGRAM_NAME);
				exit(1);
				break;
		}
	}

	files = argc - optind;

	if (files < 1) {
		usage();
	}


	for (int findex = 0; findex < files; findex++)
	{
		if (strlen(argv[findex + optind]) > FILENAME_MAX) {
			continue;
		}

		uint64_t hash = gethash(argv[findex + optind], hash_algorithm);

		if (hash == 0xFFFFFFFFFFFFFFFF) {
			continue;
		}

		if (head == NULL)
		{
			hashes = (hashf *) malloc(sizeof(*hashes));
			if (!hashes) {
				fprintf(stderr, "ERROR: Failed to allocate memory.\n");
				return -1;
			}
			head = hashes;
		}
		else
		{
			hashes->next = (hashf *) malloc(sizeof(*hashes));
			if (!hashes->next) {
				fprintf(stderr, "ERROR: Failed to allocate memory.\n");
				return -1;
			}
			hashes = hashes->next;
		}

		strcpy(hashes->filename, argv[findex + optind]);
		hashes->hash = hash;

		if (print_hashes) {
			printf("%s: %.*lx\n", hashes->filename, HASHLENGTH / 4, hashes->hash);
		}
	}

	hashes = head;

	while(hashes != NULL)
	{
		hashf *x = hashes;
		hashf *y = x->next;
		while (y != NULL)
		{
			int hashdist = hammdist(x->hash, y->hash);
			if (hashdist < 5) {
				printf("%s and %s are similar with a dist of %d\n",
				x->filename, y->filename, hashdist);
			}
			y = y->next;
		}
		hashes = hashes->next;
		free(x);
	}

	return 0;
}

void usage(void)
{
	printf("Usage: %s [OPTION]... FILES...\n", PROGRAM_NAME);
	puts(	"Compare similarity of image files.\n\n"
		"  Hashing algorithms:\n"
		"  -a, --ahash		use aHash (average hash)\n"
		"  -d, --dhash		use dHash [DEFAULT]\n"
		"  -p, --phash		use pHash (perceptive hash)\n\n"
		"  -s, --show-hashes	print calculated hashes of all files\n"
		"  -h, --help		print this help");
	exit(0);
}

uint64_t gethash(char *filename, int hash_algorithm)
{
	MagickWand *mw;
	uint64_t hash = 0;

	MagickWandGenesis();
	mw = NewMagickWand();

	if (!check_extension(filename) || !MagickReadImage(mw,filename))
	{
		if (mw) {
			mw = DestroyMagickWand(mw);
		}
		MagickWandTerminus();
		return 0xFFFFFFFFFFFFFFFF; 
	}

	MagickSetImageType(mw, GrayscaleType);

	switch(hash_algorithm)
	{
		case AHASH:
			ahash(&mw, &hash);
			break;
		case DHASH:
			dhash(&mw, &hash);
			break;
		case PHASH:
			phash(&mw, &hash);
			break;
	}

	if (mw) {
		mw = DestroyMagickWand(mw);
	}

	MagickWandTerminus();
	return hash;
}

void ahash(MagickWand **mw, uint64_t *hash)
{
	if (!*mw) return;

	uint64_t avg = 0;
	uint32_t outpixels[64];

	MagickResizeImage(*mw, 8, 8, SCALER);
	MagickExportImagePixels(*mw, 0, 0, 8, 8, "I", LongPixel, outpixels);

	/* First pass, get avg colour */
	for (int pixelindex = 0; pixelindex < HASHLENGTH; pixelindex++) {
		avg += outpixels[pixelindex];
	}
	avg /= HASHLENGTH;

	/* Second pass, go through all pixels in the image and check against the avg */
	for (int pixelindex = 0; pixelindex < HASHLENGTH; pixelindex++)
	{
		*hash <<= 1;
		if (outpixels[pixelindex] > avg) {
			*hash |= 1;
		}
	}
}

void dhash(MagickWand **mw, uint64_t *hash)
{
	if (!*mw) return;

	uint32_t outpixels[72];

	MagickResizeImage(*mw, 9, 8, SCALER);
	MagickExportImagePixels(*mw, 0, 0, 9, 8, "I", LongPixel, outpixels);

	/* Rightmost pixel column is not part of the hash */
	for (int pixelindex = 0; pixelindex < 72; pixelindex++)
	{
		if ((pixelindex + 1) % 9 == 0) {
			continue;
		}
		*hash <<= 1;
		if (outpixels[pixelindex] < outpixels[pixelindex+1]) {
			*hash |= 1;
		}
	}
}

void phash(MagickWand **mw, uint64_t *hash)
{
	if (!*mw) return;

	uint32_t outpixels[1024];
	long double dctavg = 0;
	double dct[64];
	double dctsum;

	MagickResizeImage(*mw, 32, 32, SCALER);
	MagickExportImagePixels(*mw, 0, 0, 32, 32, "I", LongPixel, outpixels);

	/* Compute the DCT (only the top left 8x8) */
	for (int k = 0, g = 0; k < 232 && g < 64; k++, g++)
	{
		dctsum = 0;
		for (int n = 0; n < 1024; n++) {
			dctsum += outpixels[n] * cos(M_PI / 1024 * (n + 0.5) * k);
		}
		dct[g] = dctsum;
		if ((k + 1) % 8 == 0) {
			k += 24;
		}
	}

	for (int i = 1; i < 64; i++) {
		dctavg += dct[i];
	}
	dctavg /= 63;

	for (int i = 0; i < 64; i++)
	{
		*hash <<= 1;
		if (dct[i] < dctavg) {
			*hash |= 1;
		}
	}
}

int hammdist(uint64_t a, uint64_t b)
{
	#if __has_builtin(__builtin_popcountll)
		return __builtin_popcountll(a ^ b);
	#else
		int dist = 0;
		for (uint64_t xhash = a ^ b; xhash > 0; dist++) {
			xhash &= (xhash - 1);
		}
		return dist;
	#endif
}

bool check_extension(char *filename)
{
	static int ext_count = sizeof(extensions) / sizeof(*extensions);
	const char *file_ext = strrchr(filename, '.');
	int ext_length;

	if (file_ext == NULL) {
		return false;
	}

	for (int i = 0; i < ext_count; i++)
	{
		ext_length = strlen(extensions[i]);
		if (strncmp(file_ext, extensions[i], ext_length) == 0) {
			return true;
		}
	}

	return false;
}
