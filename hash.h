#ifndef HASH_H
#define HASH_H

#include <stdint.h>

#if __has_include(<MagickWand/MagickWand.h>)
	#include <MagickWand/MagickWand.h>
#elif __has_include(<wand/MagickWand.h>)
	#include <wand/MagickWand.h>
	#define LEGACY_MAGICKWAND 1
#endif

/* "[x]Filter" where x is one of Bessel Blackman Box Catrom Cubic Gaussian
   Hanning Hermite Lanczos Mitchell Point Quadratic Sinc Triangle */
#define SCALER HermiteFilter

uint64_t get_hash(char *filepath, int hash_algorithm);
int hammdist(uint64_t a, uint64_t b);

enum hash_algorithms {AHASH, DHASH, PHASH};

#endif
