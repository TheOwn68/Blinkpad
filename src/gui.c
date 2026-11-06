#include "common.h"
#include <gtk/gtk.h>
#include <glib.h>
#include <gio/gio.h>
#include <pthread.h>

static GtkWidget *window;
static PatternEngine *current_engine = NULL;
static gboolean poll_engine(gpointer data);
static void save_selections(void);
static void load_selections(void);
static GtkWidget *statusbar;
static guint status_ctx;

/* control page widgets */
typedef struct {
    int idx;
    GtkWidget *scale;
    GtkWidget *sw;
    GtkWidget *combo;
    GtkWidget *value;
} LedRow;
static LedRow led_rows[MAX_LEDS];
static int nled_rows = 0;

/* pattern page */
static GtkWidget *pat_combo;
static GtkWidget *led_check[MAX_LEDS];
static GtkWidget *param_spin[8];
static const char *param_keys[8] = {"speed","freq","step","count","interval","level","duty","unit"};
static GtkWidget *pat_status;

/* notify page */
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

static void add_class(GtkWidget *w, const char *name) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), name);
}

/* ---------- shared UI helpers ---------- */

static GtkWidget *page_new(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_margin_start(box, 22);
    gtk_widget_set_margin_end(box, 22);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 22);
    return box;
}

static GtkWidget *page_scroll(GtkWidget *child) {
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

static GtkWidget *title_block(const char *title, const char *subtitle) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *t = gtk_label_new(title);
    add_class(t, "page-title");
    gtk_widget_set_halign(t, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 0);
    if (subtitle) {
        GtkWidget *s = gtk_label_new(subtitle);
        add_class(s, "page-subtitle");
        gtk_widget_set_halign(s, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
        gtk_label_set_xalign(GTK_LABEL(s), 0.0f);
        gtk_box_pack_start(GTK_BOX(box), s, FALSE, FALSE, 0);
    }
    return box;
}

static GtkWidget *section_label(const char *text) {
    GtkWidget *l = gtk_label_new(text);
    add_class(l, "section-title");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

static GtkWidget *card_new(void) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(card, "card");
    return card;
}

/* Icon that best matches a given LED. */
static GtkWidget *led_icon(int idx) {
    const char *name = "input-keyboard-symbolic";
    const char *f = leds[idx].friendly;
    if (strcmp(f, "lid_logo") == 0)         name = "preferences-desktop-display-symbolic";
    else if (strcmp(f, "power") == 0)       name = "battery-good-charging-symbolic";
    else if (strcmp(f, "standby") == 0)     name = "weather-clear-night-symbolic";
    else if (strcmp(f, "thinkvantage") == 0) name = "dialog-information-symbolic";
    else if (strcmp(f, "micmute") == 0)     name = "microphone-sensitivity-muted-symbolic";
    else if (strcmp(f, "mute") == 0)        name = "audio-volume-muted-symbolic";

    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if (!gtk_icon_theme_has_icon(theme, name))
        name = "input-keyboard-symbolic";

    GtkWidget *img = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_MENU);
    gtk_widget_set_opacity(img, 0.85);
    gtk_widget_set_valign(img, GTK_ALIGN_CENTER);
    return img;
}

/* Pill-shaped toggle button with an icon and a label. */
static GtkWidget *led_chip(int idx) {
    GtkWidget *btn = gtk_toggle_button_new();
    add_class(btn, "chip");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), led_icon(idx), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(led_display_name(idx)), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btn), box);
    return btn;
}

static GtkWidget *led_flow(GtkWidget **checks, int default_mode) {
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
    for (int i = 0; i < nleds; i++) {
        checks[i] = led_chip(i);
        int def = 0;
        if (default_mode == 1) {
            def = (strcmp(leds[i].friendly, "thinkvantage") == 0);
        } else if (default_mode == 2) {
            def = (strcmp(leds[i].friendly, "thinkvantage") == 0 ||
                   strcmp(leds[i].friendly, "power") == 0 ||
                   strcmp(leds[i].friendly, "kbd_backlight") == 0);
        }
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[i]), def);
        gtk_flow_box_insert(GTK_FLOW_BOX(flow), checks[i], -1);
    }
    return flow;
}

