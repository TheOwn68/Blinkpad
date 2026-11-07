#include "common.h"
#include <ctype.h>
#include <gio/gio.h>

Led leds[MAX_LEDS];
int nleds = 0;

/* daemon globals (defined here, shared) */
int daemon_indices[64];
int daemon_nleds = 0;
int daemon_count = 4;
double daemon_interval = 0.1;
volatile int daemon_stop = 0;

int find_led_by_name(const char *name) {
    for (int i = 0; i < nleds; i++) if (strcmp(leds[i].friendly, name) == 0) return i;
    return -1;
}

const char *led_display_name(int idx) {
    if (idx < 0 || idx >= nleds) return "";
    const char *f = leds[idx].friendly;
    if (strcmp(f, "kbd_backlight") == 0) return "Keyboard Backlight";
    if (strcmp(f, "lid_logo") == 0) return "Lid Logo (red dot)";
    if (strcmp(f, "power") == 0) return "Power / Charging";
    if (strcmp(f, "standby") == 0) return "Standby (moon)";
    if (strcmp(f, "thinkvantage") == 0) return "ThinkVantage";
    if (strcmp(f, "micmute") == 0) return "Microphone Mute";
    if (strcmp(f, "mute") == 0) return "Speaker Mute";
    if (strcmp(f, "capslock") == 0) return "Caps Lock";
    if (strcmp(f, "numlock") == 0) return "Num Lock";
    if (strcmp(f, "scrolllock") == 0) return "Scroll Lock";
    return f;
}

static int relevant(const char *name) {
    char low[256];
    size_t i;
    for (i = 0; name[i] && i < 255; i++) low[i] = (char)tolower((unsigned char)name[i]);
    low[i] = '\0';
    if (strstr(low, "tpacpi::")) return 1;
    if (strstr(low, "thinkpad")) return 1;
    if (strstr(low, "platform::")) {
        if (strstr(low, "kbd") || strstr(low, "backlight") || strstr(low, "mute")) return 1;
    }
    if (strstr(low, "kbd_backlight")) return 1;
    if (strstr(low, "keyboard") && strstr(low, "backlight")) return 1;
    if (strstr(low, "lid_logo")) return 1;
    if (strstr(low, "::power")) return 1;
    if (strstr(low, "standby")) return 1;
    if (strstr(low, "thinkvantage")) return 1;
    if (strstr(low, "backlight")) return 1;
    if (strstr(low, "capslock")) return 1;
    if (strstr(low, "numlock")) return 1;
    if (strstr(low, "scrolllock")) return 1;
    return 0;
}

static void friendly_name(const char *kernel, char *out) {
    char low[256];
    size_t i;
    for (i = 0; kernel[i] && i < 255; i++) low[i] = (char)tolower((unsigned char)kernel[i]);
    low[i] = '\0';
    if (strstr(low, "kbd") && strstr(low, "backlight")) { strcpy(out, "kbd_backlight"); return; }
    if (strstr(low, "lid") && strstr(low, "logo")) { strcpy(out, "lid_logo"); return; }
    if (strstr(low, "::power") || strcmp(low, "power") == 0) { strcpy(out, "power"); return; }
    if (strstr(low, "standby")) { strcpy(out, "standby"); return; }
    if (strstr(low, "thinkvantage")) { strcpy(out, "thinkvantage"); return; }
    if (strstr(low, "capslock")) { strcpy(out, "capslock"); return; }
    if (strstr(low, "numlock")) { strcpy(out, "numlock"); return; }
    if (strstr(low, "scrolllock")) { strcpy(out, "scrolllock"); return; }
    if (strstr(low, "micmute") || strstr(low, "mic_mute")) { strcpy(out, "micmute"); return; }
    if (strstr(low, "mute") && !strstr(low, "mic")) { strcpy(out, "mute"); return; }
    /* sanitize */
    size_t j = 0;
    for (i = 0; kernel[i] && j < NAME_LEN - 1; i++) {
        char c = kernel[i];
        if (c == ':' || c == ' ') c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

static int name_is_hidden(const char *friendly) {
    char product[256] = "";
    const char *files[] = {"/sys/class/dmi/id/product_name", "/sys/class/dmi/id/product_family", "/sys/class/dmi/id/product_version"};
    for (int i = 0; i < 3; i++) {
        FILE *pf = fopen(files[i], "r");
        if (pf) {
            char buf[128];
            if (fgets(buf, sizeof(buf), pf)) {
                strcat(product, buf);
                strcat(product, " ");
            }
            fclose(pf);
        }
    }
    if (strstr(product, "T490") && strcmp(friendly, "thinkvantage") == 0) return 1;

    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/blinkpad/hidden", home);
    FILE *f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\n")] = '\0';
            if (strcmp(line, friendly) == 0) { fclose(f); return 1; }
        }
        fclose(f);
    }
    return 0;
}

