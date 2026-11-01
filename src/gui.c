#include "common.h"
#include <gtk/gtk.h>
#include <glib.h>
#include <gio/gio.h>
#include <pthread.h>

static GtkWidget *window;
static PatternEngine *current_engine = NULL;
static gboolean poll_engine(gpointer data);
static GtkWidget *statusbar;
static guint status_ctx;

/* control tab widgets */
typedef struct {
    int idx;
    GtkWidget *scale;
    GtkWidget *sw;
    GtkWidget *combo;
} LedRow;
static LedRow led_rows[MAX_LEDS];
static int nled_rows = 0;

/* pattern tab */
static GtkWidget *pat_combo;
static GtkWidget *led_check[MAX_LEDS];
static GtkWidget *param_spin[8];
static const char *param_keys[8] = {"speed","freq","step","count","interval","level","duty","unit"};
static GtkWidget *pat_status;

/* notify tab */
static GtkWidget *notify_led_combo;
static GtkWidget *notify_count;
static GtkWidget *notify_interval;
static GtkWidget *daemon_status_label;
static GtkWidget *daemon_btn;
static volatile int daemon_running = 0;
static pthread_t daemon_thread;
static volatile int daemon_thread_active = 0;
extern int daemon_indices[64];
extern int daemon_nleds;
static GtkWidget *notify_checks[MAX_LEDS];

static void status_msg(const char *msg) {
    gtk_statusbar_pop(GTK_STATUSBAR(statusbar), status_ctx);
    gtk_statusbar_push(GTK_STATUSBAR(statusbar), status_ctx, msg);
}

static void all_off_cb(GtkWidget *w, gpointer d) {
    int indices[MAX_LEDS];
    for (int i = 0; i < nleds; i++) indices[i] = i;
    all_off(indices, nleds);
    for (int i = 0; i < nled_rows; i++) {
        gtk_range_set_value(GTK_RANGE(led_rows[i].scale), 0);
        gtk_switch_set_active(GTK_SWITCH(led_rows[i].sw), FALSE);
    }
    status_msg("All off");
}

static void all_max_cb(GtkWidget *w, gpointer d) {
    for (int i = 0; i < nleds; i++) led_set(i, led_max(i));
    for (int i = 0; i < nled_rows; i++) {
        gtk_range_set_value(GTK_RANGE(led_rows[i].scale), led_max(led_rows[i].idx));
        gtk_switch_set_active(GTK_SWITCH(led_rows[i].sw), TRUE);
    }
    status_msg("All max");
}

static void on_scale_changed(GtkRange *r, gpointer data) {
    LedRow *row = (LedRow *)data;
    int val = (int)gtk_range_get_value(r);
    led_set(row->idx, val);
}

static void on_switch_state_set(GtkSwitch *sw, gboolean state, gpointer data) {
    LedRow *row = (LedRow *)data;
    if (state) {
        int v = (int)gtk_range_get_value(GTK_RANGE(row->scale));
        if (v == 0) { v = led_max(row->idx); gtk_range_set_value(GTK_RANGE(row->scale), v); }
        led_set(row->idx, v);
    } else {
        led_set(row->idx, 0);
    }
    return;
}

static void on_trigger_changed(GtkComboBox *combo, gpointer data) {
    LedRow *row = (LedRow *)data;
    gchar *txt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (txt) { led_set_trigger(row->idx, txt); g_free(txt); }
}

static GtkWidget *build_control(void) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 12); gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12); gtk_widget_set_margin_bottom(grid, 12);

    const char *headers[] = {"LED", "Brightness", "On/Off", "Trigger"};
    for (int j = 0; j < 4; j++) {
        GtkWidget *l = gtk_label_new(headers[j]);
        gtk_grid_attach(GTK_GRID(grid), l, j, 0, 1, 1);
    }

    nled_rows = 0;
    for (int i = 0; i < nleds; i++) {
        int row = i + 1;
        GtkWidget *lbl = gtk_label_new(led_display_name(i));
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);

        GtkAdjustment *adj = gtk_adjustment_new(led_get(i), 0, led_max(i), 1, 1, 0);
        GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
        gtk_scale_set_digits(GTK_SCALE(scale), 0);
        gtk_widget_set_hexpand(scale, TRUE);
        gtk_grid_attach(GTK_GRID(grid), scale, 1, row, 1, 1);

        GtkWidget *sw = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(sw), led_get(i) > 0);
        gtk_grid_attach(GTK_GRID(grid), sw, 2, row, 1, 1);

        GtkWidget *combo = gtk_combo_box_text_new();
        int cnt = 0;
        char **trigs = led_triggers(i, &cnt);
        for (int t = 0; t < cnt; t++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), trigs[t]);
        /* set active to current trigger */
        char *cur = led_trigger(i);
        for (int t = 0; t < cnt; t++) {
            if (strcmp(trigs[t], cur) == 0) { gtk_combo_box_set_active(GTK_COMBO_BOX(combo), t); break; }
        }
        gtk_grid_attach(GTK_GRID(grid), combo, 3, row, 1, 1);

        led_rows[nled_rows].idx = i;
        led_rows[nled_rows].scale = scale;
        led_rows[nled_rows].sw = sw;
        led_rows[nled_rows].combo = combo;
        nled_rows++;

        g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), &led_rows[nled_rows-1]);
        g_signal_connect(sw, "state-set", G_CALLBACK(on_switch_state_set), &led_rows[nled_rows-1]);
        g_signal_connect(combo, "changed", G_CALLBACK(on_trigger_changed), &led_rows[nled_rows-1]);
    }

    /* buttons */
    GtkWidget *btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *offbtn = gtk_button_new_with_label("All Off");
    g_signal_connect(offbtn, "clicked", G_CALLBACK(all_off_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), offbtn, FALSE, FALSE, 0);
    GtkWidget *maxbtn = gtk_button_new_with_label("All Max");
    g_signal_connect(maxbtn, "clicked", G_CALLBACK(all_max_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), maxbtn, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), btnbox, 0, nleds + 1, 4, 1);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), grid);
    return scrolled;
}