/* ---------- control page ---------- */

static int is_mute_led(int idx) {
    return (strcmp(leds[idx].friendly, "mute") == 0 || strcmp(leds[idx].friendly, "micmute") == 0);
}

static void all_off_cb(GtkWidget *w, gpointer d) {
    int indices[MAX_LEDS]; int ni = 0;
    for (int i = 0; i < nleds; i++) {
        if (is_mute_led(i)) continue;
        indices[ni++] = i;
    }
    all_off(indices, ni);
    for (int i = 0; i < nled_rows; i++) {
        if (is_mute_led(led_rows[i].idx)) continue;
        gtk_range_set_value(GTK_RANGE(led_rows[i].scale), 0);
        gtk_switch_set_active(GTK_SWITCH(led_rows[i].sw), FALSE);
    }
    status_msg("All lights off (mute LEDs preserved)");
}

static void all_max_cb(GtkWidget *w, gpointer d) {
    for (int i = 0; i < nleds; i++) {
        if (is_mute_led(i)) continue;
        led_set(i, led_max(i));
    }
    for (int i = 0; i < nled_rows; i++) {
        if (is_mute_led(led_rows[i].idx)) continue;
        gtk_range_set_value(GTK_RANGE(led_rows[i].scale), led_max(led_rows[i].idx));
        gtk_switch_set_active(GTK_SWITCH(led_rows[i].sw), TRUE);
    }
    status_msg("All lights at maximum (mute LEDs preserved)");
}

static void on_scale_changed(GtkRange *r, gpointer data) {
    LedRow *row = (LedRow *)data;
    int val = (int)gtk_range_get_value(r);
    led_set(row->idx, val);
    if (row->value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        gtk_label_set_text(GTK_LABEL(row->value), buf);
    }
}

static gboolean on_switch_state_set(GtkSwitch *sw, gboolean state, gpointer data) {
    LedRow *row = (LedRow *)data;
    if (state) {
        int v = (int)gtk_range_get_value(GTK_RANGE(row->scale));
        if (v == 0) { v = led_max(row->idx); gtk_range_set_value(GTK_RANGE(row->scale), v); }
        led_set(row->idx, v);
    } else {
        led_set(row->idx, 0);
    }
    return FALSE;
}

static void on_trigger_changed(GtkComboBox *combo, gpointer data) {
    LedRow *row = (LedRow *)data;
    gchar *txt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (txt) { led_set_trigger(row->idx, txt); g_free(txt); }
}

static GtkWidget *build_led_row(int i) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 9);
    gtk_widget_set_margin_bottom(grid, 9);

    gtk_grid_attach(GTK_GRID(grid), led_icon(i), 0, 0, 1, 1);

    GtkWidget *lbl = gtk_label_new(led_display_name(i));
    add_class(lbl, "led-name");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(lbl, 165, -1);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_grid_attach(GTK_GRID(grid), lbl, 1, 0, 1, 1);

    GtkAdjustment *adj = gtk_adjustment_new(led_get(i), 0, led_max(i), 1, 1, 0);
    GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_digits(GTK_SCALE(scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), scale, 2, 0, 1, 1);

    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%d", led_get(i));
    GtkWidget *val = gtk_label_new(vbuf);
    add_class(val, "dim");
    gtk_widget_set_size_request(val, 22, -1);
    gtk_widget_set_halign(val, GTK_ALIGN_END);
    gtk_widget_set_valign(val, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), val, 3, 0, 1, 1);

    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), led_get(i) > 0);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), sw, 4, 0, 1, 1);

    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_widget_set_size_request(combo, 150, -1);
    gtk_widget_set_hexpand(combo, FALSE);
    gtk_widget_set_valign(combo, GTK_ALIGN_CENTER);
    int cnt = 0;
    char **trigs = led_triggers(i, &cnt);
    for (int t = 0; t < cnt; t++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), trigs[t]);
    char *cur = led_trigger(i);
    for (int t = 0; t < cnt; t++) {
        if (strcmp(trigs[t], cur) == 0) { gtk_combo_box_set_active(GTK_COMBO_BOX(combo), t); break; }
    }
    gtk_grid_attach(GTK_GRID(grid), combo, 5, 0, 1, 1);

    LedRow *row = &led_rows[nled_rows++];
    row->idx = i;
    row->scale = scale;
    row->sw = sw;
    row->combo = combo;
    row->value = val;

    g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), row);
    g_signal_connect(sw, "state-set", G_CALLBACK(on_switch_state_set), row);
    g_signal_connect(combo, "changed", G_CALLBACK(on_trigger_changed), row);
    return grid;
}

