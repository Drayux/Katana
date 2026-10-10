#include "lasr/utils.h"

#include "gui/dialogs.h"
#include "lasr/auto-splitter.h"
#include "lasr/maps/maps.h"
#include "logging.h"

#include <glib.h>
#include <stdatomic.h>
#include <stdio.h>

game_process process;

/**
 * Restarts the auto splitter by disabling it and re-enabling it again
 *
 * @return true if the auto splitter was enabled before the restart, false
 * otherwise
 */
bool restart_auto_splitter(void)
{
	bool const was_asl_enabled = atomic_load(&auto_splitter_enabled);
	if (was_asl_enabled) {
		stop_auto_splitter();
		atomic_store(&auto_splitter_enabled, true);
	}
	return was_asl_enabled;
}

/**
 * Associates a 'lasr_global' container tracking a lua value with the lua
 * runtime itself. Omitting this call, the value will remain unchanged
 * regardless of the lua state.
 *
 * NOTE: This cannot be called by the main thread and thus, cannot be safely
 * called while the splitter is running.
 *
 * @param container A non-null reference to a 'lasr_global' container to be
 * tracked.
 */
void register_shared_global(lasr_global * new)
{
	if (atomic_load(&auto_splitter_running)) {
		/* Reject this call if the autosplitter is running (developer error if
		 * this happens.)
		 * The linked list is not atomic, so we are certain to spontaneously
		 * crash if this were ignored */
		LOG_DEBUGF("Reject registration of export var `%s`",
			new ? new->key : "<none>");
		return;
	}
	else if (!new) {
		/* Valid flow, nothing to do. */
		return;
	}

	atomic_store(&new->held, true);
	new->next = shared_globals;
	shared_globals = new;

	LOG_DEBUGF("Register export var `%s`", new ? new->key : "<none>");
}

/**
 * Gets the base address of a module.
 *
 * @param module The module name for which to find the base address of. If NULL,
 * the main process is used.
 *
 * @return The base address of the chosen module.
 */
uintptr_t find_base_address(char const * module)
{
	char const * module_to_grep = module == 0 ? process.name : module;

	ProcessMap map;
	bool const found = maps_findMapByName(module_to_grep, &map);
	if (found) {
		return map.start;
	}
	return 0;
}

/**
 * Prints a memory error to stdout.
 *
 * @param err The error code to print.
 *
 * @return True if the error was printed, false if the error is unknown.
 */
bool handle_memory_error(uint32_t err)
{
	static bool shownDialog = false;
	if (err == 0)
		return false;
	switch (err) {
		case EFAULT:
			printf("[readAddress] EFAULT: Invalid memory space/address\n");
			break;
		case EINVAL:
			printf("[readAddress] EINVAL: An error ocurred while reading "
				   "memory\n");
			break;
		case ENOMEM:
			printf("[readAddress] ENOMEM: Please get more memory\n");
			break;
		case EPERM:
			printf("[readAddress] EPERM: Permission denied\n");

			if (!shownDialog) {
				shownDialog = true;
				g_idle_add(display_non_capable_mem_read_dialog, NULL);
			}

			break;
		case ESRCH:
			printf(
				"[readAddress] ESRCH: No process with specified PID exists\n");
			break;
	}
	return true;
}

/**
 * Utility function to convert a lua value to a string.
 *
 * Converts a value to a printable C string according to its type.
 * This is due to lua_tostring returning "null" for booleans and
 * other non-string types.
 */
char const * value_to_c_string(lua_State * L, int index)
{
	switch (lua_type(L, index)) {
		case LUA_TSTRING:
			return lua_tostring(L, index);
		case LUA_TNUMBER:
			return lua_tostring(L, index);
		case LUA_TBOOLEAN:
			return lua_toboolean(L, index) ? "true" : "false";
		case LUA_TNIL:
			return "nil";
		default:
			return "??";
	}
}
