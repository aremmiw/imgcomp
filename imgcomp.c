#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <math.h>
#include <getopt.h>

#include <sqlite3.h>

#if __has_include(<MagickWand/MagickWand.h>)
	#include <MagickWand/MagickWand.h>
#elif __has_include(<wand/MagickWand.h>)
	#include <wand/MagickWand.h>
	#define LEGACY_MAGICKWAND 1
#endif

#define PROGRAM_NAME "imgcomp"
#define HASHLENGTH 64

/* "[x]Filter" where x is one of Bessel Blackman Box Catrom Cubic Gaussian
   Hanning Hermite Lanczos Mitchell Point Quadratic Sinc Triangle */
#define SCALER HermiteFilter

typedef struct hashf
{
	uint64_t hash;
	char filename[PATH_MAX];
	struct hashf *next;
} hashf;

void usage(void);

uint64_t gethash(char *filename, int hash_algorithm);
void ahash(MagickWand **mw, uint64_t *hash);
void dhash(MagickWand **mw, uint64_t *hash);
void phash(MagickWand **mw, uint64_t *hash);
int hammdist(uint64_t a, uint64_t b);

bool check_extension(char *filename);
void exit_with_error(char *message);

enum hash_algorithms {AHASH, DHASH, PHASH};
char *extensions[] = {".jpeg", ".jpg", ".png", ".gif", ".tiff", ".tif", ".webp", ".jxl", ".bmp", ".avif"};

