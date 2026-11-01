#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <glib.h>

#define MAX_LEDS 64
#define NAME_LEN 64
#define KERN_LEN 128
#define PATH_LEN 256

typedef struct {
    char friendly[NAME_LEN];
    char kernel[KERN_LEN];
} Led;

extern Led leds[MAX_LEDS];
extern int nleds;

void discover_leds(void);
const char *led_path(const char *kernel, const char *attr);
int led_max(int idx);
int led_get(int idx);
int led_set(int idx, int val);
char *led_trigger(int idx);
void led_set_trigger(int idx, const char *t);
char **led_triggers(int idx, int *count);
void all_off(int *indices, int n);
void snapshot_leds(int *indices, int n, int *out);
void restore_leds(int *indices, int n, int *vals);

int ensure_permissions(void);
int find_led_by_name(const char *name);
const char *led_display_name(int idx);

extern int daemon_indices[64];
extern int daemon_nleds;
extern int daemon_count;
extern double daemon_interval;
extern volatile int daemon_stop;

/* Pattern engine */
typedef struct {
    int *indices;
    int n;
    char pattern[32];
    double speed, freq, step, level, duty, unit, interval, cycle;
    int count;
    int *saved;
    pthread_t thread;
    volatile int running;
    volatile int stop;
} PatternEngine;

void pattern_init(PatternEngine *e, int *indices, int n, const char *pattern);
void pattern_set_param(PatternEngine *e, const char *key, double val);
void pattern_start(PatternEngine *e, int save);
void pattern_stop(PatternEngine *e, int restore);
int pattern_running(PatternEngine *e);

void flash_leds(int *indices, int n, int count, double interval);

#endif