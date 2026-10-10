#pragma once

#include "dialog.h"

void ls_alert_error(GtkWindow * parent, char const * title,
	char const * message, char const * detail);
void ls_alert_warning(GtkWindow * parent, char const * title,
	char const * message, char const * detail);
void ls_alert_info(GtkWindow * parent, char const * title, char const * message,
	char const * detail);
void ls_alert(GtkWindow * parent, char const * title, char const * message,
	char const * detail, LSDialogIcon const * icon);
