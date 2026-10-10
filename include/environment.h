#pragma once

#include <stddef.h>

/* In here because calls to get_libresplit_..._folder need to be `free(ed)` */
#include <stdlib.h>

/* (Merged in from utils.c ... not the prettiest, but not super important yet TODO)*/
#include <stdbool.h>
#include <sys/types.h>
/* *** */

// TODO: Refactor these function names when reasonable

void getXDGruntimeDir(char * buffer, size_t size);
char const * get_libresplit_data_folder_path(void);
char const * get_libresplit_folder_path(void);
void check_directories(void);
bool create_default_directory(char const * name, char const * path, mode_t permissions);
