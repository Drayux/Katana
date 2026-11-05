#pragma once

#include "common.h"

typedef struct {
    int (* prepare)(void);
    int (* destroy)(void);
    int (* run)(int notify_fd);
    int (* notify)(void); // emitted whenever the timer state changes
    int (* stop)(void);
} frontend_t;

int ui_load(char const * frontend_name);
int ui_thread_start(pthread_t * thread);
void ui_thread_shutdown(void);
void ui_notify(void);
