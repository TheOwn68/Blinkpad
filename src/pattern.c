#include "common.h"
#include <math.h>

static void *pattern_thread(void *arg) {
    PatternEngine *e = (PatternEngine *)arg;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    double speed = e->speed <= 0 ? 1.0 : e->speed;
    while (!e->stop) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double t = ((now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9) * speed;

        int maxes[e->n];
        for (int i = 0; i < e->n; i++) maxes[i] = led_max(e->indices[i]);

        int vals[e->n];
        for (int i = 0; i < e->n; i++) vals[i] = 0;

        if (strcmp(e->pattern, "off") == 0) {
            for (int i = 0; i < e->n; i++) vals[i] = 0;
        } else if (strcmp(e->pattern, "solid") == 0) {
            for (int i = 0; i < e->n; i++) {
                if (e->level < 0) vals[i] = maxes[i];
                else vals[i] = (int)(maxes[i] * e->level);
            }
        } else if (strcmp(e->pattern, "blink") == 0) {
            double freq = e->freq <= 0 ? 2.0 : e->freq;
            double duty = e->duty <= 0 ? 0.5 : e->duty;
            double phase = fmod(t * freq, 1.0);
            int on = phase < duty;
            for (int i = 0; i < e->n; i++) vals[i] = on ? maxes[i] : 0;
        } else if (strcmp(e->pattern, "strobe") == 0) {
            double freq = e->freq <= 0 ? 10.0 : e->freq;
            double phase = fmod(t * freq, 1.0);
            int on = phase < 0.1;
            for (int i = 0; i < e->n; i++) vals[i] = on ? maxes[i] : 0;
        } else if (strcmp(e->pattern, "pulse") == 0) {
            double freq = e->freq <= 0 ? 0.5 : e->freq;
            for (int i = 0; i < e->n; i++) {
                double v = (sin(2 * M_PI * freq * t) + 1) / 2.0;
                vals[i] = (int)(maxes[i] * v);
            }
        } else if (strcmp(e->pattern, "heartbeat") == 0) {
            double cycle = e->cycle <= 0 ? 1.2 : e->cycle;
            double pos = fmod(t, cycle) / cycle;
            int hb = 0;
            if (pos < 0.12) hb = 1;
            else if (pos > 0.15 && pos < 0.27) hb = 1;
            for (int i = 0; i < e->n; i++) vals[i] = hb ? maxes[i] : 0;
        } else if (strcmp(e->pattern, "chase") == 0) {
            double step = e->step <= 0 ? 0.2 : e->step;
            int idx = (int)(t / step) % (e->n > 0 ? e->n : 1);
            for (int i = 0; i < e->n; i++) vals[i] = (i == idx) ? maxes[i] : 0;
        } else if (strcmp(e->pattern, "cylon") == 0) {
            double step = e->step <= 0 ? 0.15 : e->step;
            int idx = 0;
            if (e->n > 1) {
                int pos = (int)(t / step) % (2 * e->n - 2);
                idx = (pos < e->n) ? pos : (2 * e->n - 2 - pos);
            }
            for (int i = 0; i < e->n; i++) vals[i] = (i == idx) ? maxes[i] : 0;
        } else if (strcmp(e->pattern, "wave") == 0) {
            double freq = e->freq <= 0 ? 0.8 : e->freq;
            for (int i = 0; i < e->n; i++) {
                double phase = 2 * M_PI * (freq * t + (double)i / (e->n > 1 ? e->n : 1));
                double v = (sin(phase) + 1) / 2.0;
                vals[i] = (int)(maxes[i] * v);
            }
        } else if (strcmp(e->pattern, "sos") == 0) {
            double unit = e->unit <= 0 ? 0.2 : e->unit;
            /* build timeline */
            /* dots/dashes */
            typedef struct { double dur; int on; } Seg;
            Seg segs[64]; int nseg = 0;
            for (int r = 0; r < 3; r++) { segs[nseg].dur = unit; segs[nseg].on = 1; nseg++; segs[nseg].dur = unit; segs[nseg].on = 0; nseg++; }
            segs[nseg].dur = unit * 2; segs[nseg].on = 0; nseg++;
            for (int r = 0; r < 3; r++) { segs[nseg].dur = unit * 3; segs[nseg].on = 1; nseg++; segs[nseg].dur = unit; segs[nseg].on = 0; nseg++; }
            segs[nseg].dur = unit * 2; segs[nseg].on = 0; nseg++;
            for (int r = 0; r < 3; r++) { segs[nseg].dur = unit; segs[nseg].on = 1; nseg++; segs[nseg].dur = unit; segs[nseg].on = 0; nseg++; }
            segs[nseg].dur = unit * 4; segs[nseg].on = 0; nseg++;
            double total = 0;
            for (int i = 0; i < nseg; i++) total += segs[i].dur;
            double pos = fmod(t, total);
            double acc = 0;
            int on = 0;
            for (int i = 0; i < nseg; i++) {
                if (pos >= acc && pos < acc + segs[i].dur) { on = segs[i].on; break; }
                acc += segs[i].dur;
            }
            for (int i = 0; i < e->n; i++) vals[i] = (i == 0 && on) ? maxes[i] : 0;
        } else if (strcmp(e->pattern, "notification") == 0) {
            int count = e->count <= 0 ? 3 : e->count;
            double interval = e->interval <= 0 ? 0.15 : e->interval;
            double total = count * interval * 2;
            if (t >= total) { e->stop = 1; }
            else {
                int phase = (fmod(t, interval * 2) < interval) ? 1 : 0;
                for (int i = 0; i < e->n; i++) vals[i] = phase ? maxes[i] : 0;
            }
        } else if (strcmp(e->pattern, "rainbow") == 0) {
            double freq = e->freq <= 0 ? 0.6 : e->freq;
            for (int i = 0; i < e->n; i++) {
                double v = (sin(2 * M_PI * (freq * t + i * 0.3)) + 1) / 2.0;
                vals[i] = (int)(maxes[i] * v);
            }
        }

        for (int i = 0; i < e->n; i++) {
            led_set(e->indices[i], vals[i]);
        }

        /* tick sleep 50ms */
        struct timespec ts = {0, 50000000};
        nanosleep(&ts, NULL);
    }
    e->running = 0;
    return NULL;
}