int main(int argc, char **argv)
{
	hashf *hashes = NULL;
	hashf *head = NULL;
	int files, optc;
	int tolerance = 5;
	int hash_algorithm = DHASH;
	bool print_hashes = false;
	char dbpath[PATH_MAX];
	sqlite3 *db;

	static struct option const longopts[] =
	{
		{"ahash", no_argument, NULL, 'a'},
		{"dhash", no_argument, NULL, 'd'},
		{"phash", no_argument, NULL, 'p'},
		{"show-hashes", no_argument, NULL, 's'},
		{"tolerance", required_argument, NULL, 't'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while ((optc = getopt_long(argc, argv, "adpst:h", longopts, NULL)) != -1)
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
			case 't':
				tolerance = atoi(optarg);
				if (tolerance != 0 && (0 > tolerance || tolerance > 64 || strlen(optarg) != floor(log10(abs(tolerance))) + 1)) {
					exit_with_error("Invalid use of --tolerance, run '" PROGRAM_NAME " --help' for usage info.\n");
				}
				break;
			case 'h':
				usage();
				break;
			default:
				exit_with_error("Run '" PROGRAM_NAME " --help' for usage info.\n");
				break;
		}
	}

	files = argc - optind;

	if (files < 1) {
		usage();
	}

	if (getenv("XDG_CACHE_HOME")) {
		strncpy(dbpath, getenv("XDG_CACHE_HOME"), PATH_MAX - 25);
	}
	else if (getenv("HOME"))
	{
		strncpy(dbpath, getenv("HOME"), PATH_MAX - 25);
		strcat(dbpath, "/.cache");
	}
	else {
		exit_with_error("Check that $HOME or $XDG_CACHE_HOME is set\n");
	}

	struct stat cache_dirst;
	if (stat(dbpath, &cache_dirst) != 0 && errno == ENOENT) {
		mkdir(dbpath, 0755);
	}
	else if (stat(dbpath, &cache_dirst) != 0 || (cache_dirst.st_mode & S_IFMT) != S_IFDIR) {
		exit_with_error("Can't access cache directory.\n");
	}

	strcat(dbpath, "/" PROGRAM_NAME ".sqlite");

	if (sqlite3_open(dbpath, &db) != SQLITE_OK)
	{
		fprintf(stderr, "Failed to open the database file: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}
	else
	{
		char *init_db = "CREATE TABLE IF NOT EXISTS hashes(id INTEGER PRIMARY KEY, filename TEXT, hashtype INT, hash TEXT, filesize INT, mtime INT);";
		char *errmsg = NULL;

		if (sqlite3_exec(db, init_db, 0, 0, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "SQL error: %s\n", errmsg);
			sqlite3_free(errmsg);
			sqlite3_close(db);
			exit(1);
		}
		else
		{
			sqlite3_exec(db, "PRAGMA synchronous = OFF;", 0, 0, &errmsg);
			sqlite3_exec(db, "PRAGMA journal_mode = MEMORY;", 0, 0, &errmsg);
		}
	}

	sqlite3_stmt *insert_stmt, *select_stmt, *update_stmt;
	sqlite3_prepare_v2(db, "INSERT INTO hashes (id, filename, hashtype, hash, filesize, mtime) VALUES(NULL, ?1, ?2, ?3, ?4, ?5);", -1, &insert_stmt, 0);
	sqlite3_prepare_v2(db, "SELECT hash, mtime, filesize FROM hashes WHERE filename=?1 AND hashtype=?2", -1, &select_stmt, 0);
	sqlite3_prepare_v2(db, "UPDATE hashes SET hash=?1, filesize=?2, mtime=?3 WHERE filename=?4 AND hashtype=?5;", -1, &update_stmt, 0);

	for (int findex = 0; findex < files; findex++)
	{
		char hash_buffer[17] = {0};
		uint64_t hash;
		int select_status;
		struct stat stat_buf;

		if (stat(argv[findex + optind], &stat_buf) == -1
		    || (stat_buf.st_mode & S_IFMT) != S_IFREG
		    || strlen(argv[findex + optind]) > PATH_MAX
		    || !check_extension(argv[findex + optind])) {
			continue;
		}

		char *real_filepath = realpath(argv[findex + optind], NULL);

		sqlite3_bind_text(select_stmt, 1, real_filepath, -1, SQLITE_STATIC);
		sqlite3_bind_int(select_stmt, 2, hash_algorithm);

		select_status = sqlite3_step(select_stmt);

		if (select_status == SQLITE_ROW
		    && (int64_t) strtoul((char *) sqlite3_column_text(select_stmt, 1), NULL, 10) == stat_buf.st_mtim.tv_sec
		    && sqlite3_column_int(select_stmt, 2) == stat_buf.st_size) {
			hash = (uint64_t) strtoul((char *) sqlite3_column_text(select_stmt, 0), NULL, 16);
		}
		else
		{
			hash = gethash(argv[findex + optind], hash_algorithm);
			snprintf(hash_buffer, 17, "%.16lx", hash);

			if (select_status == SQLITE_ROW)
			{
				sqlite3_bind_text(update_stmt, 1, hash_buffer, -1, SQLITE_STATIC);
				sqlite3_bind_int(update_stmt, 2, stat_buf.st_size);
				sqlite3_bind_int(update_stmt, 3, stat_buf.st_mtim.tv_sec);
				sqlite3_bind_text(update_stmt, 4, real_filepath, -1, SQLITE_STATIC);
				sqlite3_bind_int(update_stmt, 5, hash_algorithm);

				sqlite3_step(update_stmt);
				sqlite3_reset(update_stmt);
			}
			else
			{
				sqlite3_bind_text(insert_stmt, 1, real_filepath, -1, SQLITE_STATIC);
				sqlite3_bind_int(insert_stmt, 2, hash_algorithm);
				sqlite3_bind_text(insert_stmt, 3, hash_buffer, -1, SQLITE_STATIC);
				sqlite3_bind_int(insert_stmt, 4, stat_buf.st_size);
				sqlite3_bind_int(insert_stmt, 5, stat_buf.st_mtim.tv_sec);

				sqlite3_step(insert_stmt);
				sqlite3_reset(insert_stmt);
			}
		}

		sqlite3_reset(select_stmt);

		free(real_filepath);

		if (hash == 0xFFFFFFFFFFFFFFFF) {
			continue;
		}

		if (head == NULL)
		{
			hashes = (hashf *) malloc(sizeof(*hashes));
			if (!hashes) {
				exit_with_error("ERROR: Failed to allocate memory.\n");
			}
			head = hashes;
			hashes->next = NULL;
		}
		else
		{
			hashes->next = (hashf *) malloc(sizeof(*hashes));
			if (!hashes->next) {
				exit_with_error("ERROR: Failed to allocate memory.\n");
			}
			hashes = hashes->next;
			hashes->next = NULL;
		}

		strncpy(hashes->filename, argv[findex + optind], PATH_MAX - 1);
		hashes->hash = hash;


		if (print_hashes) {
			printf("%s: %.*lx\n", hashes->filename, HASHLENGTH / 4, hashes->hash);
		}
	}

	sqlite3_finalize(select_stmt);
	sqlite3_finalize(insert_stmt);
	sqlite3_finalize(update_stmt);
	hashes = head;

	while(hashes != NULL)
	{
		hashf *x = hashes;
		hashf *y = x->next;
		while (y != NULL)
		{
			int hashdist = hammdist(x->hash, y->hash);
			if (hashdist < tolerance) {
				printf("%s and %s are similar with a dist of %d\n",
				x->filename, y->filename, hashdist);
			}
			y = y->next;
		}
		hashes = hashes->next;
		free(x);
	}

	sqlite3_close(db);

	return 0;
}

void usage(void)
{
	printf("Usage: %s [OPTION]... FILES...\n", PROGRAM_NAME);
	puts(	"Compare similarity of image files.\n\n"
		"Hashing algorithms:\n"
		"  -a, --ahash		use aHash (average hash)\n"
		"  -d, --dhash		use dHash [DEFAULT]\n"
		"  -p, --phash		use pHash (perceptive hash)\n\n"
		"Other options:\n"
		"  -s, --show-hashes	print calculated hashes of all files\n"
		"  -t, --tolerance=NUM	control how similar images must be to be considered\n"
		"			 'similar'. parameter NUM is an integer from\n"
		"			 0 (identical) to 64 (very different). defaults to 5\n"
		"  -h, --help		print this help");
	exit(0);
}

uint64_t gethash(char *filename, int hash_algorithm)
{
	MagickWand *mw;
	uint64_t hash = 0;

	MagickWandGenesis();
	mw = NewMagickWand();

	if (!MagickReadImage(mw,filename))
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

	#ifndef LEGACY_MAGICKWAND
		MagickResizeImage(*mw, 8, 8, SCALER);
	#else
		MagickResizeImage(*mw, 8, 8, SCALER, 1);
	#endif
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

void phash(MagickWand **mw, uint64_t *hash)
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

bool check_extension(char *filename)
{
	static int ext_count = sizeof(extensions) / sizeof(*extensions);
	const char *file_ext = strrchr(filename, '.');

	if (file_ext == NULL) {
		return false;
	}

	for (int i = 0; i < ext_count; i++) {
		if (strcasecmp(file_ext, extensions[i]) == 0) {
			return true;
		}
	}

	return false;
}

void exit_with_error(char *message)
{
	fprintf(stderr, "%s", message);
	exit(1);
}