static GtkWidget *build_control(void) {
    GtkWidget *page = page_new();

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(head), title_block("Lights",
        "Adjust each ThinkPad LED independently."), TRUE, TRUE, 0);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(actions, "linked");
    GtkWidget *offbtn = gtk_button_new_with_label("All Off");
    g_signal_connect(offbtn, "clicked", G_CALLBACK(all_off_cb), NULL);
    gtk_box_pack_start(GTK_BOX(actions), offbtn, FALSE, FALSE, 0);
    GtkWidget *maxbtn = gtk_button_new_with_label("All Max");
    add_class(maxbtn, "suggested-action");
    g_signal_connect(maxbtn, "clicked", G_CALLBACK(all_max_cb), NULL);
    gtk_box_pack_start(GTK_BOX(actions), maxbtn, FALSE, FALSE, 0);
    gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(head), actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), head, FALSE, FALSE, 0);

    GtkWidget *card = card_new();
    nled_rows = 0;
    for (int i = 0; i < nleds; i++) {
        if (i > 0)
            gtk_box_pack_start(GTK_BOX(card), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card), build_led_row(i), FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(page), card, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new("Tip: the trigger controls kernel-driven blinking. Leave it on “none” for manual control.");
    add_class(hint, "dim");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);

    return page_scroll(page);
}

/* ---------- patterns page ---------- */

static void start_pattern_cb(GtkWidget *w, gpointer d) {
    if (current_engine) {
        pattern_stop(current_engine, 1);
        free(current_engine->indices);
        free(current_engine);
        current_engine = NULL;
    }
    int indices[64]; int ni = 0;
    for (int i = 0; i < nleds; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(led_check[i]))) {
            indices[ni++] = i;
        }
    }
    if (ni == 0 && nleds > 0) { indices[0] = 0; ni = 1; }
    gchar *pname = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(pat_combo));
    if (!pname) pname = g_strdup("solid");

    current_engine = malloc(sizeof(PatternEngine));
    pattern_init(current_engine, indices, ni, pname);
    for (int p = 0; p < 8; p++) {
        double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(param_spin[p]));
        pattern_set_param(current_engine, param_keys[p], v);
    }
    pattern_start(current_engine, 1);
    char msg[256];
    snprintf(msg, sizeof(msg), "Running: %s", pname);
    gtk_label_set_text(GTK_LABEL(pat_status), msg);
    g_free(pname);
    save_selections();
    g_timeout_add(200, poll_engine, NULL);
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

static void select_all_cb(GtkWidget *w, gpointer d) {
    for (int i = 0; i < nleds; i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(led_check[i]), TRUE);
    }
}