void pattern_init(PatternEngine *e, int *indices, int n, const char *pattern) {
    memset(e, 0, sizeof(*e));
    e->indices = malloc(sizeof(int) * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) e->indices[i] = indices[i];
    e->n = n;
    strncpy(e->pattern, pattern, sizeof(e->pattern) - 1);
    e->speed = 1.0;
    e->freq = 0;
    e->step = 0;
    e->level = -1;
    e->duty = 0;
    e->unit = 0;
    e->interval = 0;
    e->cycle = 0;
    e->count = 0;
    e->saved = NULL;
    e->running = 0;
    e->stop = 0;
}

void pattern_set_param(PatternEngine *e, const char *key, double val) {
    if (strcmp(key, "speed") == 0) e->speed = val;
    else if (strcmp(key, "freq") == 0 || strcmp(key, "rate") == 0) e->freq = val;
    else if (strcmp(key, "step") == 0) e->step = val;
    else if (strcmp(key, "level") == 0) e->level = val;
    else if (strcmp(key, "duty") == 0) e->duty = val;
    else if (strcmp(key, "unit") == 0) e->unit = val;
    else if (strcmp(key, "interval") == 0) e->interval = val;
    else if (strcmp(key, "cycle") == 0) e->cycle = val;
    else if (strcmp(key, "count") == 0) e->count = (int)val;
}

void pattern_start(PatternEngine *e, int save) {
    if (e->running) return;
    if (save) {
        e->saved = malloc(sizeof(int) * (e->n > 0 ? e->n : 1));
        snapshot_leds(e->indices, e->n, e->saved);
    }
    e->stop = 0;
    e->running = 1;
    pthread_create(&e->thread, NULL, pattern_thread, e);
}

void pattern_stop(PatternEngine *e, int restore) {
    if (!e->running) return;
    e->stop = 1;
    pthread_join(e->thread, NULL);
    e->running = 0;
    if (restore && e->saved) {
        restore_leds(e->indices, e->n, e->saved);
    }
    if (e->saved) { free(e->saved); e->saved = NULL; }
}

int pattern_running(PatternEngine *e) { return e->running; }
/* note: running stays 1 until joined; adjust: */
/* We'll treat running as thread alive; set 0 after join above. */

void flash_leds(int *indices, int n, int count, double interval) {
    int saved[n > 0 ? n : 1];
    snapshot_leds(indices, n, saved);
    double total = count * interval * 2;
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    while (1) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double t = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
        if (t >= total) break;
        int phase = (fmod(t, interval * 2) < interval) ? 1 : 0;
        for (int i = 0; i < n; i++) led_set(indices[i], phase ? led_max(indices[i]) : 0);
        struct timespec ts = {0, 30000000}; nanosleep(&ts, NULL);
    }
    restore_leds(indices, n, saved);
}
