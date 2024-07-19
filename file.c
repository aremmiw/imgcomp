#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "imgcomp.h"
#include "hash.h"
#include "file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static uint64_t check_hash(char *filepath, sqlite3 *db, sqlite3_stmt **stmts, struct stat stat_buf, Copts options);

char *extensions[] = {".jpeg", ".jpg", ".png", ".gif", ".tiff", ".tif", ".webp", ".jxl", ".bmp", ".avif"};

bool check_extension(char *filepath)
{
	static int ext_count = sizeof(extensions) / sizeof(*extensions);
	const char *file_ext = strrchr(filepath, '.');

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

static uint64_t check_hash(char *filepath, sqlite3 *db, sqlite3_stmt **stmts, struct stat stat_buf, Copts options)
{
	char hash_buffer[17] = {0};
	char *real_filepath = realpath(filepath, NULL);
	int statement_status;
	uint64_t hash = 0;

	sqlite3_bind_text(stmts[SELECT_STMT], 1, real_filepath, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmts[SELECT_STMT], 2, options.hash_algorithm);

	statement_status = sqlite3_step(stmts[SELECT_STMT]);

	if (statement_status == SQLITE_ROW
	    && (int64_t) strtoul((char *) sqlite3_column_text(stmts[SELECT_STMT], 1), NULL, 10) == stat_buf.st_mtim.tv_sec
	    && sqlite3_column_int(stmts[SELECT_STMT], 2) == stat_buf.st_size) {
		hash = (uint64_t) strtoul((char *) sqlite3_column_text(stmts[SELECT_STMT], 0), NULL, 16);
	}
	else if (statement_status == SQLITE_ERROR) {
		exit_with_error((char *) sqlite3_errmsg(db));
	}
	else
	{
		hash = get_hash(filepath, options.hash_algorithm);
		snprintf(hash_buffer, 17, "%.16lx", hash);

		if (statement_status == SQLITE_ROW)
		{
			sqlite3_bind_text(stmts[UPDATE_STMT], 1, hash_buffer, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[UPDATE_STMT], 2, stat_buf.st_size);
			sqlite3_bind_int(stmts[UPDATE_STMT], 3, stat_buf.st_mtim.tv_sec);
			sqlite3_bind_text(stmts[UPDATE_STMT], 4, real_filepath, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[UPDATE_STMT], 5, options.hash_algorithm);

			sqlite3_step(stmts[UPDATE_STMT]);
			statement_status = sqlite3_reset(stmts[UPDATE_STMT]);
		}
		else
		{
			sqlite3_bind_text(stmts[INSERT_STMT], 1, real_filepath, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[INSERT_STMT], 2, options.hash_algorithm);
			sqlite3_bind_text(stmts[INSERT_STMT], 3, hash_buffer, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmts[INSERT_STMT], 4, stat_buf.st_size);
			sqlite3_bind_int(stmts[INSERT_STMT], 5, stat_buf.st_mtim.tv_sec);

			sqlite3_step(stmts[INSERT_STMT]);
			statement_status = sqlite3_reset(stmts[INSERT_STMT]);
		}

		if (statement_status != SQLITE_OK) {
			exit_with_error((char *) sqlite3_errmsg(db));
		}
	}

	sqlite3_reset(stmts[SELECT_STMT]);

	free(real_filepath);

	return hash;
}

void add_hash(char *filepath, sqlite3 *db, sqlite3_stmt **stmts, Copts options)
{
	uint64_t hash = 0;
	struct stat stat_file;

	if (stat(filepath, &stat_file) == -1) {
		return;
	}

	hash = check_hash(filepath, db, stmts, stat_file, options);
	if (hash == 0xFFFFFFFFFFFFFFFF) {
		return;
	}

	ll_alloc();

	strncpy(hashes->filepath, filepath, PATH_MAX - 1);
	hashes->hash = hash;

	if (options.print_hashes) {
		printf("%s: %.*lx\n", hashes->filepath, 16, hashes->hash);
	}
}

