#pragma once

#include "gui/app_window.h"

void timer_stop_and_reset(LSAppWindow * win);
void timer_start_split(LSAppWindow * win);
void timer_stop_or_reset(LSAppWindow * win);
void timer_unsplit(LSAppWindow * win);
void timer_skip(LSAppWindow * win);
void gui_timer_pause(LSAppWindow * win);
void timer_unpause(LSAppWindow * win);
void gui_timer_stop(LSAppWindow * win);
void gui_timer_split(LSAppWindow * win);
void timer_cancel_run(LSAppWindow * win);