int hide_led(const char *friendly) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/.config/blinkpad", home);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/hidden", dir);
    // append if not already
    FILE *f = fopen(path, "r");
    int exists = 0;
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\n")] = '\0';
            if (strcmp(line, friendly) == 0) { exists = 1; break; }
        }
        fclose(f);
    }
    if (exists) return 0;
    f = fopen(path, "a");
    if (!f) return -1;
    fprintf(f, "%s\n", friendly);
    fclose(f);
    return 0;
}

int show_led(const char *friendly) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/blinkpad/hidden", home);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    // read all, rewrite without target
    char lines[64][128]; int n = 0;
    char line[128];
    while (fgets(line, sizeof(line), f) && n < 64) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, friendly) == 0) continue;
        strcpy(lines[n++], line);
    }
    fclose(f);
    f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]);
    fclose(f);
    return 0;
}

void discover_leds(void) {
    nleds = 0;
    DIR *d = opendir("/sys/class/leds");
    if (!d) return;
    struct dirent *ent;
    /* collect names */
    char names[256][KERN_LEN];
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!relevant(ent->d_name)) continue;
        if (count < 256) {
            strncpy(names[count], ent->d_name, KERN_LEN - 1);
            names[count][KERN_LEN - 1] = '\0';
            count++;
        }
    }
    closedir(d);
    /* sort */
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if (strcmp(names[i], names[j]) > 0) {
                char tmp[KERN_LEN]; strcpy(tmp, names[i]); strcpy(names[i], names[j]); strcpy(names[j], tmp);
            }
    for (int i = 0; i < count && nleds < MAX_LEDS; i++) {
        char fn[NAME_LEN];
        friendly_name(names[i], fn);
        /* collision handling */
        int coll = 0;
        for (int k = 0; k < nleds; k++) if (strcmp(leds[k].friendly, fn) == 0) { coll = 1; break; }
        if (coll) {
            /* append number */
            char tmp[NAME_LEN];
            snprintf(tmp, sizeof(tmp), "%s%d", fn, 2);
            strcpy(fn, tmp);
        }
        if (name_is_hidden(fn)) continue;
        strncpy(leds[nleds].friendly, fn, NAME_LEN - 1);
        leds[nleds].friendly[NAME_LEN - 1] = '\0';
        strncpy(leds[nleds].kernel, names[i], KERN_LEN - 1);
        leds[nleds].kernel[KERN_LEN - 1] = '\0';
        nleds++;
    }
}

const char *led_path(const char *kernel, const char *attr) {
    static char buf[PATH_LEN];
    snprintf(buf, sizeof(buf), "/sys/class/leds/%s/%s", kernel, attr);
    return buf;
}

int led_max(int idx) {
    if (idx < 0 || idx >= nleds) return 1;
    FILE *f = fopen(led_path(leds[idx].kernel, "max_brightness"), "r");
    if (!f) return 1;
    int v = 1; fscanf(f, "%d", &v); fclose(f);
    if (v < 1) v = 1;
    return v;
}

static GDBusConnection *upower_sysbus = NULL;

static int upower_kbd_available(void) {
    if (!upower_sysbus) upower_sysbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!upower_sysbus) return 0;
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(upower_sysbus,
        "org.freedesktop.UPower", "/org/freedesktop/UPower/KbdBacklight",
        "org.freedesktop.UPower.KbdBacklight", "GetMaxBrightness",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &err);
    if (!res) { if (err) g_error_free(err); return 0; }
    g_variant_unref(res);
    return 1;
}

static int upower_kbd_get(void) {
    if (!upower_sysbus) upower_sysbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!upower_sysbus) return -1;
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(upower_sysbus,
        "org.freedesktop.UPower", "/org/freedesktop/UPower/KbdBacklight",
        "org.freedesktop.UPower.KbdBacklight", "GetBrightness",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &err);
    if (!res) { if (err) g_error_free(err); return -1; }
    int v = 0;
    g_variant_get(res, "(i)", &v);
    g_variant_unref(res);
    return v;
}

static int upower_kbd_set(int val) {
    if (!upower_sysbus) upower_sysbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!upower_sysbus) return -1;
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(upower_sysbus,
        "org.freedesktop.UPower", "/org/freedesktop/UPower/KbdBacklight",
        "org.freedesktop.UPower.KbdBacklight", "SetBrightness",
        g_variant_new("(i)", val), NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &err);
    if (!res) { if (err) g_error_free(err); return -1; }
    g_variant_unref(res);
    return 0;
}

