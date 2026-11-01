#include "common.h"
#include <glib.h>
#include <gio/gio.h>
#include <signal.h>
#include <math.h>

static void sigint_handler(int s) { (void)s; daemon_stop = 1; }

static gboolean daemon_check_stop(gpointer data) {
    if (daemon_stop) {
        g_main_loop_quit((GMainLoop *)data);
        return FALSE;
    }
    return TRUE;
}

static void daemon_notify_cb(GDBusConnection *c, const gchar *sender_name, const gchar *object_path,
                             const gchar *interface_name, const gchar *signal_name,
                             GVariant *parameters, gpointer user_data) {
    (void)c;(void)sender_name;(void)object_path;(void)interface_name;(void)signal_name;(void)parameters;(void)user_data;
    flash_leds(daemon_indices, daemon_nleds, daemon_count, daemon_interval);
}

static void cmd_list(void) {
    for (int i = 0; i < nleds; i++) {
        printf("%-16s (%s) brightness=%d/%d trigger=%s\n", leds[i].friendly, led_display_name(i), led_get(i), led_max(i), led_trigger(i));
    }
}

static void cmd_set(const char *name, const char *val) {
    int idx = find_led_by_name(name);
    if (idx < 0) { fprintf(stderr, "unknown led %s\n", name); exit(1); }
    int v;
    if (strcmp(val, "max") == 0) v = led_max(idx);
    else v = atoi(val);
    led_set(idx, v);
    printf("%s -> %d\n", name, led_get(idx));
}

static void cmd_get(const char *name) {
    int idx = find_led_by_name(name);
    if (idx < 0) { fprintf(stderr, "unknown led\n"); exit(1); }
    printf("%d\n", led_get(idx));
}

static void cmd_trigger(const char *name, const char *trig) {
    int idx = find_led_by_name(name);
    if (idx < 0) { fprintf(stderr, "unknown led\n"); exit(1); }
    if (!trig) {
        printf("%s\n", led_trigger(idx));
    } else {
        led_set_trigger(idx, trig);
        printf("%s trigger -> %s\n", name, trig);
    }
}