static GtkWidget *build_patterns(void) {
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page), title_block("Patterns",
        "Animate the selected LEDs with a pattern."), FALSE, FALSE, 0);

    /* pattern chooser card */
    GtkWidget *patcard = card_new();
    GtkWidget *patrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_margin_start(patrow, 12); gtk_widget_set_margin_end(patrow, 12);
    gtk_widget_set_margin_top(patrow, 12); gtk_widget_set_margin_bottom(patrow, 12);
    gtk_box_pack_start(GTK_BOX(patrow), section_label("PATTERN"), FALSE, FALSE, 0);
    pat_combo = gtk_combo_box_text_new();
    gtk_widget_set_size_request(pat_combo, 220, -1);
    const char *pats[] = {"off","solid","blink","strobe","pulse","heartbeat","chase","cylon","wave","sos","notification"};
    for (int i = 0; i < 11; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(pat_combo), pats[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(pat_combo), 1);
    gtk_box_pack_start(GTK_BOX(patrow), pat_combo, FALSE, FALSE, 0);
    pat_status = gtk_label_new("Idle");
    add_class(pat_status, "status-pill");
    gtk_widget_set_valign(pat_status, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(patrow), pat_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(patcard), patrow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), patcard, FALSE, FALSE, 0);

    /* LED chips card */
    GtkWidget *ledcard = card_new();
    GtkWidget *ledwrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(ledwrap, 12); gtk_widget_set_margin_end(ledwrap, 12);
    gtk_widget_set_margin_top(ledwrap, 12); gtk_widget_set_margin_bottom(ledwrap, 12);
    gtk_box_pack_start(GTK_BOX(ledwrap), section_label("LEDS"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ledwrap), led_flow(led_check, 2), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ledcard), ledwrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ledcard, FALSE, FALSE, 0);

    /* parameters card */
    GtkWidget *paramcard = card_new();
    GtkWidget *paramwrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(paramwrap, 12); gtk_widget_set_margin_end(paramwrap, 12);
    gtk_widget_set_margin_top(paramwrap, 12); gtk_widget_set_margin_bottom(paramwrap, 12);
    gtk_box_pack_start(GTK_BOX(paramwrap), section_label("PARAMETERS"), FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    const char *names[8] = {"Speed","Frequency","Step","Count","Interval","Level","Duty","Unit"};
    double defaults[8] = {1.0,2.0,0.15,3,0.15,1.0,0.5,0.2};
    double los[8] = {0.1,0.1,0.05,1,0.05,0.0,0.1,0.05};
    double his[8] = {5.0,20.0,1.0,20,1.0,1.0,0.9,1.0};
    for (int p = 0; p < 8; p++) {
        int col = (p % 2) * 2;
        int row = p / 2;
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(names[p]), col, row, 1, 1);
        GtkAdjustment *adj;
        if (p == 3) adj = gtk_adjustment_new(defaults[p], los[p], his[p], 1, 1, 0);
        else adj = gtk_adjustment_new(defaults[p], los[p], his[p], 0.05, 0.1, 0);
        GtkWidget *sp = gtk_spin_button_new(adj, 1, (p==3)?0:2);
        gtk_widget_set_size_request(sp, 110, -1);
        param_spin[p] = sp;
        gtk_grid_attach(GTK_GRID(grid), sp, col + 1, row, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(paramwrap), grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(paramcard), paramwrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), paramcard, FALSE, FALSE, 0);

    /* action row */
    GtkWidget *btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *start = gtk_button_new_with_label("Start");
    add_class(start, "suggested-action");
    g_signal_connect(start, "clicked", G_CALLBACK(start_pattern_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), start, FALSE, FALSE, 0);
    GtkWidget *stop = gtk_button_new_with_label("Stop");
    g_signal_connect(stop, "clicked", G_CALLBACK(stop_pattern_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), stop, FALSE, FALSE, 0);
    GtkWidget *allbtn = gtk_button_new_with_label("Select All");
    g_signal_connect(allbtn, "clicked", G_CALLBACK(select_all_cb), NULL);
    gtk_box_pack_start(GTK_BOX(btnbox), allbtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), btnbox, FALSE, FALSE, 0);

    return page_scroll(page);
}

/* ---------- notifications page ---------- */

static void do_flash_gui(void) {
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
        gtk_label_set_text(GTK_LABEL(daemon_status_label), "Stopped");
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
    gtk_label_set_text(GTK_LABEL(daemon_status_label), "Running");

    pthread_create(&daemon_thread, NULL, gui_daemon_thread, NULL);
    save_selections();
}

