/** \file detailed-timer.c
 *
 * Implementation of the "Detailed timer" component.
 */
#include "components.h"

#define MAX_PRECISION 6

/**
 * @brief The component representing the detailed timer part of the window.
 */
typedef struct LSDetailedTimer {
    LSComponent base; /*!< The base struct that is extended */
    GtkWidget* container; /*!< The root container for the detailed timer */
    GtkWidget* segment_info; /*!< Box */
    GtkWidget* segment_info_pb; /*!< Label */
    GtkWidget* segment_info_best; /*!< Label */
    GtkWidget* time_container; /*!< Box */
    GtkWidget* time; /*!< Box */
    GtkWidget* time_seconds; /*!< Label */
    GtkWidget* time_millis; /*!< Label */
    GtkWidget* segment; /*!< Box */
    GtkWidget* segment_seconds; /*!< Label */
    GtkWidget* segment_millis; /*!< Label */
	int comparison_method; /*<! Local setting for comparison_method */
	unsigned int precision; /*<! Local setting for comparison_method */
	unsigned int segment_precision; /*<! Local setting for comparison_method */
} LSDetailedTimer;
extern LSComponentOps ls_detailed_timer_operations;

/**
 * Constructor
 */
LSComponent* ls_component_detailed_timer_new(json_t* config)
{
    LSDetailedTimer* self;
    GtkWidget* spacer;
	json_t* ref;

    struct {
        bool show_segment_info;
        bool show_segment_timer;
        const char* method;
		int precision;
    } opt = { 0 };

    self = calloc(1, sizeof(LSDetailedTimer));
    if (!self)
        return NULL;
    self->base.ops = &ls_detailed_timer_operations;

    /* Configuration option: `detailed`
     * default: true
     * If true, show segment info in addition to the clock. */
    opt.show_segment_info = !(json_is_false(json_object_get(config, "detailed")));
	/* This option can trivially be split into two; currently implemented is
	 * the simpler version, where one option drives both detailed compoents. */
    opt.show_segment_timer = opt.show_segment_info;

    /* Configuration option: `method`
     * default: < game->comparison_method >
     * Timer will display the time value of the corresponding method, even if
	 * different from the splits setting. */
	self->comparison_method = -1; // unset
	ref = json_object_get(config, "method");
    opt.method = json_string_value(ref);
	if (opt.method) {
		if (!strcmp(opt.method, "real")) {
			self->comparison_method = 0;
		} else if (!strcmp(opt.method, "game")) {
			self->comparison_method = 1;
		}
	} else if (json_is_integer(ref)) {
		self->comparison_method = json_integer_value(ref);
	}

    /* Configuration option: `precision`
     * default: 2
     * Number of decimal points to show in the 'millis' section. */
    self->precision = 2;
	ref = json_object_get(config, "precision");
	if (json_is_integer(ref)) {
		opt.precision = json_integer_value(ref);
		if (opt.precision >= 0) {
			self->precision = (opt.precision > MAX_PRECISION) ?
				MAX_PRECISION : (unsigned int) opt.precision;
		}
	}

    /* Configuration option: `segment-precision`
     * default: < precision >
     * Number of decimal points to show in the 'millis' section of the segment. */
    self->segment_precision = self->precision;
	ref = json_object_get(config, "segment-precision");
	if (json_is_integer(ref)) {
		opt.precision = json_integer_value(ref);
		if (opt.precision >= 0) {
			self->segment_precision = (opt.precision > MAX_PRECISION) ?
				MAX_PRECISION : (unsigned int) opt.precision;
		}
	}

    /* --- End of configuration options --- */

	self->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	add_class(self->container, "timer-container");

	if (opt.show_segment_info) {
		/* Build the column displaying PB segment and best segment */
		self->segment_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		add_class(self->segment_info, "detailed-timer");
		gtk_widget_set_valign(self->segment_info, GTK_ALIGN_END);
		gtk_box_append(GTK_BOX(self->container), self->segment_info);

		self->segment_info_best = gtk_label_new(NULL);
		add_class(self->segment_info_best, "segment-best");

		self->segment_info_pb = gtk_label_new(NULL);
		add_class(self->segment_info_pb, "segment-pb");
		gtk_box_append(GTK_BOX(self->segment_info), self->segment_info_pb);
		gtk_box_append(GTK_BOX(self->segment_info), self->segment_info_best);
	}

	self->time_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	add_class(self->time_container, "timer");
	gtk_widget_set_hexpand(self->time_container, TRUE);
	gtk_box_append(GTK_BOX(self->container), self->time_container);

    self->time = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(self->time, "timer");
    add_class(self->time, "time");
	gtk_box_append(GTK_BOX(self->time_container), self->time);

	spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(spacer, TRUE);
	gtk_box_append(GTK_BOX(self->time), spacer);

    self->time_seconds = gtk_label_new(NULL);
    add_class(self->time_seconds, "timer-seconds");
    gtk_widget_set_valign(self->time_seconds, GTK_ALIGN_BASELINE_FILL);
    gtk_box_append(GTK_BOX(self->time), self->time_seconds);

	spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(spacer, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(self->time), spacer);

	self->time_millis = gtk_label_new(NULL);
	add_class(self->time_millis, "timer-millis");
	gtk_widget_set_valign(self->time_millis, GTK_ALIGN_BASELINE_FILL);
	gtk_box_append(GTK_BOX(spacer), self->time_millis);

	if (opt.show_segment_timer) {
		/* Build the segment timer (underneath main timer) */
		self->segment = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		add_class(self->segment, "segment");
		gtk_box_append(GTK_BOX(self->time_container), self->segment);

		spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_hexpand(spacer, TRUE);
		gtk_box_append(GTK_BOX(self->segment), spacer);

		self->segment_seconds = gtk_label_new(NULL);
		add_class(self->segment_seconds, "segment-seconds");
		gtk_widget_set_valign(self->segment_seconds, GTK_ALIGN_BASELINE_FILL);
		gtk_box_append(GTK_BOX(self->segment), self->segment_seconds);

		spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_valign(spacer, GTK_ALIGN_END);
		gtk_box_append(GTK_BOX(self->segment), spacer);

		self->segment_millis = gtk_label_new(NULL);
		add_class(self->segment_millis, "segment-millis");
		gtk_widget_set_valign(self->segment_millis, GTK_ALIGN_BASELINE_FILL);
		gtk_box_append(GTK_BOX(self->segment), self->segment_millis);
	}

    return (LSComponent*)self;
}