int main(int argc, char **argv) {
    discover_leds();
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Blinkpad 1.1.0\n");
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: blinkpad <command>\n");
        return 1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "list") == 0) {
        cmd_list();
    } else if (strcmp(cmd, "set") == 0) {
        if (argc < 4) { fprintf(stderr, "set <led> <value>\n"); return 1; }
        cmd_set(argv[2], argv[3]);
    } else if (strcmp(cmd, "get") == 0) {
        if (argc < 3) { fprintf(stderr, "get <led>\n"); return 1; }
        cmd_get(argv[2]);
    } else if (strcmp(cmd, "trigger") == 0) {
        if (argc < 3) { fprintf(stderr, "trigger <led> [trigger]\n"); return 1; }
        cmd_trigger(argv[2], argc >= 4 ? argv[3] : NULL);
    } else if (strcmp(cmd, "ensure-permissions") == 0) {
        int ok = ensure_permissions();
        printf("writable: %s\n", ok ? "yes" : "no");
        return ok ? 0 : 1;
    } else if (strcmp(cmd, "off") == 0) {
        if (argc >= 3 && strcmp(argv[2], "--leds") == 0 && argc >= 4) {
            char buf[256]; strcpy(buf, argv[3]);
            int indices[64]; int ni = 0;
            char *tok = strtok(buf, ",");
            while (tok) {
                int idx = find_led_by_name(tok);
                if (idx >= 0) indices[ni++] = idx;
                tok = strtok(NULL, ",");
            }
            all_off(indices, ni);
        } else {
            int indices[MAX_LEDS]; int ni = 0;
            for (int i = 0; i < nleds; i++) {
                if (strcmp(leds[i].friendly, "mute") == 0 || strcmp(leds[i].friendly, "micmute") == 0) continue;
                indices[ni++] = i;
            }
            all_off(indices, ni);
        }
        printf("off\n");
    } else if (strcmp(cmd, "flash") == 0) {
        char leds_str[256] = "";
        int count = 4;
        double interval = 0.1;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--leds") == 0 && i + 1 < argc) { strcpy(leds_str, argv[++i]); }
            else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = atoi(argv[++i]); }
            else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) { interval = atof(argv[++i]); }
        }
        if (strlen(leds_str) == 0) {
            int ti = -1;
            for (int i = 0; i < nleds; i++) if (strcmp(leds[i].friendly, "thinkvantage") == 0) ti = i;
            if (ti < 0 && nleds > 0) ti = 0;
            if (ti >= 0) strcpy(leds_str, leds[ti].friendly);
        }
        int indices[64]; int ni = 0;
        char buf[256]; strcpy(buf, leds_str);
        char *tok = strtok(buf, ",");
        while (tok) {
            int idx = find_led_by_name(tok);
            if (idx >= 0) indices[ni++] = idx;
            tok = strtok(NULL, ",");
        }
        if (ni == 0 && nleds > 0) { indices[0] = 0; ni = 1; }
        int saved[ni > 0 ? ni : 1];
        snapshot_leds(indices, ni, saved);
        double total = count * interval * 2;
        struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
        while (1) {
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            double t = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
            if (t >= total) break;
            int phase = (fmod(t, interval * 2) < interval) ? 1 : 0;
            for (int i = 0; i < ni; i++) led_set(indices[i], phase ? led_max(indices[i]) : 0);
            struct timespec ts = {0, 30000000}; nanosleep(&ts, NULL);
        }
        restore_leds(indices, ni, saved);
        printf("flashed\n");
    } else if (strcmp(cmd, "pattern") == 0) {
        if (argc < 3) { fprintf(stderr, "pattern <name> [--leds ...] ...\n"); return 1; }
        const char *pname = argv[2];
        char leds_str[1024] = "";
        char allbuf[1024] = "";
        int first = 1;
        for (int i = 0; i < nleds; i++) {
            if (strcmp(leds[i].friendly, "mute") == 0 || strcmp(leds[i].friendly, "micmute") == 0) continue;
            if (!first) strcat(allbuf, ",");
            strcat(allbuf, leds[i].friendly);
            first = 0;
        }
        strcpy(leds_str, allbuf);
        double speed = 0, freq = 0, step = 0, level = -1, duty = 0, unit = 0, interval = 0, cycle = 0;
        int count = 0;
        int no_restore = 0;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--leds") == 0 && i + 1 < argc) { strcpy(leds_str, argv[++i]); }
            else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) { speed = atof(argv[++i]); }
            else if (strcmp(argv[i], "--freq") == 0 && i + 1 < argc) { freq = atof(argv[++i]); }
            else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) { freq = atof(argv[++i]); }
            else if (strcmp(argv[i], "--step") == 0 && i + 1 < argc) { step = atof(argv[++i]); }
            else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = atoi(argv[++i]); }
            else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) { interval = atof(argv[++i]); }
            else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) { level = atof(argv[++i]); }
            else if (strcmp(argv[i], "--duty") == 0 && i + 1 < argc) { duty = atof(argv[++i]); }
            else if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) { unit = atof(argv[++i]); }
            else if (strcmp(argv[i], "--no-restore") == 0) { no_restore = 1; }
        }
        int indices[64]; int ni = 0;
        char buf[1024]; strcpy(buf, leds_str);
        char *tok = strtok(buf, ",");
        while (tok && ni < 64) {
            int idx = find_led_by_name(tok);
            if (idx >= 0) indices[ni++] = idx;
            tok = strtok(NULL, ",");
        }
        if (ni == 0) { fprintf(stderr, "no leds\n"); return 1; }
        PatternEngine eng;
        pattern_init(&eng, indices, ni, pname);
        if (speed > 0) pattern_set_param(&eng, "speed", speed);
        if (freq > 0) pattern_set_param(&eng, "freq", freq);
        if (step > 0) pattern_set_param(&eng, "step", step);
        if (level >= 0) pattern_set_param(&eng, "level", level);
        if (duty > 0) pattern_set_param(&eng, "duty", duty);
        if (unit > 0) pattern_set_param(&eng, "unit", unit);
        if (interval > 0) pattern_set_param(&eng, "interval", interval);
        if (count > 0) pattern_set_param(&eng, "count", count);
        pattern_start(&eng, 1);
        printf("Running pattern '%s'. Press Ctrl+C to stop.\n", pname);
        fflush(stdout);
        signal(SIGINT, sigint_handler);
        daemon_stop = 0;
        while (pattern_running(&eng) && !daemon_stop) {
            struct timespec ts = {0, 200000000};
            nanosleep(&ts, NULL);
        }
        daemon_stop = 0;
        pattern_stop(&eng, !no_restore);
        printf("stopped\n");
        free(eng.indices);
    } else if (strcmp(cmd, "daemon") == 0) {
        char leds_str[256] = "";
        int count = 4;
        double interval = 0.1;
        /* default thinkvantage */
        for (int i = 0; i < nleds; i++) if (strcmp(leds[i].friendly, "thinkvantage") == 0) strcpy(leds_str, leds[i].friendly);
        if (strlen(leds_str) == 0 && nleds > 0) strcpy(leds_str, leds[0].friendly);
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--led") == 0 && i + 1 < argc) strcpy(leds_str, argv[++i]);
            else if (strcmp(argv[i], "--leds") == 0 && i + 1 < argc) strcpy(leds_str, argv[++i]);
            else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = atoi(argv[++i]);
            else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) interval = atof(argv[++i]);
        }
        /* parse comma list into daemon_indices */
        daemon_nleds = 0;
        char buf[256]; strcpy(buf, leds_str);
        char *tok = strtok(buf, ",");
        while (tok && daemon_nleds < 64) {
            int idx = find_led_by_name(tok);
            if (idx >= 0) daemon_indices[daemon_nleds++] = idx;
            tok = strtok(NULL, ",");
        }
        if (daemon_nleds == 0 && nleds > 0) { daemon_indices[0] = 0; daemon_nleds = 1; }
        daemon_count = count;
        daemon_interval = interval;
        printf("Daemon running: flash %s on notifications. Ctrl+C to stop.\n", leds_str);
        fflush(stdout);
        signal(SIGINT, sigint_handler);
        daemon_stop = 0;

        GError *err = NULL;
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (!bus) { fprintf(stderr, "no session bus: %s\n", err ? err->message : "?"); return 1; }

        g_dbus_connection_signal_subscribe(bus,
            "org.freedesktop.Notifications",
            "org.freedesktop.Notifications",
            "Notify",
            "/org/freedesktop/Notifications",
            NULL, G_DBUS_SIGNAL_FLAGS_NONE,
            daemon_notify_cb, NULL, NULL);

        GMainLoop *loop = g_main_loop_new(NULL, FALSE);
        g_timeout_add(200, (GSourceFunc)daemon_check_stop, loop);
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
        printf("daemon stopped\n");
    } else if (strcmp(cmd, "hide") == 0) {
        if (argc < 3) { fprintf(stderr, "hide <led>\n"); return 1; }
        if (hide_led(argv[2]) == 0) printf("Hidden %s (restart app to apply)\n", argv[2]);
        else printf("Failed\n");
    } else if (strcmp(cmd, "show") == 0) {
        if (argc < 3) { fprintf(stderr, "show <led>\n"); return 1; }
        if (show_led(argv[2]) == 0) printf("Unhidden %s (restart app to apply)\n", argv[2]);
        else printf("Failed\n");
    } else if (strcmp(cmd, "hidden") == 0) {
        // print hidden list
        const char *home = getenv("HOME");
        char path[512];
        snprintf(path, sizeof(path), "%s/.config/blinkpad/hidden", home ? home : "/tmp");
        FILE *f = fopen(path, "r");
        if (!f) { printf("No hidden LEDs.\n"); }
        else {
            char line[128];
            int any = 0;
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\n")] = '\0';
                printf("%s\n", line);
                any = 1;
            }
            if (!any) printf("No hidden LEDs.\n");
            fclose(f);
        }
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        return 1;
    }
    return 0;
}