/* pattern tab */
static void start_pattern_cb(GtkWidget *w, gpointer d) {
    if (current_engine) {
        pattern_stop(current_engine, 1);
        free(current_engine->indices);
        free(current_engine);
        current_engine = NULL;
    }
    /* selected leds */
    int indices[64]; int ni = 0;
    for (int i = 0; i < nleds; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(led_check[i]))) {
            indices[ni++] = i;
        }
    }
    if (ni == 0 && nleds > 0) { indices[0] = 0; ni = 1; }
    /* pattern name */
    gchar *pname = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(pat_combo));
    if (!pname) pname = g_strdup("solid");

    current_engine = malloc(sizeof(PatternEngine));
    pattern_init(current_engine, indices, ni, pname);
    /* params */
    for (int p = 0; p < 8; p++) {
        double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(param_spin[p]));
        pattern_set_param(current_engine, param_keys[p], v);
    }
    pattern_start(current_engine, 1);
    char msg[256];
    snprintf(msg, sizeof(msg), "Running: %s", pname);
    gtk_label_set_text(GTK_LABEL(pat_status), msg);
    g_free(pname);
    g_timeout_add(200, poll_engine, NULL);

    /* poll */
    /* use timeout */
    /* store in a simple loop via g_timeout_add */
}

static gboolean poll_engine(gpointer data) {
    if (current_engine && pattern_running(current_engine)) {
        return TRUE;
    }
    if (current_engine) {
        gtk_label_set_text(GTK_LABEL(pat_status), "Idle");
    }
    return FALSE;
}

static void stop_pattern_cb(GtkWidget *w, gpointer d) {
    if (current_engine) {
        pattern_stop(current_engine, 1);
        free(current_engine->indices);
        free(current_engine);
        current_engine = NULL;
        gtk_label_set_text(GTK_LABEL(pat_status), "Idle");
    }
}

static void fast_flash_cb(GtkWidget *w, gpointer d) {
    /* set strobe + high freq, then start */
    gtk_combo_box_set_active(GTK_COMBO_BOX(pat_combo), 3); /* strobe is index 3? patterns list: off0 solid1 blink2 strobe3 */
    /* find freq spinner index (param_keys[1]) */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(param_spin[1]), 12.0);
    start_pattern_cb(NULL, NULL);
}

