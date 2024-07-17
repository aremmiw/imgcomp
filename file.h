#ifndef FILE_H
#define FILE_H

#include "imgcomp.h"

#include <stdbool.h>
#include <sqlite3.h>

bool check_extension(char *filepath);
void add_hash(char *filepath, sqlite3 *db, sqlite3_stmt **stmts, Copts options);

#endif
