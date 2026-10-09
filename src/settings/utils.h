#pragma once

#include <gtk/gtk.h>
#include <stdbool.h>

void get_libresplit_data_folder_path(char * out_path);
void get_libresplit_folder_path(char * out_path);
void check_directories(void);
bool create_default_directory(char const * name, char const * path,
	mode_t permissions, GtkWindow * parent);