static GtkWidget *build_patterns(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12); gtk_widget_set_margin_bottom(box, 12);

    /* pattern selector */
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Pattern:"), FALSE, FALSE, 0);
    pat_combo = gtk_combo_box_text_new();
    const char *pats[] = {"off","solid","blink","strobe","pulse","heartbeat","chase","cylon","wave","sos","rainbow","notification"};
    for (int i = 0; i < 12; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(pat_combo), pats[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(pat_combo), 1);
    gtk_box_pack_start(GTK_BOX(row), pat_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    /* led selection */
    GtkWidget *ledbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ledbox), gtk_label_new("LEDs:"), FALSE, FALSE, 0);
    for (int i = 0; i < nleds; i++) {
        led_check[i] = gtk_check_button_new_with_label(led_display_name(i));
        /* default active for thinkvantage/power/kbd */
        int def = (strcmp(leds[i].friendly, "thinkvantage") == 0 ||
                   strcmp(leds[i].friendly, "power") == 0 ||
                   strcmp(leds[i].friendly, "kbd_backlight") == 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(led_check[i]), def);
        gtk_box_pack_start(GTK_BOX(ledbox), led_check[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), ledbox, FALSE, FALSE, 0);

    /* params grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    const char *keys[8] = {"speed","freq","step","count","interval","level","duty","unit"};
    double defaults[8] = {1.0,2.0,0.15,3,0.15,1.0,0.5,0.2};
    double los[8] = {0.1,0.1,0.05,1,0.05,0.0,0.1,0.05};
    double his[8] = {5.0,20.0,1.0,20,1.0,1.0,0.9,1.0};
    for (int p = 0; p < 8; p++) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(keys[p]), 0, p, 1, 1);
        GtkAdjustment *adj;
        if (p == 3) adj = gtk_adjustment_new(defaults[p], los[p], his[p], 1, 1, 0);
        else adj = gtk_adjustment_new(defaults[p], los[p], his[p], 0.05, 0.1, 0);
        GtkWidget *sp = gtk_spin_button_new(adj, 1, (p==3)?0:2);
        param_spin[p] = sp;
        gtk_grid_attach(GTK_GRID(grid), sp, 1, p, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    /* buttons */
    GtkWidget *btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *start = gtk_button_new_with_label("Start");
    g_signal_connect(start, "clicked", G_CALLBACK(start_pattern_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), start, FALSE, FALSE, 0);
    /* connect poll after start? We'll add timeout in start callback via g_timeout_add */
    GtkWidget *stop = gtk_button_new_with_label("Stop");
    g_signal_connect(stop, "clicked", G_CALLBACK(stop_pattern_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), stop, FALSE, FALSE, 0);
    GtkWidget *fast = gtk_button_new_with_label("Fast Flash");
    g_signal_connect(fast, "clicked", G_CALLBACK(fast_flash_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), fast, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btnbox, FALSE, FALSE, 0);

    pat_status = gtk_label_new("Idle");
    gtk_box_pack_start(GTK_BOX(box), pat_status, FALSE, FALSE, 0);

    return box;
}

/* In start_pattern_cb we need to add poll timeout. Modify by calling g_timeout_add there. But start_pattern_cb defined above before poll_engine? poll_engine defined after. Forward declare. */
static gboolean poll_engine(gpointer data);

/* We'll patch start_pattern_cb: after pattern_start, add timeout. Since function already written, we need to edit. Instead, redefine start_pattern_cb here? It's already defined. Let's just add a note: we'll modify via sed? Better: edit file. */

/* notify tab */
static void do_flash_gui(void) {
    /* get selected led */
    int indices[64]; int ni = 0;
    for (int i = 0; i < nleds; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(notify_checks[i]))) indices[ni++] = i;
    }
    if (ni == 0 && nleds > 0) { indices[0] = 0; ni = 1; }
    int count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(notify_count));
    double interval = gtk_spin_button_get_value(GTK_SPIN_BUTTON(notify_interval));
    flash_leds(indices, ni, count, interval);
}

static void test_flash_cb(GtkWidget *w, gpointer d) { do_flash_gui(); }

static gboolean gui_daemon_check_stop(gpointer data) {
    if (daemon_stop) {
        g_main_loop_quit((GMainLoop *)data);
        return FALSE;
    }
    return TRUE;
}

static void gui_daemon_notify_cb(GDBusConnection *c, const gchar *sender_name, const gchar *object_path,
                                 const gchar *interface_name, const gchar *signal_name,
                                 GVariant *parameters, gpointer user_data) {
    (void)c;(void)sender_name;(void)object_path;(void)interface_name;(void)signal_name;(void)parameters;(void)user_data;
    flash_leds(daemon_indices, daemon_nleds, daemon_count, daemon_interval);
}

static void *gui_daemon_thread(void *arg) {
    (void)arg;
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) return NULL;
    g_dbus_connection_signal_subscribe(bus,
        "org.freedesktop.Notifications",
        "org.freedesktop.Notifications",
        "Notify",
        "/org/freedesktop/Notifications",
        NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        gui_daemon_notify_cb, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(200, (GSourceFunc)gui_daemon_check_stop, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    daemon_thread_active = 0;
    return NULL;
}

static void toggle_daemon_cb(GtkWidget *w, gpointer d) {
    if (daemon_running) {
        daemon_stop = 1;
        if (daemon_thread_active) {
            pthread_join(daemon_thread, NULL);
            daemon_thread_active = 0;
        }
        daemon_running = 0;
        gtk_button_set_label(GTK_BUTTON(daemon_btn), "Start notification daemon");
        gtk_label_set_text(GTK_LABEL(daemon_status_label), "Daemon: stopped");
        return;
    }
    daemon_nleds = 0;
    for (int i = 0; i < nleds; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(notify_checks[i]))) daemon_indices[daemon_nleds++] = i;
    }
    if (daemon_nleds == 0 && nleds > 0) { daemon_indices[0] = 0; daemon_nleds = 1; }
    daemon_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(notify_count));
    daemon_interval = gtk_spin_button_get_value(GTK_SPIN_BUTTON(notify_interval));
    daemon_stop = 0;
    daemon_running = 1;
    daemon_thread_active = 1;
    gtk_button_set_label(GTK_BUTTON(daemon_btn), "Stop notification daemon");
    gtk_label_set_text(GTK_LABEL(daemon_status_label), "Daemon: running");

    /* start thread */
    pthread_create(&daemon_thread, NULL, gui_daemon_thread, NULL);
}

