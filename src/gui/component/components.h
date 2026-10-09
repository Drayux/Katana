#pragma once

#include "gui/components/component.h"
#include "gui/utils.h"
#include "timer.h"
#include <gtk/gtk.h>
#include <jansson.h>

typedef LSComponent* (*ls_component_new_func)(json_t* config);

typedef struct _ComponentMetadata {
    char* name; /*!< Unique name of the component */
    ls_component_new_func new; /*!< New instance function pointer */
    bool is_default; /*|< Should this component be created by default */
} LSComponentAvailable;

typedef struct _ComponentRegistry {
    size_t count; /*!< Number of loaded components */
    size_t size; /*!< Size of the component registry */
    LSComponentAvailable* components; /*!< Array of the available components */
    bool enabled; /*!< Defines if the component registry is initialized and enabled */
} LSComponentRegistry;

extern LSComponentRegistry ls_components;

const LSComponentAvailable* get_component(const char* const name);
bool register_component(char*, ls_component_new_func);
bool init_components(void);

/* TODO: This was a bit lazy -- I put it here because multiple components
 * depend on this logic. However, this makes no sense as an external interface.
 * It should be moved to a private UI-related "library" */
GString* uri_from_path(const ls_game* game, const char* source_path);
/* *** */
