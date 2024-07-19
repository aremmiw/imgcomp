#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "imgcomp.h"
#include "hash.h"
#include "file.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/stat.h>
#include <math.h>
#include <getopt.h>

#include <sqlite3.h>

static void usage(void);
static void init_sqlitedb(sqlite3 **dbp);
static void compare_hashes(Copts options);

Hashf *head = NULL, *hashes = NULL;

int main(int argc, char **argv)
{
	int files, optc;
	Copts options =
	{
		.tolerance = 5,
		.hash_algorithm = DHASH,
		.print_hashes = false,
	};
	sqlite3 *db;

	sqlite3_stmt *stmts[STMT_TOTAL];

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
				options.hash_algorithm = AHASH;
				break;
			case 'd':
				options.hash_algorithm = DHASH;
				break;
			case 'p':
				options.hash_algorithm = PHASH;
				break;
			case 's':
				options.print_hashes = true;
				break;
			case 't':
				options.tolerance = atoi(optarg);
				if (options.tolerance != 0 && (0 > options.tolerance || options.tolerance > 64 || strlen(optarg) != floor(log10(abs(options.tolerance))) + 1)) {
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

	sqlite3_prepare_v2(db, "SELECT hash, mtime, filesize FROM hashes WHERE filepath=?1 AND hashtype=?2", -1, &stmts[SELECT_STMT], 0);
	sqlite3_prepare_v2(db, "UPDATE hashes SET hash=?1, filesize=?2, mtime=?3 WHERE filepath=?4 AND hashtype=?5;", -1, &stmts[UPDATE_STMT], 0);
	sqlite3_prepare_v2(db, "INSERT INTO hashes (id, filepath, hashtype, hash, filesize, mtime) VALUES(NULL, ?1, ?2, ?3, ?4, ?5);", -1, &stmts[INSERT_STMT], 0);

	for (int findex = 0; findex < files; findex++)
	{
		struct stat stat_buf;

		if (stat(argv[findex + optind], &stat_buf) == -1) {
			continue;
		}
		switch (stat_buf.st_mode & S_IFMT)
		{
			case S_IFREG:
				if (strlen(argv[findex + optind]) > PATH_MAX || !check_extension(argv[findex + optind])) {
					continue;
				}
				add_hash(argv[findex + optind], db, stmts, options);
				break;
			case S_IFDIR: /* TODO: add this */
				break;
			default:
				continue;
				break;
		}
	}

	sqlite3_finalize(stmts[SELECT_STMT]);
	sqlite3_finalize(stmts[UPDATE_STMT]);
	sqlite3_finalize(stmts[INSERT_STMT]);
	sqlite3_close(db);

	compare_hashes(options);

	return 0;
}

static void usage(void)
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

void exit_with_error(char *message)
{
	fprintf(stderr, "ERROR: %s\n", message);

	while (head) {
		Hashf *x = head->next;
		free(head);
		head = x;
	}

	exit(1);
}

static void init_sqlitedb(sqlite3 **dbp)
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
		char *init_db = "CREATE TABLE IF NOT EXISTS hashes(id INTEGER PRIMARY KEY, filepath TEXT, hashtype INT, hash TEXT, filesize INT, mtime INT);";
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

void ll_alloc(void)
{
	if (head == NULL)
	{
		hashes = (Hashf *) malloc(sizeof(*hashes));
		if (!hashes) {
			exit_with_error("ERROR: Failed to allocate memory.\n");
		}
		head = hashes;
		hashes->next = NULL;
	}
	else
	{
		hashes->next = (Hashf *) malloc(sizeof(*hashes));
		if (!hashes->next) {
			exit_with_error("ERROR: Failed to allocate memory.\n");
		}
		hashes = hashes->next;
		hashes->next = NULL;
	}
}

static void compare_hashes(Copts options)
{
	while (head != NULL)
	{
		Hashf *x = head;
		Hashf *y = x->next;
		while (y != NULL)
		{
			int hashdist = hammdist(x->hash, y->hash);
			if (hashdist < options.tolerance) {
				printf("%s and %s are similar with a dist of %d\n",
				x->filepath, y->filepath, hashdist);
			}
			y = y->next;
		}
		head = head->next;
		free(x);
		x = NULL;
	}
}