static GtkWidget *build_notify(void) {
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page), title_block("Notifications",
        "Flash lights whenever a desktop notification arrives."), FALSE, FALSE, 0);

    /* LED chips card */
    GtkWidget *ledcard = card_new();
    GtkWidget *ledwrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(ledwrap, 12); gtk_widget_set_margin_end(ledwrap, 12);
    gtk_widget_set_margin_top(ledwrap, 12); gtk_widget_set_margin_bottom(ledwrap, 12);
    gtk_box_pack_start(GTK_BOX(ledwrap), section_label("LEDS TO FLASH"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ledwrap), led_flow(notify_checks, 1), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ledcard), ledwrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ledcard, FALSE, FALSE, 0);

    /* timing card */
    GtkWidget *timecard = card_new();
    GtkWidget *timewrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(timewrap, 12); gtk_widget_set_margin_end(timewrap, 12);
    gtk_widget_set_margin_top(timewrap, 12); gtk_widget_set_margin_bottom(timewrap, 12);
    gtk_box_pack_start(GTK_BOX(timewrap), section_label("FLASH SETTINGS"), FALSE, FALSE, 0);

    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Count"), FALSE, FALSE, 0);
    notify_count = gtk_spin_button_new_with_range(1, 20, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(notify_count), 4);
    gtk_box_pack_start(GTK_BOX(row2), notify_count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Interval (s)"), FALSE, FALSE, 0);
    notify_interval = gtk_spin_button_new_with_range(0.05, 1.0, 0.05);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(notify_interval), 0.1);
    gtk_box_pack_start(GTK_BOX(row2), notify_interval, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timewrap), row2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timecard), timewrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), timecard, FALSE, FALSE, 0);

    /* actions card */
    GtkWidget *actcard = card_new();
    GtkWidget *actwrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(actwrap, 12); gtk_widget_set_margin_end(actwrap, 12);
    gtk_widget_set_margin_top(actwrap, 12); gtk_widget_set_margin_bottom(actwrap, 12);
    GtkWidget *test = gtk_button_new_with_label("Test flash");
    add_class(test, "suggested-action");
    g_signal_connect(test, "clicked", G_CALLBACK(test_flash_cb), NULL);
    gtk_box_pack_start(GTK_BOX(actwrap), test, FALSE, FALSE, 0);
    daemon_btn = gtk_button_new_with_label("Start notification daemon");
    g_signal_connect(daemon_btn, "clicked", G_CALLBACK(toggle_daemon_cb), NULL);
    gtk_box_pack_start(GTK_BOX(actwrap), daemon_btn, FALSE, FALSE, 0);
    daemon_status_label = gtk_label_new("Stopped");
    add_class(daemon_status_label, "status-pill");
    gtk_widget_set_valign(daemon_status_label, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(actwrap), daemon_status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actcard), actwrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), actcard, FALSE, FALSE, 0);

    return page_scroll(page);
}

/* ---------- settings page ---------- */

static void fix_perms_cb(GtkWidget *w, gpointer d) {
    int ok = ensure_permissions();
    char msg[128];
    snprintf(msg, sizeof(msg), "Permissions: %s", ok ? "OK" : "FAILED");
    status_msg(msg);
}

