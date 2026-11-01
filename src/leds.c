#include "common.h"
#include <ctype.h>

Led leds[MAX_LEDS];
int nleds = 0;

static const char *FNLOCK_PATH = "/sys/devices/virtual/firmware-attributes/thinklmi/attributes/FnKeyAsPrimary/current_value";

int fnlock_available(void) {
    // check existence (as root via pkexec? just stat)
    struct stat st;
    if (stat(FNLOCK_PATH, &st) == 0) return 1;
    return 0;
}

static char fnlock_buf[64];
const char *fnlock_status(void) {
    fnlock_buf[0] = '\0';
    char cmd[256];
    if (geteuid() == 0) {
        snprintf(cmd, sizeof(cmd), "cat %s", FNLOCK_PATH);
    } else {
        snprintf(cmd, sizeof(cmd), "pkexec cat %s", FNLOCK_PATH);
    }
    FILE *p = popen(cmd, "r");
    if (!p) return fnlock_buf;
    if (fgets(fnlock_buf, sizeof(fnlock_buf), p)) {
        fnlock_buf[strcspn(fnlock_buf, "\n")] = '\0';
    }
    pclose(p);
    return fnlock_buf;
}

int fnlock_set(const char *value) {
    if (strcmp(value, "Enable") != 0 && strcmp(value, "Disable") != 0) return -1;
    char cmd[512];
    if (geteuid() == 0) {
        snprintf(cmd, sizeof(cmd), "sh -c 'echo %s > %s'", value, FNLOCK_PATH);
    } else {
        snprintf(cmd, sizeof(cmd), "pkexec sh -c 'echo %s > %s'", value, FNLOCK_PATH);
    }
    int rc = system(cmd);
    return (rc == 0) ? 0 : -1;
}

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

int led_get(int idx) {
    if (idx < 0 || idx >= nleds) return 0;
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
    /* check writable */
    int ok = 1;
    for (int i = 0; i < nleds; i++) {
        if (access(led_path(leds[i].kernel, "brightness"), W_OK) != 0) { ok = 0; break; }
    }
    if (ok) return 1;
    /* try pkexec chmod 0666 on all */
    for (int i = 0; i < nleds; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "pkexec chmod 0666 %s %s 2>/dev/null",
                 led_path(leds[i].kernel, "brightness"),
                 led_path(leds[i].kernel, "trigger"));
        system(cmd);
    }
    ok = 1;
    for (int i = 0; i < nleds; i++) {
        if (access(led_path(leds[i].kernel, "brightness"), W_OK) != 0) { ok = 0; break; }
    }
    return ok;
}
