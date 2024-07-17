#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hash.h"

static void ahash(MagickWand **mw, uint64_t *hash);
static void dhash(MagickWand **mw, uint64_t *hash);
static void phash(MagickWand **mw, uint64_t *hash);

uint64_t get_hash(char *filepath, int hash_algorithm)
{
	MagickWand *mw;
	uint64_t hash = 0;

	MagickWandGenesis();
	mw = NewMagickWand();

	if (!MagickReadImage(mw,filepath))
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

static void ahash(MagickWand **mw, uint64_t *hash)
{
	if (!*mw) return;

	uint64_t avg = 0;
	uint32_t outpixels[64];

	#ifndef LEGACY_MAGICKWAND
		MagickResizeImage(*mw, 8, 8, SCALER);
	#else
		MagickResizeImage(*mw, 8, 8, SCALER, 1);
	#endif
	MagickExportImagePixels(*mw, 0, 0, 8, 8, "I", LongPixel, outpixels);

	/* First pass, get avg colour */
	for (int pixelindex = 0; pixelindex < 64; pixelindex++) {
		avg += outpixels[pixelindex];
	}
	avg /= 64;

	/* Second pass, go through all pixels in the image and check against the avg */
	for (int pixelindex = 0; pixelindex < 64; pixelindex++)
	{
		*hash <<= 1;
		if (outpixels[pixelindex] > avg) {
			*hash |= 1;
		}
	}
}

static void dhash(MagickWand **mw, uint64_t *hash)
{
	if (!*mw) return;

	uint32_t outpixels[72];

	#ifndef LEGACY_MAGICKWAND
		MagickResizeImage(*mw, 9, 8, SCALER);
	#else
		MagickResizeImage(*mw, 9, 8, SCALER, 1);
	#endif
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

static void phash(MagickWand **mw, uint64_t *hash)
{
	if (!*mw) return;

	uint32_t outpixels[1024];
	long double dctavg = 0;
	double dct[64];
	double dctsum;

	#ifndef LEGACY_MAGICKWAND
		MagickResizeImage(*mw, 32, 32, SCALER);
	#else
		MagickResizeImage(*mw, 32, 32, SCALER, 1);
	#endif
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