static GtkWidget *build_settings(void) {
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page), title_block("Settings",
        "Permissions and application information."), FALSE, FALSE, 0);

    GtkWidget *permcard = card_new();
    GtkWidget *permwrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(permwrap, 12); gtk_widget_set_margin_end(permwrap, 12);
    gtk_widget_set_margin_top(permwrap, 12); gtk_widget_set_margin_bottom(permwrap, 12);
    gtk_box_pack_start(GTK_BOX(permwrap), section_label("PERMISSIONS"), FALSE, FALSE, 0);
    GtkWidget *permdesc = gtk_label_new("Blinkpad needs write access to the LED sysfs entries. Run this once if lights cannot be changed.");
    add_class(permdesc, "dim");
    gtk_label_set_line_wrap(GTK_LABEL(permdesc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(permdesc), 0.0f);
    gtk_box_pack_start(GTK_BOX(permwrap), permdesc, FALSE, FALSE, 0);
    GtkWidget *fix = gtk_button_new_with_label("Fix permissions");
    gtk_widget_set_halign(fix, GTK_ALIGN_START);
    g_signal_connect(fix, "clicked", G_CALLBACK(fix_perms_cb), NULL);
    gtk_box_pack_start(GTK_BOX(permwrap), fix, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(permcard), permwrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), permcard, FALSE, FALSE, 0);

    GtkWidget *aboutcard = card_new();
    GtkWidget *aboutwrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(aboutwrap, 12); gtk_widget_set_margin_end(aboutwrap, 12);
    gtk_widget_set_margin_top(aboutwrap, 12); gtk_widget_set_margin_bottom(aboutwrap, 12);
    gtk_box_pack_start(GTK_BOX(aboutwrap), section_label("ABOUT"), FALSE, FALSE, 0);
    GtkWidget *name = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(name), "<b>Blinkpad</b>  ·  1.3.0");
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(aboutwrap), name, FALSE, FALSE, 0);
    GtkWidget *desc = gtk_label_new("Native C / GTK3 control for monochrome ThinkPad LEDs.");
    add_class(desc, "dim");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_box_pack_start(GTK_BOX(aboutwrap), desc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(aboutcard), aboutwrap, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), aboutcard, FALSE, FALSE, 0);

    return page_scroll(page);
}

/* ---------- lifecycle ---------- */

static void on_destroy(GtkWidget *w, gpointer d) {
    if (current_engine) {
        pattern_stop(current_engine, 1);
        free(current_engine->indices);
        free(current_engine);
        current_engine = NULL;
    }
    save_selections();
    daemon_stop = 1;
    if (daemon_thread_active) {
        pthread_join(daemon_thread, NULL);
        daemon_thread_active = 0;
    }
    gtk_main_quit();
}

static void save_selections(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/.config/blinkpad", home);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/ui.conf", dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "pattern_leds=");
    int first = 1;
    for (int i = 0; i < nleds; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(led_check[i]))) {
            if (!first) fputc(',', f);
            fputs(leds[i].friendly, f);
            first = 0;
        }
    }
    fprintf(f, "\n");
    gchar *pname = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(pat_combo));
    fprintf(f, "pattern=%s\n", pname ? pname : "solid");
    g_free(pname);
    fprintf(f, "notify_leds=");
    first = 1;
    for (int i = 0; i < nleds; i++) {
        if (notify_checks[i] && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(notify_checks[i]))) {
            if (!first) fputc(',', f);
            fputs(leds[i].friendly, f);
            first = 0;
        }
    }
    fprintf(f, "\n");
    fclose(f);
}