int led_get(int idx) {
    if (idx < 0 || idx >= nleds) return 0;
    if (strcmp(leds[idx].friendly, "kbd_backlight") == 0 && upower_kbd_available()) {
        int v = upower_kbd_get();
        if (v >= 0) return v;
    }
    FILE *f = fopen(led_path(leds[idx].kernel, "brightness"), "r");
    if (!f) return 0;
    int v = 0; fscanf(f, "%d", &v); fclose(f);
    return v;
}

int led_set(int idx, int val) {
    if (idx < 0 || idx >= nleds) return 0;
    int m = led_max(idx);
    if (val < 0) val = 0;
    if (val > m) val = m;
    if (strcmp(leds[idx].friendly, "kbd_backlight") == 0 && upower_kbd_available()) {
        if (upower_kbd_set(val) == 0) return val;
    }
    FILE *f = fopen(led_path(leds[idx].kernel, "brightness"), "w");
    if (!f) return -1;
    fprintf(f, "%d", val);
    fclose(f);
    return val;
}

char *led_trigger(int idx) {
    static char buf[8192];
    buf[0] = '\0';
    if (idx < 0 || idx >= nleds) return buf;
    FILE *f = fopen(led_path(leds[idx].kernel, "trigger"), "r");
    if (!f) return buf;
    if (fgets(buf, sizeof(buf), f)) {
        /* find bracketed */
        char *s = strchr(buf, '[');
        if (s) {
            s++;
            char *e = strchr(s, ']');
            if (e) *e = '\0';
            /* shift */
            memmove(buf, s, strlen(s) + 1);
        }
    }
    fclose(f);
    /* trim newline */
    buf[strcspn(buf, "\n")] = '\0';
    return buf;
}

void led_set_trigger(int idx, const char *t) {
    if (idx < 0 || idx >= nleds) return;
    FILE *f = fopen(led_path(leds[idx].kernel, "trigger"), "w");
    if (!f) return;
    fputs(t, f);
    fclose(f);
}

char **led_triggers(int idx, int *count) {
    *count = 0;
    if (idx < 0 || idx >= nleds) return NULL;
    FILE *f = fopen(led_path(leds[idx].kernel, "trigger"), "r");
    if (!f) return NULL;
    static char buf[8192];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return NULL; }
    fclose(f);
    /* count tokens */
    int cap = 64;
    char **arr = calloc(cap, sizeof(char *));
    /* remove brackets for parsing: replace [ and ] with spaces */
    for (char *q = buf; *q; q++) if (*q == '[' || *q == ']') *q = ' ';
    char *tok = strtok(buf, " \t\n");
    while (tok) {
        if (*count >= cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char *)); }
        arr[(*count)++] = strdup(tok);
        tok = strtok(NULL, " \t\n");
    }
    return arr;
}

static int find_led(const char *friendly) {
    for (int i = 0; i < nleds; i++)
        if (strcmp(leds[i].friendly, friendly) == 0) return i;
    return -1;
}

void all_off(int *indices, int n) {
    for (int i = 0; i < n; i++) led_set(indices[i], 0);
}

void snapshot_leds(int *indices, int n, int *out) {
    for (int i = 0; i < n; i++) out[i] = led_get(indices[i]);
}

void restore_leds(int *indices, int n, int *vals) {
    for (int i = 0; i < n; i++) led_set(indices[i], vals[i]);
}

int ensure_permissions(void) {
    /* check writable (both brightness and trigger are needed) */
    int ok = 1;
    for (int i = 0; i < nleds; i++) {
        if (access(led_path(leds[i].kernel, "brightness"), W_OK) != 0 ||
            access(led_path(leds[i].kernel, "trigger"), W_OK) != 0) { ok = 0; break; }
    }
    if (ok) return 1;

    /* chmod every LED in one privileged command (one polkit prompt). */
    size_t cap = 64 + (size_t)nleds * 2 * (PATH_LEN * 2 + 16);
    char *cmd = malloc(cap);
    if (!cmd) return 0;
    size_t len = (size_t)snprintf(cmd, cap, "pkexec sh -c 'chmod 0666");
    for (int i = 0; i < nleds && len < cap - 2; i++) {
        len += (size_t)snprintf(cmd + len, cap - len, " '%s' '%s'",
                                led_path(leds[i].kernel, "brightness"),
                                led_path(leds[i].kernel, "trigger"));
    }
    snprintf(cmd + len, cap - len, " 2>/dev/null'");
    system(cmd);
    free(cmd);

    ok = 1;
    for (int i = 0; i < nleds; i++) {
        if (access(led_path(leds[i].kernel, "brightness"), W_OK) != 0 ||
            access(led_path(leds[i].kernel, "trigger"), W_OK) != 0) { ok = 0; break; }
    }
    return ok;
}
