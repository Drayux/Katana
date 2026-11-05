#pragma once

// TODO: Make this a "pretty" registration type system. Not quite sure what
// that exactly looks like, but it's of low importance pending the final form
// of the plugins system. This entire file is somewhat a placeholder for that.

#include "ui.h"

#include <string.h>

extern frontend_t tui_frontend;
extern frontend_t livesplit_frontend;

static inline frontend_t * frontend_select(char const * frontend)
{
	frontend = frontend ? frontend : "";
	if (!strcmp(frontend, "LIVESPLIT")) return &livesplit_frontend;

	/* fallback */
	// if (!strcmp(frontend, "TUI")) return &tui_frontend;
	return &tui_frontend;

	// TODO: Real fallback is "NULL" which will set the UI mode to "headless"
	// No UI thread will run when this happens, but a flag will be set so that
	// main does not exit immediatley.
}
