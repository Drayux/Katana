#include "environment.h"

#include "common.h"
#include "logging.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// #include "gui/widgets/alert.h"

#include <linux/limits.h>
#include <pwd.h>
#include <sys/stat.h>

/**
 * Creates a directory tree recursively.
 *
 * Works like the "mkdir -p" command on shell, creating
 * a directory and all its parents if necessary.
 *
 * Adapted from https://stackoverflow.com/a/2336245
 *
 * @param dir The path describing the resulting directory tree.
 * @param permissions The attributes used to create the directories.
 * @param name optional name for the error message on failure
 * @return bool Whether or not the directory creation was successful
 */
static bool mkdir_p(char const * dir, mode_t permissions, char const * name)
{
	char * path = NULL;
	char * p = NULL;
	size_t len;

	// create a mutable copy
	path = strdup(dir);
	if (path == NULL) {
		return false;
	}

	len = strlen(path);
	if (path[len - 1] == '/') {
		path[len - 1] = 0;
	}

	for (p = path + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (!create_default_directory(name != NULL ? name : "requested",
					path, permissions)) {
				free(path);
				return false;
			}

			*p = '/';
		}
	}

	bool ret = create_default_directory(
		name != NULL ? name : "requested", path, permissions);
	free(path);
	return ret;
}

/**
 * @brief Attempts to create a directory at a specified path, intended for the
 * default directories LibreSplit uses. If the directory already exist this
 * returns true before creating. Otherwise the directory is attempted to be
 * created. If the creation fails, then displays an alert to the user indicating
 * that the creation failed and logs the error.
 *
 * @param name The name of the directory type i.e. Splits for the splits
 * directory
 * @param path The path to the directory to create.
 * @param permissions The permissions to give to the directory.
 * @param parent The parent window for the potential user error message on
 * failure.
 * @return bool Whether or not the directory creation was successful
 */
bool create_default_directory(char const * name, char const * path,
	mode_t permissions)
{
	struct stat st = {0};
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
		return true;
	}

	if (mkdir(path, permissions) != 0) {
		LOG_ERRF("Failed to create directory: %s: %s", path, strerror(errno));

		// char error_msg[PATH_MAX];
		// snprintf(error_msg, sizeof error_msg,
			// "We were unable to create the %s directory at:\n%s", name, path);
		// ls_alert_error(
			// parent, "Error", "Unable to create directory", error_msg);
		return false;
	}

	return true;
}


/* --- */


/**
 * Gets the user's runtime directory.
 *
 * Falls back to /run/user/uid if XDG_RUNTIME_DIR is not set.
 *
 * @param buffer The buffer to write the runtime directory to.
 * @param size The size of the destination buffer.
 */
void getXDGruntimeDir(char * buffer, size_t size)
{
	char const * xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
	buffer[0] = '\0';
	if (xdg_runtime_dir) {
		strncpy(buffer, xdg_runtime_dir, size - 1);
		buffer[size - 1] = '\0';
		return;
	}

	int const uid = getuid();
	snprintf(buffer, size, "/run/user/%d", uid);
}

/**
 * Copies the user's LibreSplit data path in a given string.
 *
 * @param out_path The string to copy the data path into.
 */
char const * get_libresplit_data_folder_path(void)
{
	char const * const xdg_data_dir = getenv("XDG_DATA_HOME");
	char const * const home_dir = getenv("HOME");

	char const * const format = xdg_data_dir ?
		"%s/katana" : "%s/.local/share/katana";
	char const * const value = xdg_data_dir ?
		xdg_data_dir : home_dir;

	char * output_buffer;
	size_t len;

	if (!value) {
		LOG_WARN("Data path could not be resolved (try setting HOME or XDG_DATA_HOME)");
		return NULL;
	}

	len = snprintf(NULL, 0, format, value);
	if (len >= PATH_MAX) {
		LOG_WARN("Data path is too long");
		return NULL;
	}

	output_buffer = malloc(len + 1);
	ASSERT_ALLOC(output_buffer, NULL);

	(void) sprintf(output_buffer, format, value);
	return output_buffer;
}

/**
 * Copies the user's LibreSplit configuration path in a given string.
 *
 * @param out_path The string to copy the configuration path into.
 *
 * TODO: This is similar enough to the data version that they could be merged.
 */
char const * get_libresplit_folder_path(void)
{
	char const * const xdg_config_dir = getenv("XDG_CONFIG_HOME");
	char const * const home_dir = getenv("HOME");

	char const * const format = xdg_config_dir ?
		"%s/katana" : "%s/.config/katana";
	char const * const value = xdg_config_dir ?
		xdg_config_dir : home_dir;

	char * output_buffer;
	size_t len;

	if (!value) {
		LOG_WARN("Config path could not be resolved (try setting HOME or XDG_CONFIG_HOME)");
		return NULL;
	}

	len = snprintf(NULL, 0, format, value);
	if (len >= PATH_MAX) {
		LOG_WARN("Config path is too long");
		return NULL;
	}

	output_buffer = malloc(len + 1);
	ASSERT_ALLOC(output_buffer, NULL);

	(void) sprintf(output_buffer, format, value);
	return output_buffer;
}

/**
 * Checks and creates libresplit config directories.
 *
 * Performs a directory check, creating the libresplit
 * config directory if necessary.
 */
void check_directories(void)
{
	const char * libresplit_directory;
	const char * libresplit_data_directory;

	libresplit_directory = get_libresplit_folder_path();
	if (!libresplit_directory) {
		return;
	}

	libresplit_data_directory = get_libresplit_data_folder_path();
	if (!libresplit_data_directory) {
		free((void *) libresplit_directory);
		return;
	}

	char auto_splitters_directory[PATH_MAX];
	char themes_directory[PATH_MAX];
	char splits_directory[PATH_MAX];
	char runs_directory[PATH_MAX];
	char plugins_directory[PATH_MAX];

	strcpy(auto_splitters_directory, libresplit_directory);
	strcat(auto_splitters_directory, "/auto-splitters");

	strcpy(themes_directory, libresplit_directory);
	strcat(themes_directory, "/themes");

	strcpy(splits_directory, libresplit_directory);
	strcat(splits_directory, "/splits");

	strcpy(runs_directory, libresplit_directory);
	strcat(runs_directory, "/runs");

	strcpy(plugins_directory, libresplit_data_directory);
	strcat(plugins_directory, "/plugins");

	// Make the libresplit data directory if it doesn't exist
	if (!mkdir_p(libresplit_data_directory, 0755, "LibreSplit Data"));
	// Make the libresplit config directory if it doesn't exist
	else if (!mkdir_p(libresplit_directory, 0755, "LibreSplit Config"));
	else {
		// Make the autosplitters directory if it doesn't exist
		create_default_directory("autosplitters directory", auto_splitters_directory, 0755);

		// Make the themes directory if it doesn't exist
		create_default_directory("themes directory", themes_directory, 0755);

		// Make the splits directory if it doesn't exist
		create_default_directory("splits directory", splits_directory, 0755);

		// Make the runs directory if it doesn't exist
		create_default_directory("runs directory", runs_directory, 0755);
	}

	free((void *) libresplit_directory);
	free((void *) libresplit_data_directory);
}
