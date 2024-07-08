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
void init_sqlitedb(sqlite3 **dbp);
void ll_alloc();
void compare_hashes(int tolerance);
uint64_t check_hash(char *filename, sqlite3 *db, sqlite3_stmt **stmts, struct stat stat_buf, int hash_algorithm);

enum hash_algorithms {AHASH, DHASH, PHASH};
char *extensions[] = {".jpeg", ".jpg", ".png", ".gif", ".tiff", ".tif", ".webp", ".jxl", ".bmp", ".avif"};
hashf *hashes = NULL;
hashf *head = NULL;

int main(int argc, char **argv)
{
	int files, optc;
	int tolerance = 5;
	int hash_algorithm = DHASH;
	bool print_hashes = false;
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

	init_sqlitedb(&db);

	sqlite3_stmt *stmts[3];
	sqlite3_prepare_v2(db, "SELECT hash, mtime, filesize FROM hashes WHERE filename=?1 AND hashtype=?2", -1, &stmts[0], 0);
	sqlite3_prepare_v2(db, "INSERT INTO hashes (id, filename, hashtype, hash, filesize, mtime) VALUES(NULL, ?1, ?2, ?3, ?4, ?5);", -1, &stmts[1], 0);
	sqlite3_prepare_v2(db, "UPDATE hashes SET hash=?1, filesize=?2, mtime=?3 WHERE filename=?4 AND hashtype=?5;", -1, &stmts[2], 0);

	for (int findex = 0; findex < files; findex++)
	{
		uint64_t hash;
		struct stat stat_buf;

		if (stat(argv[findex + optind], &stat_buf) == -1
		    || (stat_buf.st_mode & S_IFMT) != S_IFREG
		    || strlen(argv[findex + optind]) > PATH_MAX
		    || !check_extension(argv[findex + optind])) {
			continue;
		}

		hash = check_hash(argv[findex + optind], db, stmts, stat_buf, hash_algorithm);

		if (hash == 0xFFFFFFFFFFFFFFFF) {
			continue;
		}

		ll_alloc();

		strncpy(hashes->filename, argv[findex + optind], PATH_MAX - 1);
		hashes->hash = hash;

		if (print_hashes) {
			printf("%s: %.*lx\n", hashes->filename, HASHLENGTH / 4, hashes->hash);
		}

	}

	sqlite3_finalize(stmts[0]);
	sqlite3_finalize(stmts[1]);
	sqlite3_finalize(stmts[2]);
	sqlite3_close(db);

	compare_hashes(tolerance);

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
	fprintf(stderr, "ERROR: %s\n", message);

	while (head) {
		hashf *x = head->next;
		free(head);
		head = x;
	}

	exit(1);
}

void init_sqlitedb(sqlite3 **dbp)
{
	char dbpath[PATH_MAX];
	struct stat cache_dirst;

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

	if (stat(dbpath, &cache_dirst) != 0 && errno == ENOENT) {
		mkdir(dbpath, 0755);
	}
	else if (stat(dbpath, &cache_dirst) != 0 || (cache_dirst.st_mode & S_IFMT) != S_IFDIR) {
		exit_with_error("Can't access cache directory.\n");
	}

	strcat(dbpath, "/" PROGRAM_NAME ".sqlite");

	if (sqlite3_open(dbpath, dbp) != SQLITE_OK)
	{
		fprintf(stderr, "ERROR: %s\n", sqlite3_errmsg(*dbp));
		sqlite3_close(*dbp);
		exit(1);
	}
	else
	{
		char *init_db = "CREATE TABLE IF NOT EXISTS hashes(id INTEGER PRIMARY KEY, filename TEXT, hashtype INT, hash TEXT, filesize INT, mtime INT);";
		char *errmsg = NULL;

		if (sqlite3_exec(*dbp, init_db, 0, 0, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "ERROR: %s\n", errmsg);
			sqlite3_free(errmsg);
			sqlite3_close(*dbp);
			exit(1);
		}
		else
		{
			sqlite3_exec(*dbp, "PRAGMA synchronous = OFF;", 0, 0, &errmsg);
			sqlite3_exec(*dbp, "PRAGMA journal_mode = MEMORY;", 0, 0, &errmsg);
		}
	}

}

void ll_alloc()
{
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
}

void compare_hashes(int tolerance)
{
	while (head != NULL)
	{
		hashf *x = head;
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
		head = head->next;
		free(x);
		x = NULL;
	}
}

uint64_t check_hash(char *filename, sqlite3 *db, sqlite3_stmt **stmts, struct stat stat_buf, int hash_algorithm)
{
	char hash_buffer[17] = {0};
	char *real_filepath = realpath(filename, NULL);
	int statement_status;
	uint64_t hash;

	sqlite3_bind_text(stmts[0], 1, real_filepath, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmts[0], 2, hash_algorithm);

	statement_status = sqlite3_step(stmts[0]);

	if (statement_status == SQLITE_ROW
	    && (int64_t) strtoul((char *) sqlite3_column_text(stmts[0], 1), NULL, 10) == stat_buf.st_mtim.tv_sec
	    && sqlite3_column_int(stmts[0], 2) == stat_buf.st_size) {
		hash = (uint64_t) strtoul((char *) sqlite3_column_text(stmts[0], 0), NULL, 16);
	}
	else if (statement_status == SQLITE_ERROR) {
		exit_with_error((char *) sqlite3_errmsg(db));
	}
	else
	{
		hash = gethash(filename, hash_algorithm);
		snprintf(hash_buffer, 17, "%.16lx", hash);

		if (statement_status == SQLITE_ROW)
		{
			sqlite3_bind_text(stmts[2], 1, hash_buffer, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[2], 2, stat_buf.st_size);
			sqlite3_bind_int(stmts[2], 3, stat_buf.st_mtim.tv_sec);
			sqlite3_bind_text(stmts[2], 4, real_filepath, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[2], 5, hash_algorithm);

			sqlite3_step(stmts[2]);
			statement_status = sqlite3_reset(stmts[2]);
		}
		else
		{
			sqlite3_bind_text(stmts[1], 1, real_filepath, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[1], 2, hash_algorithm);
			sqlite3_bind_text(stmts[1], 3, hash_buffer, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[1], 4, stat_buf.st_size);
			sqlite3_bind_int(stmts[1], 5, stat_buf.st_mtim.tv_sec);

			sqlite3_step(stmts[1]);
			statement_status = sqlite3_reset(stmts[1]);
		}

		if (statement_status != SQLITE_OK) {
			exit_with_error((char *) sqlite3_errmsg(db));
		}
	}

	sqlite3_reset(stmts[0]);

	free(real_filepath);

	return hash;
}
