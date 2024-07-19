#ifndef IMGCOMP_H
#define IMGCOMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include <sqlite3.h>

#define PROGRAM_NAME "imgcomp"

typedef struct Hashf
{
	uint64_t hash;
	char filepath[PATH_MAX];
	struct Hashf *next;
} Hashf;

typedef struct Copts
{
	int tolerance;
	int hash_algorithm;
	bool print_hashes;
	bool recurse_dirs;
} Copts;

void exit_with_error(char *message);
void ll_alloc(void);

extern Hashf *head, *hashes;

enum sql_stmt_e {SELECT_STMT, UPDATE_STMT, INSERT_STMT, STMT_TOTAL};

#endif
