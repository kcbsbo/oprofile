/**
 * @file op_file.h
 * Useful file management helpers
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#ifndef OP_FILE_H
#define OP_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

int op_file_readable(char const * file);
int op_get_fsize(char const * file, off_t * size);
time_t op_get_mtime(char const * file);
char * op_relative_to_absolute_path(
	char const * path, char const * base_dir);

/**
 * create_dir - create a directory
 * @param dir  the directory name to create
 *
 * Returns 0 on success.
 */
int create_dir(char const * dir);


/**
 * create_path - create a path
 * @param path  the path to create
 *
 * create directory for each dir components in path
 * the last path component is not considered as a directory
 * but as a filename
 *
 * Returns 0 on success.
 */
int create_path(char const * path);

#ifdef __cplusplus
}
#endif

#endif /* OP_FILE_H */