static GtkWidget *build_notify(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12); gtk_widget_set_margin_bottom(box, 12);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Flash a ThinkPad LED when a desktop notification arrives."), FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("LEDs:"), FALSE, FALSE, 0);
    for (int i = 0; i < nleds; i++) {
        notify_checks[i] = gtk_check_button_new_with_label(led_display_name(i));
        int def = (strcmp(leds[i].friendly, "thinkvantage") == 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notify_checks[i]), def);
        gtk_box_pack_start(GTK_BOX(row), notify_checks[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Count:"), FALSE, FALSE, 0);
    notify_count = gtk_spin_button_new_with_range(1, 20, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(notify_count), 4);
    gtk_box_pack_start(GTK_BOX(row2), notify_count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Interval:"), FALSE, FALSE, 0);
    notify_interval = gtk_spin_button_new_with_range(0.05, 1.0, 0.05);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(notify_interval), 0.1);
    gtk_box_pack_start(GTK_BOX(row2), notify_interval, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row2, FALSE, FALSE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *test = gtk_button_new_with_label("Test flash");
    g_signal_connect(test, "clicked", G_CALLBACK(test_flash_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btns), test, FALSE, FALSE, 0);
    daemon_btn = gtk_button_new_with_label("Start notification daemon");
    g_signal_connect(daemon_btn, "clicked", G_CALLBACK(toggle_daemon_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btns), daemon_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);

    daemon_status_label = gtk_label_new("Daemon: stopped");
    gtk_box_pack_start(GTK_BOX(box), daemon_status_label, FALSE, FALSE, 0);

    return box;
}

/* settings */
static void fix_perms_cb(GtkWidget *w, gpointer d) {
    int ok = ensure_permissions();
    char msg[128];
    snprintf(msg, sizeof(msg), "Permissions: %s", ok ? "OK" : "FAILED");
    status_msg(msg);
}

static void fnlock_toggle_cb(GtkWidget *w, gpointer d) {
    const char *cur = fnlock_status();
    const char *next = (strcmp(cur, "Enable") == 0) ? "Disable" : "Enable";
    int rc = fnlock_set(next);
    char msg[128];
    snprintf(msg, sizeof(msg), "FnLock -> %s%s", next, rc == 0 ? "" : " (failed)");
    status_msg(msg);
}

static GtkWidget *build_settings(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12); gtk_widget_set_margin_bottom(box, 12);

    GtkWidget *fix = gtk_button_new_with_label("Fix permissions (make LEDs writable)");
    g_signal_connect(fix, "clicked", G_CALLBACK(fix_perms_cb), NULL);
    gtk_box_pack_start(GTK_BOX(box), fix, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Blinkpad — native C/GTK3."), FALSE, FALSE, 0);

    /* FnLock */
    if (fnlock_available()) {
        GtkWidget *fnbtn = gtk_button_new_with_label("Toggle Fn Lock");
        g_signal_connect(fnbtn, "clicked", G_CALLBACK(fnlock_toggle_cb), NULL);
        gtk_box_pack_start(GTK_BOX(box), fnbtn, FALSE, FALSE, 0);
        GtkWidget *fnstatus = gtk_label_new(NULL);
        char buf[128];
        snprintf(buf, sizeof(buf), "FnLock: %s (may need reboot)", fnlock_status());
        gtk_label_set_text(GTK_LABEL(fnstatus), buf);
        gtk_box_pack_start(GTK_BOX(box), fnstatus, FALSE, FALSE, 0);
    }
    return box;
}

static void on_destroy(GtkWidget *w, gpointer d) {
    if (current_engine) {
        pattern_stop(current_engine, 1);
        free(current_engine->indices);
        free(current_engine);
        current_engine = NULL;
    }
    daemon_stop = 1;
    if (daemon_thread_active) {
        pthread_join(daemon_thread, NULL);
        daemon_thread_active = 0;
    }
    gtk_main_quit();
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    discover_leds();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Blinkpad");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 520);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* header? just title already */

    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_control(), gtk_label_new("Control"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_patterns(), gtk_label_new("Patterns"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_notify(), gtk_label_new("Notifications"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_settings(), gtk_label_new("Settings"));

    statusbar = gtk_statusbar_new();
    status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "main");
    gtk_box_pack_start(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