// Avoid collision with timer_delete of time.h
/**
 * Destructor
 *
 * @param self The component to destroy
 */
static void detailed_timer_delete(LSComponent* self)
{
    free(self);
}

/**
 * Returns the detailed timer GTK widget.
 *
 * @param self The detailed timer component itself.
 * @return The container as a GTK Widget.
 */
static GtkWidget* detailed_timer_widget(LSComponent* self)
{
    return ((LSDetailedTimer*)self)->container;
}

/**
 * Function to execute when ls_app_window_show_game is executed.
 *
 * @param self_ The timer component itself.
 * @param game The game splits struct instance.
 * @param timer The game timer instance.
 */
static void detailed_timer_show_game(LSComponent* self_, const ls_game* game,
    const ls_timer* timer)
{
    LSDetailedTimer* self = (LSDetailedTimer*)self_;
	if (self->comparison_method < 0) {
		self->comparison_method = game->comparison_method;
	}
}

/**
 * Function to execute when ls_app_window_clear_game is executed.
 *
 * @param self_ The detailed timer component itself.
 */
static void detailed_timer_clear_game(LSComponent* self_)
{
    LSDetailedTimer* self = (LSDetailedTimer*)self_;
    gtk_label_set_text(GTK_LABEL(self->time_seconds), "");
    gtk_label_set_text(GTK_LABEL(self->time_millis), "");

	if (self->segment) {
		gtk_label_set_text(GTK_LABEL(self->segment_seconds), "");
		gtk_label_set_text(GTK_LABEL(self->segment_millis), "");
	}

    remove_class(self->time, "behind");
    remove_class(self->time, "losing");
}

/**
 * Function to execute when ls_app_window_draw is executed.
 *
 * @param self_ The detailed timer component itself.
 * @param game The game struct instance.
 * @param timer The timer instance.
 */
static void detailed_timer_draw(LSComponent* self_, const ls_game* game, const ls_timer* timer)
{
    LSDetailedTimer* self = (LSDetailedTimer*)self_;
	long long time_val;
    char time_str[18];
    char ms_str[MAX_PRECISION + 2]; // [0] = '.' ; [8] = '\0'
    char pb[256] = "PB:    ";
    char best[256] = "Best: ";

    unsigned int curr = timer->curr_split;
    if (curr == game->split_count) {
        --curr;
    }

    remove_class(self->time, "delay");
    remove_class(self->time, "behind");
    remove_class(self->time, "losing");
    remove_class(self->time, "best-split");

	time_val = ls_time_get_by_method(ls_timer_get_time(timer, true), self->comparison_method);
    ls_time_millis_string(time_str, &ms_str[1], time_val);
	if (self->precision == 0) {
		ms_str[0] = '\0';
	} else {
		ms_str[0] = '.';
		ms_str[self->precision + 1] = '\0';
	}
    gtk_label_set_text(GTK_LABEL(self->time_seconds), time_str);
    gtk_label_set_text(GTK_LABEL(self->time_millis), ms_str);

    if (curr == game->split_count) {
        curr = game->split_count - 1;
    }
    if (time_val <= 0) {
        add_class(self->time, "delay");
    } else {
        if (timer->curr_split == game->split_count
            && timer->split_info[curr]
                & LS_INFO_BEST_SPLIT) {
            add_class(self->time, "best-split");
        } else {
            if (timer->split_info[curr]
                & LS_INFO_BEHIND_TIME) {
                add_class(self->time, "behind");
            }
            if (timer->split_info[curr]
                & LS_INFO_LOSING_TIME) {
                add_class(self->time, "losing");
            }
        }
    }

	if (self->segment && timer->started) {
		time_val = ls_time_get_by_method(timer->segment_times[timer->curr_split], self->comparison_method);
		ls_time_millis_string(time_str, &ms_str[1], time_val);
		if (self->segment_precision == 0) {
			ms_str[0] = '\0';
		} else {
			ms_str[0] = '.';
			ms_str[self->segment_precision + 1] = '\0';
		}
		gtk_label_set_text(GTK_LABEL(self->segment_seconds), time_str);
		gtk_label_set_text(GTK_LABEL(self->segment_millis), ms_str);
	}

	if (self->segment_info) {
		ls_time_string(&pb[6], ls_time_get_by_method(game->segment_times[timer->curr_split], self->comparison_method));
		gtk_label_set_text(GTK_LABEL(self->segment_info_pb), pb);

		ls_time_string(&best[6], ls_time_get_by_method(game->best_segments[timer->curr_split], self->comparison_method));
		gtk_label_set_text(GTK_LABEL(self->segment_info_best), best);
	}
}

LSComponentOps ls_detailed_timer_operations = {
    .delete = detailed_timer_delete,
    .widget = detailed_timer_widget,
    .show_game = detailed_timer_show_game,
    .clear_game = detailed_timer_clear_game,
    .draw = detailed_timer_draw
};