static void load_selections(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/blinkpad/ui.conf", home);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "pattern_leds=", 13) == 0) {
            char *val = line + 13;
            for (int i = 0; i < nleds; i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(led_check[i]), FALSE);
            char buf[1024]; strcpy(buf, val);
            char *tok = strtok(buf, ",");
            while (tok) {
                int idx = find_led_by_name(tok);
                if (idx >= 0 && idx < nleds) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(led_check[idx]), TRUE);
                tok = strtok(NULL, ",");
            }
        } else if (strncmp(line, "pattern=", 8) == 0) {
            char *val = line + 8;
            const char *pats[] = {"off","solid","blink","strobe","pulse","heartbeat","chase","cylon","wave","sos","notification"};
            for (int i = 0; i < 11; i++) {
                if (strcmp(pats[i], val) == 0) { gtk_combo_box_set_active(GTK_COMBO_BOX(pat_combo), i); break; }
            }
        } else if (strncmp(line, "notify_leds=", 12) == 0) {
            char *val = line + 12;
            for (int i = 0; i < nleds; i++) {
                if (notify_checks[i]) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notify_checks[i]), FALSE);
            }
            char buf[1024]; strcpy(buf, val);
            char *tok = strtok(buf, ",");
            while (tok) {
                int idx = find_led_by_name(tok);
                if (idx >= 0 && idx < nleds && notify_checks[idx]) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notify_checks[idx]), TRUE);
                tok = strtok(NULL, ",");
            }
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    /* Resolve the theme as dark before building any widgets. */
    GtkSettings *settings = gtk_settings_get_default();
    if (settings)
        g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    discover_leds();
    for (int i = 0; i < nleds; i++) {
        if (strcmp(leds[i].friendly, "mute") == 0) {
            if (strcmp(led_trigger(i), "none") == 0) led_set_trigger(i, "audio-mute");
        } else if (strcmp(leds[i].friendly, "micmute") == 0) {
            if (strcmp(led_trigger(i), "none") == 0) led_set_trigger(i, "audio-micmute");
        }
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Blinkpad");
    gtk_window_set_default_size(GTK_WINDOW(window), 940, 680);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    GtkWidget *brand = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(brand), "<b>Blinkpad</b>");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), brand);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

    GdkGeometry hints = {0};
    hints.min_width = 620;
    hints.min_height = 440;
    gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &hints, GDK_HINT_MIN_SIZE);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 160);
    gtk_box_pack_start(GTK_BOX(vbox), stack, TRUE, TRUE, 0);

    gtk_stack_add_titled(GTK_STACK(stack), build_control(), "control", "Lights");
    gtk_stack_add_titled(GTK_STACK(stack), build_patterns(), "patterns", "Patterns");
    gtk_stack_add_titled(GTK_STACK(stack), build_notify(), "notify", "Notifications");
    gtk_stack_add_titled(GTK_STACK(stack), build_settings(), "settings", "Settings");

    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), switcher);

    statusbar = gtk_statusbar_new();
    status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "main");
    gtk_box_pack_start(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

    load_selections();

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "headerbar { min-height: 48px; }"
        "stackswitcher > button { border-radius: 9px; padding: 5px 16px; }"
        "button { border-radius: 9px; padding: 6px 14px; }"
        "entry, spinbutton, combobox button { min-height: 30px; border-radius: 8px; }"
        ".card { background-color: alpha(@theme_fg_color, 0.055);"
        "        border: 1px solid alpha(@theme_fg_color, 0.09);"
        "        border-radius: 14px; padding: 4px; }"
        ".page-title { font-size: 19pt; font-weight: 800; }"
        ".page-subtitle { color: alpha(@theme_fg_color, 0.62); font-size: 10.5pt; }"
        ".section-title { font-size: 9pt; font-weight: 700; color: alpha(@theme_fg_color, 0.66); }"
        ".dim { color: alpha(@theme_fg_color, 0.6); }"
        ".led-name { font-weight: 600; }"
        ".chip { border-radius: 999px; padding: 5px 12px; }"
        ".chip:checked { background-color: alpha(@theme_selected_bg_color, 0.24);"
        "                border-color: alpha(@theme_selected_bg_color, 0.85); }"
        ".status-pill { border-radius: 999px; padding: 3px 12px;"
        "               background-color: alpha(@theme_fg_color, 0.08);"
        "               color: alpha(@theme_fg_color, 0.75); font-size: 9pt; }"
        "scale trough { min-height: 6px; border-radius: 3px; }"
        "scale highlight { border-radius: 3px; }"
        "scale slider { min-width: 16px; min-height: 16px; }"
        "switch { border-radius: 14px; }"
        "separator { background-color: alpha(@theme_fg_color, 0.07); min-height: 1px; }"
        "statusbar { border-top: 1px solid alpha(@theme_fg_color, 0.08); font-size: 9pt; padding: 3px 10px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    gtk_widget_show_all(window);

    const char *start_page = getenv("BLINKPAD_PAGE");
    if (start_page)
        gtk_stack_set_visible_child_name(GTK_STACK(stack), start_page);

    gtk_main();
    return 0;
}
