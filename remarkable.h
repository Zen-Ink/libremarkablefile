#pragma once
#include <stdint.h>
#include <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Paths are slash-separated display names relative to the library root.
 * Returned paths/names are UTF-8. All failures set errno; no librarian required. */
typedef void (*rml_list_cb)(const char *name, int directory, int64_t size, void *user);
int rml_init(const char *root);
const char *rml_root(void);
int rml_list(const char *path, rml_list_cb callback, void *user);
int rml_stat(const char *path, struct stat *st);
int rml_open(const char *path, int flags, unsigned mode);
int rml_finish(const char *path, int success);
int rml_mkdir(const char *path);
int rml_rename(const char *from, const char *to);
int rml_remove(const char *path);
void rml_abort_pending(void);
const char *rml_error(void);
#ifdef __cplusplus
}
#endif
