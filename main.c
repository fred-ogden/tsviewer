#define _GNU_SOURCE

#include <gtk/gtk.h>
#include <cairo.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Calendar timestamps are interpreted as timezone-independent civil time.
   Local timezone and daylight-saving rules are intentionally ignored. */

#define TRUE_INT 1
#define FALSE_INT 0
#define DEFAULT_VIEW_DAYS 31.0
#define LEFT_MARGIN 70.0
#define RIGHT_MARGIN 25.0
#define TOP_MARGIN 82.0
#define BOTTOM_MARGIN 55.0
#define MAX_LINE 8192
#define MAX_INPUT_FILES 4
#define BAD_VALUE -9999.0
#define TSVIEWER_VERSION "1.04"

/* ---------------------------------------------------------------------
 * tsviewer -- Interactive Scientific Time-Series Viewer
 * Conceived by Fred L. Ogden to view Campbell Scientific TOA5 files
 * 
 * Copyright [2026] [Fred L. Ogden]
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 * Written by Fred L. Ogden with considerable assistance from ChatGPT.
 * 
 * Philosophy: If the file contains delimited numeric data, use a recognized
 *             time coordinate in column 1 or column 2, or otherwise use a
 *             finite numeric abscissa in column 1, then plot the remaining
 *             numeric columns against that coordinate in an X window using
 *             GTK3 graphical library functions.  A time-like field appearing
 *             after column 2 is not treated as the primary coordinate.
 *             For an unrecognized numeric abscissa, no units, time origin,
 *             sampling interval, or calendar meaning are inferred.
 *
 * Valid generic layouts include:
 *
 *   JD,v1,v2,...,vN
 *   MJD,v1,v2,...,vN
 *   JD,YYYY-MM-DD hh:mm:ss,v1,v2,...,vN
 *   YYYY-MM-DD,v1,v2,...,vN
 *   YYYY-MM-DD hh:mm:ss,v1,v2,...,vN
 *   index,date-time,v1,v2,...,vN
 *   index,v1,v2,...,vN
 *
 * AmeriFlux and AORC files remain supported.  Unix epoch seconds and
 * milliseconds are recognized in column 1 or column 2.  Date/time fields
 * in column 3 or later do not define the plotting coordinate.
 *
 * Design Goals:
 *
 *   - Automatic recognition of common scientific data formats.
 *   - No external dependencies beyond GTK3 and the C standard library.
 *   - Portable ISO C99 implementation.
 *   - Robust handling of imperfect observational data.
 *   - Fast interactive display of large data sets.
 *   - No hidden modification of the input data.
 *   - Sensible defaults with minimal user configuration.
 *
 * Compiled as C99 (-std=c99). Features used below that are NOT available
 * in strict C89 / ANSI C, flagged inline at their first occurrence:
 *
 *   - Declarations after statements, anywhere in a block ("mixed
 *     declarations and code"). C89 required all declarations at the top
 *     of a block, before any executable statement.
 *   - Variables declared inside a `for (...)` initializer, e.g.
 *     `for (int i = 0; ...)`, scoped to the loop.
 *   - `NAN` and `isfinite()` from <math.h> (C89 only guarantees
 *     `HUGE_VAL`; NaN handling and the isfinite()/isnan() family are C99
 *     additions).
 *   - `long long` and the `LL` family of behavior (C89 had no integer
 *     type wider than `long`).
 *   - Slash-slash line comments are also legal under C99, but this file
 *     keeps using slash-star block comments throughout for visual
 *     consistency with the rest of the codebase.
 * 
 *  Development History:
 *  v0.1-0.3: Basic CSV plotting   May, 2026
 *  v0.4: Multiple file formats, statistics, June, 2026
 *  v0.5: Generalized parser, monthly/annual support, June, 2026
 *  v0.6: Y-axis modes, logarithmic scaling, July, 2026
 *  v0.7: Multi-file comparison, NSE/KGE, July, 2026
 *  v0.7.1: Generic numeric abscissa support, July, 2026
 *  v0.7.2: Timezone-independent calendar handling, nonfatal abcissa
 *          reversals, and redraw-performance improvements, July 2026
 *  v0.8: Set-selection controls and paired multi-file selection, July 2026
 *  v0.9: First- or second-column time-coordinate detection, July 2026
 *  v1.0: Explicit reference/model statistical selection and multi-model
 *        NSE/KGE evaluation, August 2026
 *  v1.01: Screen-relative initial window size and stable status-label layout,
 *         August 2026
 *  v1.02: Mouse-wheel zoom centered on cursor, constrained to data extents,
 *         August 2026
 *  v1.03: Constrained series-panel width and automatic paired statistical
 *         role selection, August 2026
 *  v1.04: Pair-set role feedback, log-transformed NSE/KGE, and F1 help,
 *         September 2026
 *
 * --------------------------------------------------------------------- */


typedef enum {
    FILE_FORMAT_UNKNOWN = 0,
    FILE_FORMAT_TOA5,
    FILE_FORMAT_DELIMITED_HEADER,
    FILE_FORMAT_DELIMITED_NO_HEADER
} FileFormat;

typedef enum {
    TIMESTAMP_FORMAT_UNKNOWN = 0,
    TIMESTAMP_FORMAT_TEXT,
    TIMESTAMP_FORMAT_DATE_ONLY,
    TIMESTAMP_FORMAT_COMPACT_YMDHM,
    TIMESTAMP_FORMAT_COMPACT_YMDHMS,
    TIMESTAMP_FORMAT_INTERVAL_END_COMPACT,
    TIMESTAMP_FORMAT_YEAR_ONLY,
    TIMESTAMP_FORMAT_YEAR_MONTH,
    TIMESTAMP_FORMAT_JULIAN_DATE,
    TIMESTAMP_FORMAT_MODIFIED_JULIAN_DATE,
    TIMESTAMP_FORMAT_UNIX_SECONDS,
    TIMESTAMP_FORMAT_UNIX_MILLISECONDS,
    TIMESTAMP_FORMAT_NUMERIC_ABSCISSA
} TimestampFormat;

typedef enum {
    AXIS_TYPE_CALENDAR_TIME = 0,
    AXIS_TYPE_NUMERIC
} AxisType;

typedef enum {
    DELIMITER_COMMA = 0,
    DELIMITER_TAB,
    DELIMITER_PIPE,
    DELIMITER_WHITESPACE
} DelimiterType;

typedef enum {
    Y_RANGE_ALL = 0,
    Y_RANGE_POSITIVE_ONLY,
    Y_RANGE_NEGATIVE_ONLY
} YRangeMode;

typedef struct {
    char *name;
    char *units;
    double *values;
    double *integrated_values;
    int enabled;
    double red;
    double green;
    double blue;
    GtkWidget *check_button;
    GtkWidget *swatch;
    GtkWidget *label;
    GtkWidget *row_event_box;
    GtkWidget *reference_button;
    GtkWidget *model_button;
    int auto_disabled_for_log;
    double *time_values;
    int n_records;
    int source_file_index;
    double gap_threshold_s;
    int gap_threshold_computed;
} TimeSeries;

typedef struct {
    double *time_values;
    int n_records;
    int capacity_records;

    TimeSeries *series;
    int n_series;

    double first_time;
    double last_time;
    double view_start_offset_s;
    double view_duration_s;

    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *scrollbar;
    GtkWidget *status_label;
    GtkWidget *stats_label;
    GtkWidget *series_box;
    GtkWidget *summary_label;
    GtkWidget *y_all_radio;
    GtkWidget *y_positive_radio;
    GtkWidget *y_negative_radio;
    GtkWidget *log_y_check;
    GtkWidget *integrate_check;
    GtkWidget *pair_sets_check;
    GtkWidget *match_variable_colors_check;
    GtkWidget *help_window;
    int updating_controls;

    YRangeMode y_range_mode;
    int log_y_axis;
    int integrate_series;
    int match_variable_colors;
    char status_notice[512];

    int mouse_inside;
    double mouse_x;
    double mouse_y;

    int is_dragging_selection;
    int selection_active;
    double selection_t0;
    double selection_t1;
    int reference_series_index;
    int model_series_index;

    char *filename;
    FileFormat file_format;
    TimestampFormat timestamp_format;
    AxisType axis_type;

    int n_files;
    char *filenames[MAX_INPUT_FILES];
} AppData;

static int visible_index_range(AppData *app, double t0, double t1, int *i0_out, int *i1_out);
static void update_status(AppData *app);
static int load_time_series_file(AppData *app, const char *filename);
static void update_series_labels(AppData *app);
static void update_series_role_controls(AppData *app);
static int find_file1_matching_series(const AppData *app, const TimeSeries *series);
static void reference_button_toggled(GtkToggleButton *button, gpointer user_data);
static void model_button_toggled(GtkToggleButton *button, gpointer user_data);
static void set_view_window(AppData *app, double t_start, double t_end);
static void reset_view_to_full_record(AppData *app);
static void update_summary_label(AppData *app);
static int value_is_visible_for_y_mode(const AppData *app, double value);
static int append_loaded_file(AppData *app, const char *filename, int file_index);
static void apply_file_line_style(cairo_t *cr, int file_index);
static int normalized_names_equal(const char *a, const char *b);
static void checkbox_toggled(GtkToggleButton *button, gpointer user_data);
static int build_integrated_values(AppData *app);
static void free_integrated_values(AppData *app);
static int integrated_values_are_cached(const AppData *app);
static double series_display_value(const AppData *app, const TimeSeries *series, int index);
static void show_help_window(AppData *app);


static void normalize_series_name(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c)) dst[j++] = (char)tolower(c);
    }
    dst[j] = '\0';
}

static int normalized_names_equal(const char *a, const char *b)
{
    char na[512], nb[512];
    normalize_series_name(a, na, sizeof(na));
    normalize_series_name(b, nb, sizeof(nb));
    return strcmp(na, nb) == 0;
}

static void apply_file_line_style(cairo_t *cr, int file_index)
{
    static const double dashed[] = {8.0, 5.0};
    static const double long_dashed[] = {14.0, 6.0};
    static const double dotted[] = {2.0, 4.0};
    switch (file_index) {
        case 1: cairo_set_dash(cr, dashed, 2, 0.0); break;
        case 2: cairo_set_dash(cr, long_dashed, 2, 0.0); break;
        case 3: cairo_set_dash(cr, dotted, 2, 0.0); break;
        default: cairo_set_dash(cr, NULL, 0, 0.0); break;
    }
}

static int series_index_range(const TimeSeries *series, double t0, double t1,
                              int *i0_out, int *i1_out)
{
    int i0 = 0;
    int i1;
    if (!series || !series->time_values || series->n_records <= 0) return FALSE_INT;
    while (i0 < series->n_records && series->time_values[i0] < t0) i0++;
    i1 = i0;
    while (i1 < series->n_records && series->time_values[i1] <= t1) i1++;
    *i0_out = i0;
    *i1_out = i1;
    return i1 > i0;
}

static const double palette[][3] = {
    {0.00, 1.00, 1.00},   /* cyan */
    {1.00, 1.00, 0.00},   /* yellow */
    {0.20, 1.00, 0.20},   /* lime */
    {1.00, 0.55, 0.00},   /* orange */
    {1.00, 0.00, 1.00},   /* magenta */
    {1.00, 0.20, 0.20},   /* red */
    {1.00, 1.00, 1.00},   /* white */
    {0.35, 0.65, 1.00},   /* light blue */
    {0.70, 0.45, 1.00},   /* violet */
    {0.70, 0.70, 0.70},   /* gray */
    {0.95, 0.45, 0.65},   /* rose */
    {0.45, 0.95, 0.65}    /* mint */
};

static void free_csv_fields(char **fields, int n_fields)
{
    if (!fields) return;
    /* C99: `i` is declared inside the for-loop's init-clause, scoped to
       the loop body. C89 required it declared separately beforehand. */
    for (int i = 0; i < n_fields; i++) free(fields[i]);
    free(fields);
}

static char *trim_copy(const char *src)
{
    if (!src) return strdup("");
    const char *a = src;
    while (*a && isspace((unsigned char)*a)) a++;
    const char *b = a + strlen(a);
    while (b > a && isspace((unsigned char)*(b - 1))) b--;
    size_t n = (size_t)(b - a);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, a, n);
    out[n] = '\0';
    return out;
}

static int parse_csv_line(const char *line, char ***fields_out)
{
    int capacity = 16;
    int n_fields = 0;
    char **fields = (char **)calloc((size_t)capacity, sizeof(char *));
    if (!fields) return -1;

    const char *p = line;
    while (*p) {
        if (n_fields >= capacity) {
            capacity *= 2;
            char **tmp = (char **)realloc(fields, (size_t)capacity * sizeof(char *));
            if (!tmp) {
                free_csv_fields(fields, n_fields);
                return -1;
            }
            fields = tmp;
        }

        int in_quote = FALSE_INT;
        size_t cap = 64;
        size_t len = 0;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            free_csv_fields(fields, n_fields);
            return -1;
        }

        if (*p == '"') {
            in_quote = TRUE_INT;
            p++;
        }

        while (*p) {
            if (in_quote) {
                if (*p == '"') {
                    if (*(p + 1) == '"') {
                        if (len + 1 >= cap) {
                            cap *= 2;
                            char *tmp = (char *)realloc(buf, cap);
                            if (!tmp) { free(buf); free_csv_fields(fields, n_fields); return -1; }
                            buf = tmp;
                        }
                        buf[len++] = '"';
                        p += 2;
                    } else {
                        p++;
                        in_quote = FALSE_INT;
                    }
                } else {
                    if (len + 1 >= cap) {
                        cap *= 2;
                        char *tmp = (char *)realloc(buf, cap);
                        if (!tmp) { free(buf); free_csv_fields(fields, n_fields); return -1; }
                        buf = tmp;
                    }
                    buf[len++] = *p++;
                }
            } else {
                if (*p == ',') {
                    p++;
                    break;
                }
                if (*p == '\r' || *p == '\n') {
                    while (*p == '\r' || *p == '\n') p++;
                    break;
                }
                if (len + 1 >= cap) {
                    cap *= 2;
                    char *tmp = (char *)realloc(buf, cap);
                    if (!tmp) { free(buf); free_csv_fields(fields, n_fields); return -1; }
                    buf = tmp;
                }
                buf[len++] = *p++;
            }
        }

        buf[len] = '\0';
        fields[n_fields++] = trim_copy(buf);
        free(buf);

        if (!*p) break;
    }

    *fields_out = fields;
    return n_fields;
}

static int parse_timestamp_text(const char *s, time_t *t_out, TimestampFormat *format_out)
{
    static const char *datetime_formats[] = {
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d %H:%M",
        "%Y/%m/%d %H:%M:%S",
        "%Y/%m/%d %H:%M",
        "%m/%d/%Y %H:%M:%S",
        "%m/%d/%Y %H:%M",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%dT%H:%M"
    };
    static const char *date_only_formats[] = {
        "%Y-%m-%d",
        "%Y/%m/%d",
        "%m/%d/%Y"
    };
    int n_datetime_formats = (int)(sizeof(datetime_formats) / sizeof(datetime_formats[0]));
    int n_date_only_formats = (int)(sizeof(date_only_formats) / sizeof(date_only_formats[0]));
    if (!s || !*s || !t_out) return FALSE_INT;

    for (int i = 0; i < n_datetime_formats; i++) {
        struct tm tm_value;
        memset(&tm_value, 0, sizeof(tm_value));
        tm_value.tm_isdst = -1;
        char *end = strptime(s, datetime_formats[i], &tm_value);
        if (end) {
            while (*end && isspace((unsigned char)*end)) end++;
            if (*end == '\0') {
                time_t t = timegm(&tm_value);
                if (t != (time_t)-1) {
                    *t_out = t;
                    if (format_out) *format_out = TIMESTAMP_FORMAT_TEXT;
                    return TRUE_INT;
                }
            }
        }
    }

    for (int i = 0; i < n_date_only_formats; i++) {
        struct tm tm_value;
        memset(&tm_value, 0, sizeof(tm_value));
        tm_value.tm_hour = 23;
        tm_value.tm_min = 59;
        tm_value.tm_sec = 59;
        tm_value.tm_isdst = -1;
        char *end = strptime(s, date_only_formats[i], &tm_value);
        if (end) {
            while (*end && isspace((unsigned char)*end)) end++;
            if (*end == '\0') {
                time_t t = timegm(&tm_value);
                if (t != (time_t)-1) {
                    *t_out = t;
                    if (format_out) *format_out = TIMESTAMP_FORMAT_DATE_ONLY;
                    return TRUE_INT;
                }
            }
        }
    }

    return FALSE_INT;
}


static int parse_compact_timestamp(const char *s, time_t *t_out, TimestampFormat *format_out)
{
    if (!s || !*s || !t_out) return FALSE_INT;

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    if (len != 12 && len != 14) return FALSE_INT;

    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)s[i])) return FALSE_INT;
    }

    char buf[5];
    struct tm tm_value;
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_isdst = -1;

    memcpy(buf, s, 4);
    buf[4] = '\0';
    int year = atoi(buf);

    memcpy(buf, s + 4, 2);
    buf[2] = '\0';
    int month = atoi(buf);

    memcpy(buf, s + 6, 2);
    buf[2] = '\0';
    int day = atoi(buf);

    memcpy(buf, s + 8, 2);
    buf[2] = '\0';
    int hour = atoi(buf);

    memcpy(buf, s + 10, 2);
    buf[2] = '\0';
    int minute = atoi(buf);

    int second = 0;
    if (len == 14) {
        memcpy(buf, s + 12, 2);
        buf[2] = '\0';
        second = atoi(buf);
    }

    if (year < 1900 || year > 2200) return FALSE_INT;
    if (month < 1 || month > 12) return FALSE_INT;
    if (day < 1 || day > 31) return FALSE_INT;
    if (hour < 0 || hour > 23) return FALSE_INT;
    if (minute < 0 || minute > 59) return FALSE_INT;
    if (second < 0 || second > 59) return FALSE_INT;

    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = second;

    time_t t = timegm(&tm_value);
    if (t == (time_t)-1) return FALSE_INT;

    struct tm *check = gmtime(&t);
    if (!check) return FALSE_INT;
    if (check->tm_year != year - 1900 || check->tm_mon != month - 1 ||
        check->tm_mday != day || check->tm_hour != hour ||
        check->tm_min != minute || check->tm_sec != second) {
        return FALSE_INT;
    }

    *t_out = t;
    if (format_out) {
        *format_out = (len == 12) ? TIMESTAMP_FORMAT_COMPACT_YMDHM : TIMESTAMP_FORMAT_COMPACT_YMDHMS;
    }
    return TRUE_INT;
}

static int parse_numeric_timestamp(const char *s, time_t *t_out, TimestampFormat *format_out)
{
    if (!s || !*s || !t_out) return FALSE_INT;
    errno = 0;
    char *end = NULL;
    double v = strtod(s, &end);
    if (errno || end == s || !isfinite(v)) return FALSE_INT;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return FALSE_INT;

    if (v > 2400000.0 && v < 3000000.0) {
        double unix_s = (v - 2440587.5) * 86400.0;
        *t_out = (time_t)llround(unix_s);
        if (format_out) *format_out = TIMESTAMP_FORMAT_JULIAN_DATE;
        return TRUE_INT;
    }

    if (v > 40000.0 && v < 100000.0) {
        double jd = v + 2400000.5;
        double unix_s = (jd - 2440587.5) * 86400.0;
        *t_out = (time_t)llround(unix_s);
        if (format_out) *format_out = TIMESTAMP_FORMAT_MODIFIED_JULIAN_DATE;
        return TRUE_INT;
    }

    if (v > 1000000000000.0 && v < 4000000000000.0) {
        *t_out = (time_t)llround(v / 1000.0);
        if (format_out) *format_out = TIMESTAMP_FORMAT_UNIX_MILLISECONDS;
        return TRUE_INT;
    }

    if (v > 1000000000.0 && v < 4000000000.0) {
        *t_out = (time_t)llround(v);
        if (format_out) *format_out = TIMESTAMP_FORMAT_UNIX_SECONDS;
        return TRUE_INT;
    }

    if (fabs(v - floor(v + 0.5)) < 1.0e-9 && v >= 1800.0 && v <= 2200.0) {
        int year = (int)floor(v + 0.5);
        struct tm tm_value;
        memset(&tm_value, 0, sizeof(tm_value));
        tm_value.tm_year = year - 1900;
        tm_value.tm_mon = 11;
        tm_value.tm_mday = 31;
        tm_value.tm_hour = 23;
        tm_value.tm_min = 59;
        tm_value.tm_sec = 59;
        tm_value.tm_isdst = -1;
        *t_out = timegm(&tm_value);
        if (*t_out == (time_t)-1) return FALSE_INT;
        if (format_out) *format_out = TIMESTAMP_FORMAT_YEAR_ONLY;
        return TRUE_INT;
    }

    return FALSE_INT;
}

static int parse_timestamp_any(const char *s, time_t *t_out, TimestampFormat *format_out)
{
    TimestampFormat local_format = TIMESTAMP_FORMAT_UNKNOWN;
    if (parse_compact_timestamp(s, t_out, &local_format)) {
        if (format_out) *format_out = local_format;
        return TRUE_INT;
    }
    if (parse_numeric_timestamp(s, t_out, &local_format)) {
        if (format_out) *format_out = local_format;
        return TRUE_INT;
    }
    if (parse_timestamp_text(s, t_out, &local_format)) {
        if (format_out) *format_out = local_format;
        return TRUE_INT;
    }
    if (format_out) *format_out = TIMESTAMP_FORMAT_UNKNOWN;
    return FALSE_INT;
}

static int parse_double_or_nan(const char *s, double *value_out)
{
    if (!s || !*s) {
        *value_out = NAN;
        return FALSE_INT;
    }
    if (g_ascii_strcasecmp(s, "nan") == 0 || strcmp(s, "NAN") == 0) {
        *value_out = NAN;
        return FALSE_INT;
    }
    errno = 0;
    char *end = NULL;
    double v = strtod(s, &end);
    if (errno || end == s) {
        *value_out = NAN;
        return FALSE_INT;
    }
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) {
        *value_out = NAN;
        return FALSE_INT;
    }
    if (v == BAD_VALUE) {
        *value_out = NAN;
        return FALSE_INT;
    }
    *value_out = v;
    return TRUE_INT;
}

static int ensure_record_capacity(AppData *app, int needed)
{
    if (needed <= app->capacity_records) return TRUE_INT;
    /* C99: declaring `new_capacity` here, after the early-return `if`
       above, is "mixed declarations and code" -- illegal in C89, which
       requires every declaration in a block to precede all statements
       in that block. From here on this pattern (declare right where a
       value is first needed, instead of all at the top) is used freely
       throughout the file without further comment. */
    int new_capacity = app->capacity_records > 0 ? app->capacity_records * 2 : 1024;
    while (new_capacity < needed) new_capacity *= 2;

    double *new_times = (double *)realloc(app->time_values, (size_t)new_capacity * sizeof(double));
    if (!new_times) return FALSE_INT;
    app->time_values = new_times;

    for (int i = 0; i < app->n_series; i++) {
        double *new_values = (double *)realloc(app->series[i].values, (size_t)new_capacity * sizeof(double));
        if (!new_values) return FALSE_INT;
        /* C99: NAN is a guaranteed-available macro from <math.h> (when
           __STDC_IEC_559__ floating point is supported, which is true on
           every platform this app targets). C89's <math.h> has no NAN. */
        for (int j = app->capacity_records; j < new_capacity; j++) new_values[j] = NAN;
        app->series[i].values = new_values;
    }

    app->capacity_records = new_capacity;
    return TRUE_INT;
}

static int load_toa5_csv(AppData *app, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Could not open %s\n", filename);
        return FALSE_INT;
    }

    char line[MAX_LINE];
    char **row1 = NULL, **row2 = NULL, **row3 = NULL, **row4 = NULL;
    int n1 = 0, n2 = 0, n3 = 0, n4 = 0;

    if (!fgets(line, sizeof(line), fp)) goto fail;
    n1 = parse_csv_line(line, &row1);
    if (!fgets(line, sizeof(line), fp)) goto fail;
    n2 = parse_csv_line(line, &row2);
    if (!fgets(line, sizeof(line), fp)) goto fail;
    n3 = parse_csv_line(line, &row3);
    if (!fgets(line, sizeof(line), fp)) goto fail;
    n4 = parse_csv_line(line, &row4);

    if (n2 < 3 || strcmp(row2[0], "TIMESTAMP") != 0) {
        fprintf(stderr, "This does not look like a TOA5 file with TIMESTAMP in row 2.\n");
        goto fail;
    }

    app->n_series = n2 - 2;  /* skip TIMESTAMP and RECORD */
    app->series = (TimeSeries *)calloc((size_t)app->n_series, sizeof(TimeSeries));
    if (!app->series) goto fail;

    int palette_count = (int)(sizeof(palette) / sizeof(palette[0]));
    for (int i = 0; i < app->n_series; i++) {
        int col = i + 2;
        app->series[i].name = strdup(row2[col]);
        app->series[i].units = (col < n3) ? strdup(row3[col]) : strdup("");
        app->series[i].enabled = (i < 6) ? TRUE_INT : FALSE_INT;
        app->series[i].auto_disabled_for_log = FALSE_INT;
        app->series[i].red = palette[i % palette_count][0];
        app->series[i].green = palette[i % palette_count][1];
        app->series[i].blue = palette[i % palette_count][2];
        app->series[i].values = NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        char **fields = NULL;
        int nf = parse_csv_line(line, &fields);
        if (nf < 2) {
            free_csv_fields(fields, nf);
            continue;
        }

        time_t t = (time_t)-1;
        if (!parse_timestamp_any(fields[0], &t, NULL)) {
            free_csv_fields(fields, nf);
            continue;
        }

        double x_value = (double)t;
        if (app->n_records > 0) {
            double previous_x = app->time_values[app->n_records - 1];
            if (x_value < previous_x) {
                fprintf(stderr,
                        "Warning: abscissa decreased at record %d: "
                        "%.17g follows %.17g; continuing.\n",
                        app->n_records + 1, x_value, previous_x);
            } else if (x_value == previous_x) {
                fprintf(stderr,
                        "Warning: duplicate abscissa at record %d: "
                        "%.17g; continuing.\n",
                        app->n_records + 1, x_value);
            }
        }

        if (!ensure_record_capacity(app, app->n_records + 1)) {
            free_csv_fields(fields, nf);
            goto fail;
        }

        int r = app->n_records;
        app->time_values[r] = x_value;
        for (int i = 0; i < app->n_series; i++) {
            int col = i + 2;
            double v = NAN;
            if (col < nf) parse_double_or_nan(fields[col], &v);
            app->series[i].values[r] = v;
        }
        app->n_records++;
        free_csv_fields(fields, nf);
    }

    fclose(fp);
    free_csv_fields(row1, n1);
    free_csv_fields(row2, n2);
    free_csv_fields(row3, n3);
    free_csv_fields(row4, n4);

    if (app->n_records <= 0) {
        fprintf(stderr, "No records loaded.\n");
        return FALSE_INT;
    }

    app->first_time = app->time_values[0];
    app->last_time = app->time_values[app->n_records - 1];
    double total_s = app->last_time - app->first_time;
    app->view_duration_s = DEFAULT_VIEW_DAYS * 86400.0;
    if (total_s > 0.0 && app->view_duration_s > total_s) app->view_duration_s = total_s;
    if (app->view_duration_s < 1.0) app->view_duration_s = 1.0;
    app->view_start_offset_s = 0.0;
    app->filename = strdup(filename);
    app->file_format = FILE_FORMAT_TOA5;
    app->timestamp_format = TIMESTAMP_FORMAT_TEXT;
    app->axis_type = AXIS_TYPE_CALENDAR_TIME;
    return TRUE_INT;

fail:
    fclose(fp);
    free_csv_fields(row1, n1);
    free_csv_fields(row2, n2);
    free_csv_fields(row3, n3);
    free_csv_fields(row4, n4);
    return FALSE_INT;
}


static void normalize_header_token(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (*src && isspace((unsigned char)*src)) src++;
    if (*src == '#') src++;
    while (*src && isspace((unsigned char)*src)) src++;
    if (*src == '"') src++;
    for (const char *p = src; *p && j + 1 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') continue;
        if (isalnum(c)) dst[j++] = (char)tolower(c);
    }
    dst[j] = '\0';
}

static int header_token_contains_word_julian(const char *src)
{
    char norm[256];
    normalize_header_token(src, norm, sizeof(norm));
    return strstr(norm, "julian") ? TRUE_INT : FALSE_INT;
}

static int header_token_is_datetime(const char *src)
{
    char norm[256];
    normalize_header_token(src, norm, sizeof(norm));
    return (strcmp(norm, "datetime") == 0 ||
            strcmp(norm, "timestamp") == 0 ||
            strcmp(norm, "dateandtime") == 0 ||
            strcmp(norm, "time") == 0 ||
            strcmp(norm, "date") == 0) ? TRUE_INT : FALSE_INT;
}

static int header_token_is_redundant_calendar_part(const char *src)
{
    char norm[256];
    normalize_header_token(src, norm, sizeof(norm));
    if (strcmp(norm, "year") == 0 || strcmp(norm, "yr") == 0) return TRUE_INT;
    if (strcmp(norm, "month") == 0 || strcmp(norm, "mon") == 0 || strcmp(norm, "mo") == 0) return TRUE_INT;
    if (strcmp(norm, "day") == 0 || strcmp(norm, "dy") == 0) return TRUE_INT;
    if (strcmp(norm, "hour") == 0 || strcmp(norm, "hr") == 0 || strcmp(norm, "hh") == 0) return TRUE_INT;
    if (strcmp(norm, "minute") == 0 || strcmp(norm, "min") == 0 || strcmp(norm, "mi") == 0) return TRUE_INT;
    if (strcmp(norm, "second") == 0 || strcmp(norm, "sec") == 0 || strcmp(norm, "ss") == 0) return TRUE_INT;
    return FALSE_INT;
}

static int header_token_is_year(const char *src)
{
    char norm[256];
    normalize_header_token(src, norm, sizeof(norm));
    return (strcmp(norm, "year") == 0 || strcmp(norm, "yr") == 0) ? TRUE_INT : FALSE_INT;
}

static int header_token_is_month(const char *src)
{
    char norm[256];
    normalize_header_token(src, norm, sizeof(norm));
    return (strcmp(norm, "month") == 0 || strcmp(norm, "mon") == 0 || strcmp(norm, "mo") == 0) ? TRUE_INT : FALSE_INT;
}

static int parse_int_field(const char *s, int *value_out)
{
    if (!s || !*s || !value_out) return FALSE_INT;
    while (*s && isspace((unsigned char)*s)) s++;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno || end == s) return FALSE_INT;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return FALSE_INT;
    *value_out = (int)v;
    return TRUE_INT;
}

static int last_day_of_month(int year, int month)
{
    static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 31;
    if (month == 2) {
        int leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? TRUE_INT : FALSE_INT;
        return leap ? 29 : 28;
    }
    return days_in_month[month - 1];
}

static int parse_year_month_fields(const char *year_text, const char *month_text, time_t *t_out)
{
    int year = 0;
    int month = 0;
    if (!t_out) return FALSE_INT;
    if (!parse_int_field(year_text, &year)) return FALSE_INT;
    if (!parse_int_field(month_text, &month)) return FALSE_INT;
    if (year < 1800 || year > 2200) return FALSE_INT;
    if (month < 1 || month > 12) return FALSE_INT;

    struct tm tm_value;
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = last_day_of_month(year, month);
    tm_value.tm_hour = 23;
    tm_value.tm_min = 59;
    tm_value.tm_sec = 59;
    tm_value.tm_isdst = -1;
    *t_out = timegm(&tm_value);
    return (*t_out != (time_t)-1) ? TRUE_INT : FALSE_INT;
}

static void apply_header_time_coordinate_hints(char **header_fields, int n_header,
                                               int *time_col, int *first_data_col,
                                               TimestampFormat *detected_format)
{
    if (!header_fields || n_header <= 0 || !time_col || !first_data_col) return;

    /* If the first column is explicitly labeled as a Julian day/date, trust it
       as the time coordinate.  Files often include redundant calendar fields
       immediately afterward (year, month, day, hour, minute, second); these are
       auxiliary encodings of the same time coordinate, not interval end times
       and not useful plotted variables. */
    if (header_token_contains_word_julian(header_fields[0])) {
        int col = 1;
        if (col < n_header && header_token_is_datetime(header_fields[col])) col++;
        while (col < n_header && header_token_is_redundant_calendar_part(header_fields[col])) col++;
        *time_col = 0;
        *first_data_col = col;
        if (detected_format && *detected_format != TIMESTAMP_FORMAT_MODIFIED_JULIAN_DATE) {
            *detected_format = TIMESTAMP_FORMAT_JULIAN_DATE;
        }
        return;
    }

    /* Monthly summary files commonly begin with year,month,... .
       Treat the pair as one monthly time coordinate plotted at the end of
       the month.  The next column, e.g. hours, remains a data variable. */
    if (n_header >= 2 && header_token_is_year(header_fields[0]) && header_token_is_month(header_fields[1])) {
        *time_col = 0;
        *first_data_col = 2;
        if (detected_format) *detected_format = TIMESTAMP_FORMAT_YEAR_MONTH;
        return;
    }
}

static int initialize_generic_series(AppData *app, char **header_fields, int n_fields, int has_header, int first_data_col)
{
    if (!app || n_fields <= first_data_col) return FALSE_INT;
    app->n_series = n_fields - first_data_col;
    app->series = (TimeSeries *)calloc((size_t)app->n_series, sizeof(TimeSeries));
    if (!app->series) return FALSE_INT;

    int palette_count = (int)(sizeof(palette) / sizeof(palette[0]));
    for (int i = 0; i < app->n_series; i++) {
        char name_buf[64];
        int col = i + first_data_col;
        if (has_header && header_fields && col < n_fields && header_fields[col] && header_fields[col][0]) {
            app->series[i].name = strdup(header_fields[col]);
        } else {
            snprintf(name_buf, sizeof(name_buf), "Value_%d", i + 1);
            app->series[i].name = strdup(name_buf);
        }
        app->series[i].units = strdup("");
        app->series[i].enabled = (i < 6) ? TRUE_INT : FALSE_INT;
        app->series[i].auto_disabled_for_log = FALSE_INT;
        app->series[i].red = palette[i % palette_count][0];
        app->series[i].green = palette[i % palette_count][1];
        app->series[i].blue = palette[i % palette_count][2];
        app->series[i].values = NULL;
    }
    return TRUE_INT;
}

static int timestamp_formats_are_interval_compatible(TimestampFormat tf0, TimestampFormat tf1)
{
    if ((tf0 == TIMESTAMP_FORMAT_COMPACT_YMDHM || tf0 == TIMESTAMP_FORMAT_COMPACT_YMDHMS) &&
        (tf1 == TIMESTAMP_FORMAT_COMPACT_YMDHM || tf1 == TIMESTAMP_FORMAT_COMPACT_YMDHMS)) {
        return TRUE_INT;
    }

    if ((tf0 == TIMESTAMP_FORMAT_JULIAN_DATE && tf1 == TIMESTAMP_FORMAT_JULIAN_DATE) ||
        (tf0 == TIMESTAMP_FORMAT_MODIFIED_JULIAN_DATE && tf1 == TIMESTAMP_FORMAT_MODIFIED_JULIAN_DATE) ||
        (tf0 == TIMESTAMP_FORMAT_UNIX_SECONDS && tf1 == TIMESTAMP_FORMAT_UNIX_SECONDS) ||
        (tf0 == TIMESTAMP_FORMAT_UNIX_MILLISECONDS && tf1 == TIMESTAMP_FORMAT_UNIX_MILLISECONDS) ||
        (tf0 == TIMESTAMP_FORMAT_TEXT && tf1 == TIMESTAMP_FORMAT_TEXT) ||
        (tf0 == TIMESTAMP_FORMAT_DATE_ONLY && tf1 == TIMESTAMP_FORMAT_DATE_ONLY)) {
        return TRUE_INT;
    }

    return FALSE_INT;
}

static int detect_interval_timestamp_columns(char **fields, int nf, int *time_col_out,
                                             int *first_data_col_out,
                                             TimestampFormat *format_out)
{
    time_t t0 = (time_t)-1;
    time_t t1 = (time_t)-1;
    TimestampFormat tf0 = TIMESTAMP_FORMAT_UNKNOWN;
    TimestampFormat tf1 = TIMESTAMP_FORMAT_UNKNOWN;
    int col0_is_time;
    int col1_is_time;

    if (!fields || nf < 1 || !time_col_out || !first_data_col_out) return FALSE_INT;

    col0_is_time = parse_timestamp_any(fields[0], &t0, &tf0);
    col1_is_time = (nf >= 2) ? parse_timestamp_any(fields[1], &t1, &tf1) : FALSE_INT;

    /* Preserve the established two-timestamp interval convention, including
       AmeriFlux start/end timestamps.  The interval end is the coordinate. */
    if (col0_is_time && col1_is_time &&
        timestamp_formats_are_interval_compatible(tf0, tf1) &&
        difftime(t1, t0) > 0.0) {
        *time_col_out = 1;
        *first_data_col_out = 2;
        if (format_out) {
            if ((tf0 == TIMESTAMP_FORMAT_COMPACT_YMDHM || tf0 == TIMESTAMP_FORMAT_COMPACT_YMDHMS) &&
                (tf1 == TIMESTAMP_FORMAT_COMPACT_YMDHM || tf1 == TIMESTAMP_FORMAT_COMPACT_YMDHMS)) {
                *format_out = TIMESTAMP_FORMAT_INTERVAL_END_COMPACT;
            } else {
                *format_out = tf1;
            }
        }
        return TRUE_INT;
    }

    /* JD/MJD followed by a calendar timestamp contains two encodings of the
       same coordinate.  Keep the numeric coordinate and skip the redundant
       timestamp rather than exposing it as a plotted data series. */
    if (col0_is_time && col1_is_time &&
        (tf0 == TIMESTAMP_FORMAT_JULIAN_DATE ||
         tf0 == TIMESTAMP_FORMAT_MODIFIED_JULIAN_DATE)) {
        *time_col_out = 0;
        *first_data_col_out = 2;
        if (format_out) *format_out = tf0;
        return TRUE_INT;
    }

    if (col0_is_time) {
        *time_col_out = 0;
        *first_data_col_out = 1;
        if (format_out) *format_out = tf0;
        return TRUE_INT;
    }

    /* Version 0.9: an index or ancillary first field may precede the primary
       timestamp.  Deliberately search no farther than column 2. */
    if (col1_is_time) {
        *time_col_out = 1;
        *first_data_col_out = 2;
        if (format_out) *format_out = tf1;
        return TRUE_INT;
    }

    {
        double x_value = NAN;
        if (parse_double_or_nan(fields[0], &x_value) && isfinite(x_value)) {
            *time_col_out = 0;
            *first_data_col_out = 1;
            if (format_out) *format_out = TIMESTAMP_FORMAT_NUMERIC_ABSCISSA;
            return TRUE_INT;
        }
    }

    return FALSE_INT;
}

static int process_generic_data_fields(AppData *app, char **fields, int nf, int time_col, int first_data_col, TimestampFormat *detected_format)
{
    if (!app || !fields || nf <= time_col) return TRUE_INT;

    time_t t = (time_t)-1;
    double x_value = NAN;
    TimestampFormat tf = TIMESTAMP_FORMAT_UNKNOWN;

    if (detected_format && *detected_format == TIMESTAMP_FORMAT_YEAR_MONTH) {
        if (nf <= time_col + 1) return TRUE_INT;
        if (!parse_year_month_fields(fields[time_col], fields[time_col + 1], &t)) return TRUE_INT;
        tf = TIMESTAMP_FORMAT_YEAR_MONTH;
        x_value = (double)t;
    } else if (detected_format && *detected_format == TIMESTAMP_FORMAT_NUMERIC_ABSCISSA) {
        if (!parse_double_or_nan(fields[time_col], &x_value) || !isfinite(x_value)) return TRUE_INT;
        tf = TIMESTAMP_FORMAT_NUMERIC_ABSCISSA;
    } else if (parse_timestamp_any(fields[time_col], &t, &tf)) {
        x_value = (double)t;
    } else {
        if (!parse_double_or_nan(fields[time_col], &x_value) || !isfinite(x_value)) return TRUE_INT;
        tf = TIMESTAMP_FORMAT_NUMERIC_ABSCISSA;
    }

    if (*detected_format == TIMESTAMP_FORMAT_UNKNOWN) {
        *detected_format = tf;
    }

    if (app->n_records > 0) {
        double previous_x = app->time_values[app->n_records - 1];
        if (x_value < previous_x) {
            fprintf(stderr,
                    "Warning: abscissa decreased at record %d: "
                    "%.17g follows %.17g; continuing.\n",
                    app->n_records + 1, x_value, previous_x);
        } else if (x_value == previous_x) {
            fprintf(stderr,
                    "Warning: duplicate abscissa at record %d: "
                    "%.17g; continuing.\n",
                    app->n_records + 1, x_value);
        }
    }

    if (!ensure_record_capacity(app, app->n_records + 1)) return FALSE_INT;

    int r = app->n_records;
    app->time_values[r] = x_value;
    for (int i = 0; i < app->n_series; i++) {
        int col = i + first_data_col;
        double v = NAN;
        if (col < nf) parse_double_or_nan(fields[col], &v);
        app->series[i].values[r] = v;
    }
    app->n_records++;
    return TRUE_INT;
}

static int finish_loaded_file(AppData *app, const char *filename, FileFormat file_format, TimestampFormat timestamp_format)
{
    if (!app || app->n_records <= 0) {
        fprintf(stderr, "No records loaded.\n");
        return FALSE_INT;
    }

    app->first_time = app->time_values[0];
    app->last_time = app->time_values[app->n_records - 1];
    double total_s = app->last_time - app->first_time;
    app->view_duration_s = DEFAULT_VIEW_DAYS * 86400.0;
    if (total_s > 0.0 && app->view_duration_s > total_s) app->view_duration_s = total_s;
    if (app->view_duration_s < 1.0) app->view_duration_s = 1.0;
    app->view_start_offset_s = 0.0;
    app->filename = strdup(filename);
    app->file_format = file_format;
    app->timestamp_format = timestamp_format;
    app->axis_type = (timestamp_format == TIMESTAMP_FORMAT_NUMERIC_ABSCISSA)
                   ? AXIS_TYPE_NUMERIC : AXIS_TYPE_CALENDAR_TIME;
    return TRUE_INT;
}


static int is_blank_line(const char *line)
{
    if (!line) return TRUE_INT;
    while (*line) {
        if (!isspace((unsigned char)*line)) return FALSE_INT;
        line++;
    }
    return TRUE_INT;
}

static int is_comment_line(const char *line)
{
    if (!line) return FALSE_INT;
    while (*line && isspace((unsigned char)*line)) line++;
    return (*line == '#') ? TRUE_INT : FALSE_INT;
}

static DelimiterType detect_delimiter_from_line(const char *line)
{
    int comma_count = 0;
    int tab_count = 0;
    int pipe_count = 0;
    if (!line) return DELIMITER_WHITESPACE;
    for (const char *p = line; *p; p++) {
        if (*p == ',') comma_count++;
        else if (*p == '\t') tab_count++;
        else if (*p == '|') pipe_count++;
    }
    if (comma_count > 0) return DELIMITER_COMMA;
    if (tab_count > 0) return DELIMITER_TAB;
    if (pipe_count > 0) return DELIMITER_PIPE;
    return DELIMITER_WHITESPACE;
}

static int split_char_delimited_line(const char *line, char delimiter, char ***fields_out)
{
    int capacity = 16;
    int n_fields = 0;
    char **fields = (char **)calloc((size_t)capacity, sizeof(char *));
    if (!fields) return -1;

    const char *start = line;
    const char *p = line;
    while (TRUE_INT) {
        if (*p == delimiter || *p == '\0' || *p == '\r' || *p == '\n') {
            size_t len = (size_t)(p - start);
            char *tmp = (char *)malloc(len + 1);
            if (!tmp) {
                free_csv_fields(fields, n_fields);
                return -1;
            }
            memcpy(tmp, start, len);
            tmp[len] = '\0';

            if (n_fields >= capacity) {
                capacity *= 2;
                char **new_fields = (char **)realloc(fields, (size_t)capacity * sizeof(char *));
                if (!new_fields) {
                    free(tmp);
                    free_csv_fields(fields, n_fields);
                    return -1;
                }
                fields = new_fields;
            }
            fields[n_fields++] = trim_copy(tmp);
            free(tmp);

            if (*p == delimiter) {
                p++;
                start = p;
                continue;
            }
            break;
        }
        p++;
    }

    *fields_out = fields;
    return n_fields;
}

static int split_whitespace_line(const char *line, char ***fields_out)
{
    int capacity = 16;
    int n_fields = 0;
    char **fields = (char **)calloc((size_t)capacity, sizeof(char *));
    if (!fields) return -1;

    const char *p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t len = (size_t)(p - start);
        char *tmp = (char *)malloc(len + 1);
        if (!tmp) {
            free_csv_fields(fields, n_fields);
            return -1;
        }
        memcpy(tmp, start, len);
        tmp[len] = '\0';

        if (n_fields >= capacity) {
            capacity *= 2;
            char **new_fields = (char **)realloc(fields, (size_t)capacity * sizeof(char *));
            if (!new_fields) {
                free(tmp);
                free_csv_fields(fields, n_fields);
                return -1;
            }
            fields = new_fields;
        }
        fields[n_fields++] = tmp;
    }

    *fields_out = fields;
    return n_fields;
}

static int split_data_line(const char *line, DelimiterType delimiter, char ***fields_out)
{
    if (delimiter == DELIMITER_COMMA) return parse_csv_line(line, fields_out);
    if (delimiter == DELIMITER_TAB) return split_char_delimited_line(line, '\t', fields_out);
    if (delimiter == DELIMITER_PIPE) return split_char_delimited_line(line, '|', fields_out);
    return split_whitespace_line(line, fields_out);
}


static char *clean_comment_candidate(const char *line)
{
    if (!line) return NULL;
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line != '#') return NULL;
    line++;
    if (*line == ' ') line++;
    return trim_copy(line);
}

static int read_next_noncomment_line(FILE *fp, char *line, size_t line_size)
{
    while (fgets(line, (int)line_size, fp)) {
        if (is_blank_line(line) || is_comment_line(line)) continue;
        return TRUE_INT;
    }
    return FALSE_INT;
}

static int load_generic_delimited(AppData *app, const char *filename, int has_header, DelimiterType delimiter)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Could not open %s\n", filename);
        return FALSE_INT;
    }

    char line[MAX_LINE];
    char **header_fields = NULL;
    int n_header = 0;
    TimestampFormat detected_format = TIMESTAMP_FORMAT_UNKNOWN;
    int time_col = 0;
    int first_data_col = 1;
    int initialized = FALSE_INT;
    int effective_has_header = has_header;
    char *last_comment_candidate = NULL;

    if (has_header) {
        if (!read_next_noncomment_line(fp, line, sizeof(line))) {
            fclose(fp);
            return FALSE_INT;
        }
        n_header = split_data_line(line, delimiter, &header_fields);
        if (n_header < 2) {
            free_csv_fields(header_fields, n_header);
            fclose(fp);
            return FALSE_INT;
        }
    } else {
        int found_data_line = FALSE_INT;
        while (fgets(line, sizeof(line), fp)) {
            if (is_blank_line(line)) continue;
            if (is_comment_line(line)) {
                char *candidate = clean_comment_candidate(line);
                if (candidate && candidate[0]) {
                    free(last_comment_candidate);
                    last_comment_candidate = candidate;
                } else {
                    free(candidate);
                }
                continue;
            }
            found_data_line = TRUE_INT;
            break;
        }

        if (!found_data_line) {
            free(last_comment_candidate);
            fclose(fp);
            return FALSE_INT;
        }

        char **first_fields = NULL;
        int n_first = split_data_line(line, delimiter, &first_fields);
        if (!detect_interval_timestamp_columns(first_fields, n_first, &time_col, &first_data_col, &detected_format)) {
            fprintf(stderr, "Unable to parse the first data coordinate in %s.\n", filename);
            free(last_comment_candidate);
            free_csv_fields(first_fields, n_first);
            fclose(fp);
            return FALSE_INT;
        }

        if (last_comment_candidate) {
            n_header = split_data_line(last_comment_candidate, delimiter, &header_fields);
            if (n_header != n_first) {
                free_csv_fields(header_fields, n_header);
                header_fields = NULL;
                n_header = 0;
            } else {
                effective_has_header = TRUE_INT;
            }
        }

        if (header_fields) apply_header_time_coordinate_hints(header_fields, n_header, &time_col, &first_data_col, &detected_format);

        if (!initialize_generic_series(app, header_fields, n_first, (header_fields != NULL) ? TRUE_INT : FALSE_INT, first_data_col)) {
            free(last_comment_candidate);
            free_csv_fields(header_fields, n_header);
            free_csv_fields(first_fields, n_first);
            fclose(fp);
            return FALSE_INT;
        }
        initialized = TRUE_INT;
        if (!process_generic_data_fields(app, first_fields, n_first, time_col, first_data_col, &detected_format)) {
            free(last_comment_candidate);
            free_csv_fields(header_fields, n_header);
            free_csv_fields(first_fields, n_first);
            fclose(fp);
            return FALSE_INT;
        }
        free(last_comment_candidate);
        free_csv_fields(first_fields, n_first);
    }

    while (fgets(line, sizeof(line), fp)) {
        if (is_blank_line(line) || is_comment_line(line)) continue;
        char **fields = NULL;
        int nf = split_data_line(line, delimiter, &fields);
        if (nf < 1) {
            free_csv_fields(fields, nf);
            continue;
        }

        if (!initialized) {
            if (!detect_interval_timestamp_columns(fields, nf, &time_col, &first_data_col, &detected_format)) {
                fprintf(stderr, "Unable to parse the first data coordinate in %s.\n", filename);
                free_csv_fields(fields, nf);
                free_csv_fields(header_fields, n_header);
                fclose(fp);
                return FALSE_INT;
            }
            apply_header_time_coordinate_hints(header_fields, n_header, &time_col, &first_data_col, &detected_format);
            if (!initialize_generic_series(app, header_fields, n_header, TRUE_INT, first_data_col)) {
                free_csv_fields(fields, nf);
                free_csv_fields(header_fields, n_header);
                fclose(fp);
                return FALSE_INT;
            }
            initialized = TRUE_INT;
        }

        if (!process_generic_data_fields(app, fields, nf, time_col, first_data_col, &detected_format)) {
            free_csv_fields(fields, nf);
            free_csv_fields(header_fields, n_header);
            fclose(fp);
            return FALSE_INT;
        }
        free_csv_fields(fields, nf);
    }

    free_csv_fields(header_fields, n_header);
    fclose(fp);

    return finish_loaded_file(app, filename,
                              effective_has_header ? FILE_FORMAT_DELIMITED_HEADER : FILE_FORMAT_DELIMITED_NO_HEADER,
                              detected_format);
}

static int line_first_field_is_coordinate_delimited(const char *line,
                                                    DelimiterType delimiter,
                                                    TimestampFormat *format_out)
{
    char **fields = NULL;
    int nf = split_data_line(line, delimiter, &fields);
    int ok = FALSE_INT;

    if (format_out) *format_out = TIMESTAMP_FORMAT_UNKNOWN;

    if (nf > 0) {
        time_t t;
        TimestampFormat tf = TIMESTAMP_FORMAT_UNKNOWN;

        if (parse_timestamp_any(fields[0], &t, &tf)) {
            ok = TRUE_INT;
            if (format_out) *format_out = tf;
        } else {
            double x_value = NAN;
            if (parse_double_or_nan(fields[0], &x_value) && isfinite(x_value)) {
                ok = TRUE_INT;
                if (format_out) *format_out = TIMESTAMP_FORMAT_NUMERIC_ABSCISSA;
            }
        }
    }

    free_csv_fields(fields, nf);
    return ok;
}

static int load_time_series_file(AppData *app, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Could not open %s\n", filename);
        return FALSE_INT;
    }

    char line1[MAX_LINE];
    char line2[MAX_LINE];
    line1[0] = '\0';
    line2[0] = '\0';

    if (!read_next_noncomment_line(fp, line1, sizeof(line1))) {
        fclose(fp);
        fprintf(stderr, "Empty file.\n");
        return FALSE_INT;
    }
    (void)read_next_noncomment_line(fp, line2, sizeof(line2));
    fclose(fp);

    DelimiterType delimiter = detect_delimiter_from_line(line1);

    if (delimiter == DELIMITER_COMMA) {
        char **f1 = NULL;
        int n1 = parse_csv_line(line1, &f1);
        if (n1 > 0 && strcmp(f1[0], "TOA5") == 0) {
            free_csv_fields(f1, n1);
            return load_toa5_csv(app, filename);
        }
        free_csv_fields(f1, n1);
    }

    TimestampFormat first_tf = TIMESTAMP_FORMAT_UNKNOWN;
    TimestampFormat second_tf = TIMESTAMP_FORMAT_UNKNOWN;
    int first_is_coordinate = line_first_field_is_coordinate_delimited(line1, delimiter, &first_tf);
    int second_is_coordinate = line2[0]
                             ? line_first_field_is_coordinate_delimited(line2, delimiter, &second_tf)
                             : FALSE_INT;

    /* A non-coordinate first line followed by a coordinate line is a header. */
    if (!first_is_coordinate && second_is_coordinate) {
        return load_generic_delimited(app, filename, TRUE_INT, delimiter);
    }

    /* A coordinate in the first line means the file begins with data. */
    if (first_is_coordinate) {
        return load_generic_delimited(app, filename, FALSE_INT, delimiter);
    }

    fprintf(stderr,
            "Unable to determine file format. The first data column must be either\n"
            "a recognized timestamp or a finite numeric abscissa.\n");
    return FALSE_INT;
}


static int append_loaded_file(AppData *app, const char *filename, int file_index)
{
    AppData tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (!load_time_series_file(&tmp, filename)) return FALSE_INT;
    if (tmp.axis_type != app->axis_type) {
        fprintf(stderr, "Cannot mix calendar-time files with numeric-abscissa files.\n");
        free(tmp.time_values);
        for (int i = 0; i < tmp.n_series; i++) {
            free(tmp.series[i].name); free(tmp.series[i].units); free(tmp.series[i].values);
        }
        free(tmp.series); free(tmp.filename);
        return FALSE_INT;
    }

    int old_n = app->n_series;
    TimeSeries *combined = realloc(app->series, (size_t)(old_n + tmp.n_series) * sizeof(TimeSeries));
    if (!combined) {
        free(tmp.time_values);
        for (int i=0; i<tmp.n_series; i++) { free(tmp.series[i].name); free(tmp.series[i].units); free(tmp.series[i].values); }
        free(tmp.series); free(tmp.filename);
        return FALSE_INT;
    }
    app->series = combined;
    for (int i=0; i<tmp.n_series; i++) {
        TimeSeries *dst = &app->series[old_n+i];
        *dst = tmp.series[i];
        dst->source_file_index = file_index;
        dst->n_records = tmp.n_records;
        dst->time_values = malloc((size_t)tmp.n_records * sizeof(double));
        if (!dst->time_values) return FALSE_INT;
        memcpy(dst->time_values, tmp.time_values, (size_t)tmp.n_records * sizeof(double));
        for (int j=0; j<old_n+i; j++) {
            if (normalized_names_equal(dst->name, app->series[j].name)) {
                dst->red=app->series[j].red; dst->green=app->series[j].green; dst->blue=app->series[j].blue;
                break;
            }
        }
    }
    app->n_series = old_n + tmp.n_series;
    if (tmp.first_time < app->first_time) app->first_time = tmp.first_time;
    if (tmp.last_time > app->last_time) app->last_time = tmp.last_time;
    app->n_files++;
    app->filenames[file_index] = strdup(filename);
    free(tmp.time_values);
    free(tmp.series);
    free(tmp.filename);
    return TRUE_INT;
}

static void format_time_label(const AppData *app, double x, char *buf, size_t buf_size)
{
    if (app && app->axis_type == AXIS_TYPE_NUMERIC) {
        snprintf(buf, buf_size, "%.10g", x);
        return;
    }
    time_t t = (time_t)llround(x);
    struct tm *tm_value = gmtime(&t);
    if (!tm_value) {
        snprintf(buf, buf_size, "?");
        return;
    }
    strftime(buf, buf_size, "%Y-%m-%d %H:%M", tm_value);
}



static void format_x_tick(time_t t, char *time_buf, size_t time_size, char *date_buf, size_t date_size)
{
    struct tm *tm_value = gmtime(&t);
    if (!tm_value) {
        snprintf(time_buf, time_size, "?");
        snprintf(date_buf, date_size, "?");
        return;
    }
    strftime(time_buf, time_size, "%H:%M", tm_value);
    strftime(date_buf, date_size, "%Y-%m-%d", tm_value);
}

static int nice_time_tick_spacing(double span_s)
{
    static const int intervals[] = {
        300, 600, 900, 1800,
        3600, 7200, 10800, 21600, 43200,
        86400, 2 * 86400, 7 * 86400, 14 * 86400, 30 * 86400, 90 * 86400, 365 * 86400
    };
    int n = (int)(sizeof(intervals) / sizeof(intervals[0]));
    double target = span_s / 6.0;
    for (int i = 0; i < n; i++) {
        if ((double)intervals[i] >= target) return intervals[i];
    }
    return intervals[n - 1];
}

static time_t first_regular_tick(time_t t_start, int spacing_s)
{
    if (spacing_s <= 0) return t_start;

    struct tm tm_value;
    struct tm *tmp = gmtime(&t_start);
    if (!tmp) return t_start;
    tm_value = *tmp;

    if (spacing_s >= 86400) {
        tm_value.tm_hour = 0;
        tm_value.tm_min = 0;
        tm_value.tm_sec = 0;
    } else if (spacing_s >= 3600) {
        int hours = spacing_s / 3600;
        if (hours < 1) hours = 1;
        tm_value.tm_hour = (tm_value.tm_hour / hours) * hours;
        tm_value.tm_min = 0;
        tm_value.tm_sec = 0;
    } else if (spacing_s >= 60) {
        int minutes = spacing_s / 60;
        if (minutes < 1) minutes = 1;
        tm_value.tm_min = (tm_value.tm_min / minutes) * minutes;
        tm_value.tm_sec = 0;
    } else {
        tm_value.tm_sec = (tm_value.tm_sec / spacing_s) * spacing_s;
    }

    tm_value.tm_isdst = -1;
    time_t tick = timegm(&tm_value);
    if (tick == (time_t)-1) return t_start;
    while (tick < t_start) tick += spacing_s;
    return tick;
}

static double clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static double time_from_plot_x(AppData *app, GtkWidget *widget, double x)
{
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double x0 = LEFT_MARGIN;
    double x1 = alloc.width - RIGHT_MARGIN;
    if (x1 <= x0) return app->first_time;
    double frac = clamp_double((x - x0) / (x1 - x0), 0.0, 1.0);
    double view_start = app->first_time + app->view_start_offset_s;
    double view_end = view_start + app->view_duration_s;
    return view_start + frac * (view_end - view_start);
}

static void set_view_window(AppData *app, double t_start, double t_end)
{
    if (!app || app->n_records <= 0) return;
    if (t_end < t_start) {
        double tmp = t_start;
        t_start = t_end;
        t_end = tmp;
    }
    if (t_start < app->first_time) t_start = app->first_time;
    if (t_end > app->last_time) t_end = app->last_time;
    if (t_end <= t_start) return;

    double total_s = app->last_time - app->first_time;
    double duration_s = (t_end - t_start);
    double offset_s = (t_start - app->first_time);
    double minimum_span = (app->axis_type == AXIS_TYPE_CALENDAR_TIME) ? 60.0 : total_s * 1.0e-9;
    if (minimum_span <= 0.0) minimum_span = 1.0e-12;
    if (duration_s < minimum_span) duration_s = minimum_span;
    if (duration_s > total_s) duration_s = total_s;
    if (offset_s < 0.0) offset_s = 0.0;
    if (offset_s + duration_s > total_s) offset_s = total_s - duration_s;
    if (offset_s < 0.0) offset_s = 0.0;

    app->view_start_offset_s = offset_s;
    app->view_duration_s = duration_s;

    if (app->scrollbar) {
        GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(app->scrollbar));
        double step = duration_s / 20.0;
        if (app->axis_type == AXIS_TYPE_CALENDAR_TIME && step < 60.0) step = 60.0;
        double page = duration_s * 0.80;
        gtk_adjustment_configure(adj, offset_s, 0.0, total_s, step, page, duration_s);
    }
}

static void reset_view_to_full_record(AppData *app)
{
    if (!app || app->n_records <= 0) return;
    app->selection_active = FALSE_INT;
    app->is_dragging_selection = FALSE_INT;
    set_view_window(app, app->first_time, app->last_time);
    update_status(app);
    if (app->drawing_area) gtk_widget_queue_draw(app->drawing_area);
}

static int point_inside_plot(GtkWidget *widget, double x, double y)
{
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double x0 = LEFT_MARGIN;
    double x1 = alloc.width - RIGHT_MARGIN;
    double y0 = TOP_MARGIN;
    double y1 = alloc.height - BOTTOM_MARGIN;
    return (x >= x0 && x <= x1 && y >= y0 && y <= y1) ? TRUE_INT : FALSE_INT;
}

static void ordered_selection_times(AppData *app, double *a_out, double *b_out)
{
    if (app->selection_t0 <= app->selection_t1) {
        *a_out = app->selection_t0;
        *b_out = app->selection_t1;
    } else {
        *a_out = app->selection_t1;
        *b_out = app->selection_t0;
    }
}

static double nice_tick_spacing(double span, int target_ticks)
{
    static const double steps[] = {1.0, 2.0, 2.5, 5.0, 10.0};

    /* C99: isfinite() is a type-generic macro from <math.h>; C89 has no
       portable way to test for NaN/Inf other than the `v == v` trick. */
    if (!isfinite(span) || span <= 0.0)
        return 1.0;

    double raw = span / (double)(target_ticks > 1 ? target_ticks : 5);
    double exponent = floor(log10(raw));
    double scale = pow(10.0, exponent);
    double best = steps[4] * scale;

    for (int i = 0; i < 5; i++) {
        double candidate = steps[i] * scale;

        if (candidate >= raw) {
            best = candidate;
            break;
        }
    }

    return best;
}

static int decimals_for_spacing(double spacing)
{
    if (!isfinite(spacing) || spacing <= 0.0 || spacing >= 1.0)
        return 0;

    int decimals = (int)ceil(-log10(spacing) - 1.0e-12);

    /*
     * A spacing such as 0.025 requires one more decimal place than
     * ceil(-log10(spacing)) alone would indicate.
     */
    double scale = pow(10.0, (double)decimals);

    if (fabs(spacing * scale - round(spacing * scale)) > 1.0e-9)
        decimals++;

    if (decimals > 10)
        decimals = 10;

    return decimals;
}

static void append_nse_kge_statistics(GString *text,
                                      const AppData *app,
                                      const TimeSeries *reference,
                                      const TimeSeries *model,
                                      double t_start,
                                      double t_end)
{
    int ri0 = 0, ri1 = 0, mi0 = 0, mi1 = 0;
    int rn;
    int mn;
    int valid;
    int log_values_valid = TRUE_INT;
    double sr = 0.0;
    double sm = 0.0;

    if (!text || !app || !reference || !model) return;

    g_string_append_printf(text, "\nModel: F%d: %s\n",
                           model->source_file_index + 1, model->name);
    if (model->source_file_index >= 0 && model->source_file_index < app->n_files)
        g_string_append_printf(text, "Source: %s\n",
                               app->filenames[model->source_file_index]);

    series_index_range(reference, t_start, t_end, &ri0, &ri1);
    series_index_range(model, t_start, t_end, &mi0, &mi1);
    rn = ri1 - ri0;
    mn = mi1 - mi0;
    valid = (rn > 1 && rn == mn);

    if (valid) {
        for (int k = 0; k < rn; k++) {
            double rvalue = reference->values[ri0 + k];
            double mvalue = model->values[mi0 + k];

            if (reference->time_values[ri0 + k] != model->time_values[mi0 + k] ||
                !isfinite(rvalue) || !isfinite(mvalue)) {
                valid = FALSE_INT;
                break;
            }

            if (app->log_y_axis && (rvalue <= 0.0 || mvalue <= 0.0)) {
                log_values_valid = FALSE_INT;
                break;
            }

            if (app->log_y_axis) {
                rvalue = log10(rvalue);
                mvalue = log10(mvalue);
            }
            sr += rvalue;
            sm += mvalue;
        }
    }

    if (!valid) {
        g_string_append(text,
            "  NSE/KGE unavailable: missing values or timestamps do not align\n");
        return;
    }

    if (app->log_y_axis && !log_values_valid) {
        g_string_append(text,
            "  Calculation of NSE and KGE of log-transformed values not possible\n"
            "  because both series are not everywhere >0.\n");
        return;
    }

    {
        double mr = sr / (double)rn;
        double mm = sm / (double)rn;
        double sse = 0.0;
        double ssr = 0.0;
        double ssm = 0.0;
        double cross = 0.0;

        for (int k = 0; k < rn; k++) {
            double rvalue = reference->values[ri0 + k];
            double mvalue = model->values[mi0 + k];

            if (app->log_y_axis) {
                rvalue = log10(rvalue);
                mvalue = log10(mvalue);
            }

            double dref = rvalue - mr;
            double dmod = mvalue - mm;
            sse += (mvalue - rvalue) * (mvalue - rvalue);
            ssr += dref * dref;
            ssm += dmod * dmod;
            cross += dref * dmod;
        }

        if (ssr <= 0.0 || ssm <= 0.0 || fabs(mr) <= 1.0e-15) {
            g_string_append(text,
                app->log_y_axis
                ? "  NSE_log(x)/KGE_log(x) undefined for constant or zero-mean log-transformed reference data\n"
                : "  NSE/KGE undefined for constant or zero-mean reference data\n");
        }
        else {
            double nse = 1.0 - sse / ssr;
            double r = cross / sqrt(ssr * ssm);
            double alpha = sqrt(ssm / (double)(rn - 1)) /
                           sqrt(ssr / (double)(rn - 1));
            double beta = mm / mr;
            double kge = 1.0 - sqrt((r - 1.0) * (r - 1.0) +
                                    (alpha - 1.0) * (alpha - 1.0) +
                                    (beta - 1.0) * (beta - 1.0));
            if (app->log_y_axis) {
                g_string_append_printf(text,
                    "  NSE_log(x) = %.6f\n  KGE_log(x) = %.6f\n", nse, kge);
            }
            else {
                g_string_append_printf(text,
                    "  NSE = %.6f\n  KGE = %.6f\n", nse, kge);
            }
        }
    }
}

static void update_stats_panel(AppData *app)
{
    double t_start;
    double t_end;
    int selected_window = FALSE_INT;
    char a[64], b[64];
    GString *text;

    if (!app || !app->stats_label || app->n_series <= 0) return;

    if (app->selection_active) {
        ordered_selection_times(app, &t_start, &t_end);
        selected_window = TRUE_INT;
    } else {
        t_start = app->first_time + app->view_start_offset_s;
        t_end = t_start + app->view_duration_s;
    }

    format_time_label(app, t_start, a, sizeof(a));
    format_time_label(app, t_end, b, sizeof(b));

    text = g_string_new(NULL);
    g_string_append_printf(text, "%s\n%s\n%s\n\n",
                           selected_window ? "Selected-window statistics" : "Visible-window statistics",
                           a, b);

    if (app->reference_series_index >= 0 &&
        app->reference_series_index < app->n_series) {
        TimeSeries *reference = &app->series[app->reference_series_index];
        int i0 = 0, i1 = 0;
        double vmin = HUGE_VAL;
        double vmax = -HUGE_VAL;
        double sum = 0.0;
        double sumsq = 0.0;
        int n = 0;

        series_index_range(reference, t_start, t_end, &i0, &i1);
        for (int i = i0; i < i1; i++) {
            double v = series_display_value(app, reference, i);
            if (!isfinite(v)) continue;
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
            sum += v;
            sumsq += v * v;
            n++;
        }

        g_string_append_printf(text, "Reference: F%d: %s",
                               reference->source_file_index + 1, reference->name);
        if (reference->units && reference->units[0])
            g_string_append_printf(text, " (%s)", reference->units);
        g_string_append_printf(text, "\nSource: %s\n\n",
                               (reference->source_file_index >= 0 &&
                                reference->source_file_index < app->n_files)
                               ? app->filenames[reference->source_file_index] : "?");

        if (n > 0) {
            double mean = sum / (double)n;
            double variance = 0.0;
            if (n > 1) {
                variance = (sumsq - sum * sum / (double)n) / (double)(n - 1);
                if (variance < 0.0 && variance > -1.0e-12) variance = 0.0;
            }
            g_string_append_printf(text,
                                   "N      %8d\n"
                                   "Min    %8.4g\n"
                                   "Mean   %8.4g\n"
                                   "Max    %8.4g\n"
                                   "StdDev %8.4g\n",
                                   n, vmin, mean, vmax, sqrt(variance));
        } else {
            g_string_append(text, "No finite values in this interval.\n");
        }

        if (app->pair_sets_check &&
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pair_sets_check))) {
            int comparison_count = 0;

            for (int j = 0; j < app->n_series; j++) {
                TimeSeries *model = &app->series[j];
                if (model->source_file_index == 0) continue;
                if (!model->enabled) continue;
                if (!normalized_names_equal(reference->name, model->name)) continue;
                append_nse_kge_statistics(text, app, reference, model,
                                          t_start, t_end);
                comparison_count++;
            }

            if (comparison_count == 0) {
                g_string_append(text,
                    "\nNSE/KGE suppressed: no matching model series are selected.\n");
            }
        }
        else if (app->model_series_index >= 0 &&
                 app->model_series_index < app->n_series) {
            TimeSeries *model = &app->series[app->model_series_index];
            append_nse_kge_statistics(text, app, reference, model,
                                      t_start, t_end);
        }
        else {
            g_string_append(text,
                "\nSelect a Model series to calculate NSE/KGE.\n");
        }
    }
    else {
        g_string_append(text, "Select a Reference series for statistics.\n");
    }

    if (app->selection_active) {
        g_string_append(text,
            "\nClick-drag to zoom. Use horizontal scroll bar to pan after zoom.\n"
            "Right-click or R resets full view. Esc quits.\n");
    }

    gtk_label_set_text(GTK_LABEL(app->stats_label), text->str);
    g_string_free(text, TRUE);
}

static void update_status(AppData *app)
{
    if (!app->status_label || app->n_records <= 0) return;
    double view_start = app->first_time + app->view_start_offset_s;
    double view_end = view_start + app->view_duration_s;
    char a[64], b[64];
    format_time_label(app, view_start, a, sizeof(a));
    format_time_label(app, view_end, b, sizeof(b));

    GString *text = g_string_new(NULL);
    g_string_append_printf(text, "%d file%s",
                           app->n_files, app->n_files == 1 ? "" : "s");
    if (app->reference_series_index >= 0 &&
        app->reference_series_index < app->n_series) {
        TimeSeries *reference = &app->series[app->reference_series_index];
        g_string_append_printf(text, "    Statistical reference: F%d:%s",
                               reference->source_file_index + 1, reference->name);
    }
    g_string_append_printf(text, "    Window: %s  to  %s", a, b);

    if (app->mouse_inside && app->drawing_area) {
        GtkAllocation alloc;
        gtk_widget_get_allocation(app->drawing_area, &alloc);
        double x0 = LEFT_MARGIN;
        double x1 = alloc.width - RIGHT_MARGIN;
        if (x1 > x0 && app->mouse_x >= x0 && app->mouse_x <= x1) {
            double frac = (app->mouse_x - x0) / (x1 - x0);
            double cursor_time = view_start + frac * (view_end - view_start);
            char c[64];
            format_time_label(app, cursor_time, c, sizeof(c));
            g_string_append_printf(text, "    Cursor: %s", c);
            int count = 0;
            for (int s = 0; s < app->n_series && count < 6; s++) {
                TimeSeries *series = &app->series[s];
                if (!series->enabled || series->n_records <= 0) continue;
                int lo = 0, hi = series->n_records - 1;
                while (lo < hi) {
                    int mid = lo + (hi - lo) / 2;
                    if (series->time_values[mid] < cursor_time) lo = mid + 1;
                    else hi = mid;
                }
                if (lo > 0 && fabs((series->time_values[lo-1] - cursor_time)) <
                              fabs((series->time_values[lo] - cursor_time))) lo--;
                double v = series->values[lo];
                if (isfinite(v)) {
                    g_string_append_printf(text, "    F%d:%s=%.4g",
                                           series->source_file_index + 1, series->name, v);
                    count++;
                }
            }
        }
    }

    if (app->selection_active) {
        double sa, sb;
        char sba[64], sbb[64];
        ordered_selection_times(app, &sa, &sb);
        format_time_label(app, sa, sba, sizeof(sba));
        format_time_label(app, sb, sbb, sizeof(sbb));
        g_string_append_printf(text, "    Selection: %s to %s", sba, sbb);
    }

    if (app->status_notice[0] != '\0') {
        g_string_append_printf(text, "    %s", app->status_notice);
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), text->str);
    g_string_free(text, TRUE);
    update_stats_panel(app);
}

static int value_is_visible_for_y_mode(const AppData *app, double value)
{
    if (!isfinite(value)) return FALSE_INT;
    if (app->log_y_axis) return value > 0.0;
    if (app->y_range_mode == Y_RANGE_POSITIVE_ONLY) return value >= 0.0;
    if (app->y_range_mode == Y_RANGE_NEGATIVE_ONLY) return value <= 0.0;
    return TRUE_INT;
}

static void get_series_display_color(const AppData *app, const TimeSeries *series,
                                     double *red, double *green, double *blue)
{
    if (!app || !series || !red || !green || !blue) return;

    if (app->match_variable_colors) {
        *red = series->red;
        *green = series->green;
        *blue = series->blue;
    }
    else {
        int palette_count = (int)(sizeof(palette) / sizeof(palette[0]));
        ptrdiff_t series_index = series - app->series;
        if (series_index < 0 || series_index >= app->n_series) series_index = 0;
        *red = palette[series_index % palette_count][0];
        *green = palette[series_index % palette_count][1];
        *blue = palette[series_index % palette_count][2];
    }
}

static gboolean draw_swatch(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    TimeSeries *series = (TimeSeries *)user_data;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    AppData *app = g_object_get_data(G_OBJECT(widget), "app");
    double red = series->red, green = series->green, blue = series->blue;
    get_series_display_color(app, series, &red, &green, &blue);
    cairo_set_source_rgb(cr, red, green, blue);
    cairo_set_line_width(cr, 2.0);
    apply_file_line_style(cr, series->source_file_index);
    cairo_move_to(cr, 2.0, 0.5 * a.height);
    cairo_line_to(cr, a.width - 2.0, 0.5 * a.height);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
    cairo_rectangle(cr, 0.5, 0.5, a.width - 1.0, a.height - 1.0);
    cairo_stroke(cr);
    return FALSE;
}

static int set_series_enabled(AppData *app, TimeSeries *series, int requested_enabled)
{
    int enabled = requested_enabled;

    if (!app || !series) return FALSE_INT;

    series->auto_disabled_for_log = FALSE_INT;
    series->enabled = enabled;

    if (series->check_button) {
        app->updating_controls = TRUE_INT;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(series->check_button), enabled);
        app->updating_controls = FALSE_INT;
    }

    return enabled;
}

static void checkbox_toggled(GtkToggleButton *button, gpointer user_data)
{
    TimeSeries *series = (TimeSeries *)user_data;
    AppData *app = g_object_get_data(G_OBJECT(button), "app");
    GtkWidget *drawing_area = g_object_get_data(G_OBJECT(button), "drawing_area");
    int requested_enabled = gtk_toggle_button_get_active(button) ? TRUE_INT : FALSE_INT;
    if (!app || app->updating_controls) return;

    set_series_enabled(app, series, requested_enabled);

    if (app->pair_sets_check &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pair_sets_check))) {
        for (int i = 0; i < app->n_series; i++) {
            TimeSeries *candidate = &app->series[i];
            if (candidate == series) continue;
            if (candidate->source_file_index == series->source_file_index) continue;
            if (!normalized_names_equal(candidate->name, series->name)) continue;
            set_series_enabled(app, candidate, requested_enabled);
        }

        /* With Pair sets active, selecting a variable establishes its F1
           counterpart as the statistical reference.  Matching enabled series
           in F2-F4 are then treated as paired models by update_stats_panel(). */
        if (requested_enabled) {
            int file1_index = find_file1_matching_series(app, series);
            if (file1_index >= 0) {
                app->reference_series_index = file1_index;
                app->model_series_index = -1;
            }
        }
    }

    app->status_notice[0] = '\0';

    /* A paired checkbox selection can change the statistical reference.
       Refresh the Ref/Model controls immediately so the buttons show the
       same series that update_stats_panel() is using for NSE/KGE. */
    update_series_role_controls(app);
    update_status(app);
    if (drawing_area) gtk_widget_queue_draw(drawing_area);
}

static void pair_sets_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    if (!gtk_toggle_button_get_active(button)) {
        app->status_notice[0] = '\0';
        update_series_role_controls(app);
        update_status(app);
        gtk_widget_queue_draw(app->drawing_area);
        return;
    }

    if (app->reference_series_index >= 0 &&
        app->reference_series_index < app->n_series) {
        int file1_index = find_file1_matching_series(
            app, &app->series[app->reference_series_index]);
        if (file1_index >= 0) app->reference_series_index = file1_index;
    }

    /* Turning Pair sets on applies the union of all selections.  Any variable
       enabled in one file is enabled in every other file containing a
       normalized-name match.  Work from a snapshot so rejected log-axis
       selections cannot alter the union while it is being applied. */
    int *selected_snapshot = (int *)calloc((size_t)app->n_series, sizeof(int));
    if (!selected_snapshot) {
        snprintf(app->status_notice, sizeof(app->status_notice),
                 "Could not synchronize paired selections: out of memory.");
        update_status(app);
        return;
    }

    for (int i = 0; i < app->n_series; i++)
        selected_snapshot[i] = app->series[i].enabled;

    for (int i = 0; i < app->n_series; i++) {
        int should_enable = app->series[i].enabled;
        for (int j = 0; !should_enable && j < app->n_series; j++) {
            if (!selected_snapshot[j]) continue;
            if (normalized_names_equal(app->series[i].name, app->series[j].name))
                should_enable = TRUE_INT;
        }
        if (should_enable) set_series_enabled(app, &app->series[i], TRUE_INT);
    }

    free(selected_snapshot);

    snprintf(app->status_notice, sizeof(app->status_notice),
             "Pair sets enabled; matching selections synchronized across files.");

    update_series_role_controls(app);
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
}

static void select_all_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppData *app = (AppData *)user_data;

    for (int i = 0; i < app->n_series; i++)
        set_series_enabled(app, &app->series[i], TRUE_INT);

    app->status_notice[0] = '\0';
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
}

static void select_none_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppData *app = (AppData *)user_data;

    for (int i = 0; i < app->n_series; i++)
        set_series_enabled(app, &app->series[i], FALSE_INT);

    app->reference_series_index = -1;
    app->model_series_index = -1;
    app->status_notice[0] = '\0';
    update_series_role_controls(app);
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
}

static void y_range_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    if (!gtk_toggle_button_get_active(button)) return;

    if (GTK_WIDGET(button) == app->y_positive_radio)
        app->y_range_mode = Y_RANGE_POSITIVE_ONLY;
    else if (GTK_WIDGET(button) == app->y_negative_radio)
        app->y_range_mode = Y_RANGE_NEGATIVE_ONLY;
    else
        app->y_range_mode = Y_RANGE_ALL;

    app->status_notice[0] = '\0';
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
}

static void integrate_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    int requested;

    if (!app || app->updating_controls) return;
    requested = gtk_toggle_button_get_active(button) ? TRUE_INT : FALSE_INT;

    if (requested && !integrated_values_are_cached(app)) {
        if (!build_integrated_values(app)) {
            app->integrate_series = FALSE_INT;
            app->updating_controls = TRUE_INT;
            gtk_toggle_button_set_active(button, FALSE);
            app->updating_controls = FALSE_INT;
            snprintf(app->status_notice, sizeof(app->status_notice),
                     "Integrate not enabled: insufficient memory for cumulative arrays.");
            update_stats_panel(app);
            update_status(app);
            gtk_widget_queue_draw(app->drawing_area);
            return;
        }
    }

    app->integrate_series = requested;
    snprintf(app->status_notice, sizeof(app->status_notice),
             app->integrate_series
                 ? "Integrate enabled: trapezoidal cumulative integral; calendar delta-t is in hours."
                 : "Integrate disabled: plotting original values.");
    update_stats_panel(app);
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
    return;
}

static void log_y_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    int enabled = gtk_toggle_button_get_active(button) ? TRUE_INT : FALSE_INT;

    app->log_y_axis = enabled;

    if (enabled) {
        if (app->y_range_mode == Y_RANGE_NEGATIVE_ONLY) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->y_positive_radio), TRUE);
        }
        gtk_widget_set_sensitive(app->y_negative_radio, FALSE);
        snprintf(app->status_notice, sizeof(app->status_notice),
                 "Log Y axis enabled; only positive values in the displayed window are plotted.");

    }
    else {
        gtk_widget_set_sensitive(app->y_negative_radio, TRUE);
        app->status_notice[0] = '\0';
    }

    update_stats_panel(app);
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
}

static void match_variable_colors_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    app->match_variable_colors =
        gtk_toggle_button_get_active(button) ? TRUE_INT : FALSE_INT;

    for (int i = 0; i < app->n_series; i++) {
        if (app->series[i].swatch)
            gtk_widget_queue_draw(app->series[i].swatch);
    }
    gtk_widget_queue_draw(app->drawing_area);
}

static void scrollbar_changed(GtkRange *range, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    app->view_start_offset_s = gtk_range_get_value(range);
    update_status(app);
    gtk_widget_queue_draw(app->drawing_area);
}


static int compare_double_for_qsort(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}


static void format_duration_human(double seconds, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;
    if (!isfinite(seconds) || seconds <= 0.0) {
        snprintf(buf, buf_size, "unknown");
        return;
    }

    if (seconds < 60.0) {
        snprintf(buf, buf_size, "%.0f seconds", seconds);
    } else if (seconds < 3600.0) {
        double minutes = seconds / 60.0;
        if (fabs(minutes - round(minutes)) < 0.05) snprintf(buf, buf_size, "%.0f minutes", minutes);
        else snprintf(buf, buf_size, "%.1f minutes", minutes);
    } else if (seconds < 86400.0) {
        double hours = seconds / 3600.0;
        if (fabs(hours - round(hours)) < 0.05) snprintf(buf, buf_size, "%.0f hours", hours);
        else snprintf(buf, buf_size, "%.1f hours", hours);
    } else {
        double days = seconds / 86400.0;
        if (fabs(days - round(days)) < 0.05) snprintf(buf, buf_size, "%.0f days", days);
        else snprintf(buf, buf_size, "%.1f days", days);
    }
}

static const char *time_series_type_name(double median_dt_s)
{
    if (!isfinite(median_dt_s) || median_dt_s <= 0.0) return "Irregular Time Series";
    if (median_dt_s < 0.75 * 3600.0) return "Sub-hourly Time Series";
    if (median_dt_s < 1.50 * 3600.0) return "Hourly Time Series";
    if (median_dt_s < 0.75 * 86400.0) return "Sub-daily Time Series";
    if (median_dt_s < 1.50 * 86400.0) return "Daily Time Series";
    if (median_dt_s < 10.0 * 86400.0) return "Weekly Time Series";
    if (median_dt_s < 45.0 * 86400.0) return "Monthly Time Series";
    return "Irregular Time Series";
}

static double median_time_step_s(AppData *app)
{
    if (!app || app->n_records < 2) return 0.0;

    int n_delta = app->n_records - 1;
    double *dt = (double *)malloc((size_t)n_delta * sizeof(double));
    if (!dt) return 0.0;

    int n = 0;
    for (int i = 1; i < app->n_records; i++) {
        double d = (app->time_values[i] - app->time_values[i - 1]);
        if (d > 0.0 && isfinite(d)) dt[n++] = d;
    }

    if (n <= 0) {
        free(dt);
        return 0.0;
    }

    qsort(dt, (size_t)n, sizeof(double), compare_double_for_qsort);
    double median_dt = dt[n / 2];
    free(dt);
    return median_dt;
}

static void update_summary_label(AppData *app)
{
    if (!app || !app->summary_label) return;

    double dt_s = median_time_step_s(app);
    char dt_text[64];
    format_duration_human(dt_s, dt_text, sizeof(dt_text));

    char text[512];
    snprintf(text, sizeof(text),
             "%s\n"
             "Records: %d\n"
             "Variables: %d\n"
             "Time step: %s\n"
             "\n"
             "Press F1 for help",
             time_series_type_name(dt_s),
             app->n_records,
             app->n_series,
             dt_text);


    gtk_label_set_text(GTK_LABEL(app->summary_label), text);
}

static void free_integrated_values(AppData *app)
{
    if (!app || app->updating_controls) return;
    for (int s = 0; s < app->n_series; s++) {
        free(app->series[s].integrated_values);
        app->series[s].integrated_values = NULL;
    }
}

static int integrated_values_are_cached(const AppData *app)
{
    if (!app) return FALSE_INT;
    for (int s = 0; s < app->n_series; s++) {
        const TimeSeries *series = &app->series[s];
        if (series->n_records > 0 && !series->integrated_values)
            return FALSE_INT;
    }
    return TRUE_INT;
}

static int read_available_memory_bytes(size_t *available_bytes)
{
    FILE *fp;
    char line[256];
    unsigned long long kb = 0ULL;

    if (!available_bytes) return FALSE_INT;
    fp = fopen("/proc/meminfo", "r");
    if (!fp) return FALSE_INT;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
            fclose(fp);
            if (kb > (unsigned long long)SIZE_MAX / 1024ULL)
                return FALSE_INT;
            *available_bytes = (size_t)kb * 1024U;
            return TRUE_INT;
        }
    }
    fclose(fp);
    return FALSE_INT;
}

static int build_integrated_values(AppData *app)
{
    size_t required_bytes = 0U;
    size_t available_bytes = 0U;

    if (!app) return FALSE_INT;
    if (integrated_values_are_cached(app)) return TRUE_INT;

    for (int s = 0; s < app->n_series; s++) {
        const TimeSeries *series = &app->series[s];
        size_t n;
        size_t bytes;

        if (series->n_records <= 0) continue;
        n = (size_t)series->n_records;
        if (n > SIZE_MAX / sizeof(double)) return FALSE_INT;
        bytes = n * sizeof(double);
        if (required_bytes > SIZE_MAX - bytes) return FALSE_INT;
        required_bytes += bytes;
    }

    /* Keep 20 percent of currently available memory in reserve for GTK,
       Cairo, existing input arrays, and other processes.  malloc() is also
       checked because available memory can change after this estimate. */
    if (read_available_memory_bytes(&available_bytes) &&
        required_bytes > available_bytes - available_bytes / 5U) {
        fprintf(stderr,
                "Integration cache requires %.1f MiB, but only %.1f MiB "
                "is currently available.\n",
                (double)required_bytes / (1024.0 * 1024.0),
                (double)available_bytes / (1024.0 * 1024.0));
        return FALSE_INT;
    }

    free_integrated_values(app);
    for (int s = 0; s < app->n_series; s++) {
        TimeSeries *series = &app->series[s];
        double cumulative = 0.0;

        if (series->n_records <= 0) continue;
        series->integrated_values =
            (double *)malloc((size_t)series->n_records * sizeof(double));
        if (!series->integrated_values) {
            free_integrated_values(app);
            return FALSE_INT;
        }

        series->integrated_values[0] = 0.0;
        for (int i = 1; i < series->n_records; i++) {
            double v0 = series->values[i - 1];
            double v1 = series->values[i];
            double dt = series->time_values[i] - series->time_values[i - 1];

            if (app->axis_type == AXIS_TYPE_CALENDAR_TIME) dt /= 3600.0;

            if (!isfinite(v0) || !isfinite(v1) || !isfinite(dt) || dt <= 0.0) {
                series->integrated_values[i] = NAN;
                continue;
            }

            cumulative += 0.5 * (v0 + v1) * dt;
            series->integrated_values[i] = cumulative;
        }
    }
    return TRUE_INT;
}

static double series_display_value(const AppData *app,
                                   const TimeSeries *series, int index)
{
    if (!app || !series || index < 0 || index >= series->n_records) return NAN;
    if (app->integrate_series && series->integrated_values)
        return series->integrated_values[index];
    return series->values[index];
}

static int visible_index_range(AppData *app, double t0, double t1, int *i0_out, int *i1_out)
{
    int i0 = 0;
    while (i0 < app->n_records && app->time_values[i0] < t0) i0++;
    int i1 = i0;
    while (i1 < app->n_records && app->time_values[i1] <= t1) i1++;
    if (i0 > 0) i0--;
    if (i1 < app->n_records) i1++;
    if (i1 > app->n_records) i1 = app->n_records;
    *i0_out = i0;
    *i1_out = i1;
    return i1 > i0;
}

static int compute_visible_minmax(AppData *app, int i0, int i1,
                                  double *min_out, double *max_out)
{
    double ymin = HUGE_VAL;
    double ymax = -HUGE_VAL;

    double t0 = app->first_time + app->view_start_offset_s;
    double t1 = t0 + app->view_duration_s;
    (void)i0;
    (void)i1;
    for (int s = 0; s < app->n_series; s++) {
        TimeSeries *series = &app->series[s];
        int si0 = 0, si1 = 0;
        if (!series->enabled) continue;
        if (!series_index_range(series, t0, t1, &si0, &si1)) continue;
        for (int i = si0; i < si1; i++) {
            double value = series_display_value(app, series, i);
            if (!value_is_visible_for_y_mode(app, value)) continue;
            if (value < ymin) ymin = value;
            if (value > ymax) ymax = value;
        }
    }

    if (ymin == HUGE_VAL || ymax == -HUGE_VAL) return FALSE_INT;

    if (app->log_y_axis) {
        if (ymin <= 0.0 || ymax <= 0.0) return FALSE_INT;
        if (fabs(log10(ymax) - log10(ymin)) < 1.0e-12) {
            ymin /= 10.0;
            ymax *= 10.0;
        }
    }
    else if (app->y_range_mode == Y_RANGE_POSITIVE_ONLY || ymin >= 0.0) {
        ymin = 0.0;
        if (ymax <= 0.0) ymax = 1.0;
        else ymax *= 1.05;
    }
    else if (app->y_range_mode == Y_RANGE_NEGATIVE_ONLY || ymax <= 0.0) {
        ymax = 0.0;
        if (ymin >= 0.0) ymin = -1.0;
        else ymin *= 1.05;
    }
    else if (fabs(ymax - ymin) < 1.0e-12) {
        ymin -= 1.0;
        ymax += 1.0;
    }
    else {
        double pad = 0.05 * (ymax - ymin);
        ymin -= pad;
        ymax += pad;
    }

    *min_out = ymin;
    *max_out = ymax;
    return TRUE_INT;
}

static double y_value_to_screen(const AppData *app, double value,
                                double ymin, double ymax,
                                double y0, double y1)
{
    double fraction;
    if (app->log_y_axis) {
        fraction = (log10(value) - log10(ymin)) /
                   (log10(ymax) - log10(ymin));
    }
    else {
        fraction = (value - ymin) / (ymax - ymin);
    }
    return y1 - fraction * (y1 - y0);
}

static void draw_text(cairo_t *cr, double x, double y, const char *text, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

static gboolean draw_plot(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double W = alloc.width;
    double H = alloc.height;
    double x0 = LEFT_MARGIN;
    double x1 = W - RIGHT_MARGIN;
    double y0 = TOP_MARGIN;
    double y1 = H - BOTTOM_MARGIN;
    if (x1 <= x0 || y1 <= y0) return FALSE;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);

    /* Identify the input files in the otherwise unused header area. */
    for (int f = 0; f < app->n_files; f++) {
        char file_text[1024];
        snprintf(file_text, sizeof(file_text), "F%d: %s", f + 1,
                 app->filenames[f] ? app->filenames[f] : "");
        draw_text(cr, LEFT_MARGIN + 10.0, 16.0 + 15.0 * (double)f,
                  file_text, 0.9, 0.9, 0.9);
    }

    double t_start = app->first_time + app->view_start_offset_s;
    double t_end = t_start + app->view_duration_s;
    int i0 = 0, i1 = 0;
    visible_index_range(app, t_start, t_end, &i0, &i1);

    double ymin = 0.0, ymax = 1.0;
    int have_minmax = compute_visible_minmax(app, i0, i1, &ymin, &ymax);

    double tick_spacing = 1.0;
    double ytick_min = ymin;
    double ytick_max = ymax;
    int tick_decimals = 0;
    int log_exp_min = 0;
    int log_exp_max = 0;

    if (have_minmax && app->log_y_axis) {
        /* Keep the ordinate limits tied to the actual positive values in the
           visible window.  The exponent bounds are used only to place log
           grid lines and labels; do not expand the data range to whole
           decades, which can severely compress closely spaced series. */
        log_exp_min = (int)floor(log10(ymin));
        log_exp_max = (int)ceil(log10(ymax));
    }
    else if (have_minmax) {
        tick_spacing = nice_tick_spacing(ymax - ymin, 6);
        ytick_min = floor(ymin / tick_spacing) * tick_spacing;
        ytick_max = ceil(ymax / tick_spacing) * tick_spacing;
        if (app->y_range_mode == Y_RANGE_POSITIVE_ONLY || ymin >= 0.0)
            ytick_min = 0.0;
        if (app->y_range_mode == Y_RANGE_NEGATIVE_ONLY || ymax <= 0.0)
            ytick_max = 0.0;
        if (ytick_max > ytick_min) {
            ymin = ytick_min;
            ymax = ytick_max;
        }
        tick_decimals = decimals_for_spacing(tick_spacing);
    }

    double view_span_s = t_end - t_start;
    int x_tick_spacing_s = 0;
    double x_tick_spacing = 0.0;
    if (app->axis_type == AXIS_TYPE_CALENDAR_TIME) {
        x_tick_spacing_s = nice_time_tick_spacing(view_span_s);
    } else {
        double raw = view_span_s / 6.0;
        double exponent = floor(log10(raw));
        double scale = pow(10.0, exponent);
        static const double x_steps[] = {1.0, 2.0, 2.5, 5.0, 10.0};
        x_tick_spacing = 10.0 * scale;
        for (int xs = 0; xs < 5; xs++) {
            double candidate = x_steps[xs] * scale;
            if (candidate >= raw) { x_tick_spacing = candidate; break; }
        }
    }

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    if (view_span_s > 0.0) {
        if (app->axis_type == AXIS_TYPE_CALENDAR_TIME) {
            time_t tick = first_regular_tick((time_t)llround(t_start), x_tick_spacing_s);
            for (; (double)tick <= t_end; tick += x_tick_spacing_s) {
                double x = x0 + ((double)tick - t_start) / view_span_s * (x1 - x0);
                cairo_move_to(cr, x, y0);
                cairo_line_to(cr, x, y1);
            }
        } else if (x_tick_spacing > 0.0) {
            double tick = ceil(t_start / x_tick_spacing) * x_tick_spacing;
            for (; tick <= t_end + 0.5 * x_tick_spacing; tick += x_tick_spacing) {
                double x = x0 + (tick - t_start) / view_span_s * (x1 - x0);
                cairo_move_to(cr, x, y0);
                cairo_line_to(cr, x, y1);
            }
        }
    }
    if (have_minmax && app->log_y_axis) {
        for (int exponent = log_exp_min; exponent <= log_exp_max; exponent++) {
            double decade = pow(10.0, (double)exponent);
            for (int multiplier = 1; multiplier <= 9; multiplier++) {
                double value = multiplier * decade;
                if (value < ymin || value > ymax) continue;
                double y = y_value_to_screen(app, value, ymin, ymax, y0, y1);
                cairo_move_to(cr, x0, y);
                cairo_line_to(cr, x1, y);
            }
        }
    }
    else if (have_minmax) {
        for (double value = ytick_min;
             value <= ytick_max + 0.5 * tick_spacing;
             value += tick_spacing) {
            double y = y_value_to_screen(app, value, ymin, ymax, y0, y1);
            cairo_move_to(cr, x0, y);
            cairo_line_to(cr, x1, y);
        }
    }
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_stroke(cr);

    char label[128];
    if (have_minmax && app->log_y_axis) {
        for (int exponent = log_exp_min; exponent <= log_exp_max; exponent++) {
            double value = pow(10.0, (double)exponent);
            if (value < ymin || value > ymax) continue;
            double y = y_value_to_screen(app, value, ymin, ymax, y0, y1);
            if (exponent >= -3 && exponent <= 4)
                snprintf(label, sizeof(label), "%.6g", value);
            else
                snprintf(label, sizeof(label), "1e%d", exponent);
            draw_text(cr, 8, y + 4, label, 0.9, 0.9, 0.9);
        }
    }
    else if (have_minmax) {
        for (double value = ytick_min;
             value <= ytick_max + 0.5 * tick_spacing;
             value += tick_spacing) {
            double y = y_value_to_screen(app, value, ymin, ymax, y0, y1);
            if (tick_decimals == 0) snprintf(label, sizeof(label), "%.0f", value);
            else snprintf(label, sizeof(label), "%.*f", tick_decimals, value);
            draw_text(cr, 8, y + 4, label, 0.9, 0.9, 0.9);
        }
    }

    if (view_span_s > 0.0) {
        if (app->axis_type == AXIS_TYPE_CALENDAR_TIME) {
            time_t tick = first_regular_tick((time_t)llround(t_start), x_tick_spacing_s);
            for (; (double)tick <= t_end; tick += x_tick_spacing_s) {
                char time_txt[32];
                char date_txt[32];
                format_x_tick(tick, time_txt, sizeof(time_txt), date_txt, sizeof(date_txt));
                double x = x0 + ((double)tick - t_start) / view_span_s * (x1 - x0);
                cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                cairo_move_to(cr, x, y1);
                cairo_line_to(cr, x, y1 + 5);
                cairo_stroke(cr);
                draw_text(cr, x - 18, H - 33, time_txt, 0.9, 0.9, 0.9);
                draw_text(cr, x - 34, H - 18, date_txt, 0.75, 0.75, 0.75);
            }
        } else if (x_tick_spacing > 0.0) {
            int x_decimals = decimals_for_spacing(x_tick_spacing);
            double tick = ceil(t_start / x_tick_spacing) * x_tick_spacing;
            for (; tick <= t_end + 0.5 * x_tick_spacing; tick += x_tick_spacing) {
                char x_txt[64];
                if (x_decimals <= 6) snprintf(x_txt, sizeof(x_txt), "%.*f", x_decimals, tick);
                else snprintf(x_txt, sizeof(x_txt), "%.8g", tick);
                double x = x0 + (tick - t_start) / view_span_s * (x1 - x0);
                cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                cairo_move_to(cr, x, y1);
                cairo_line_to(cr, x, y1 + 5);
                cairo_stroke(cr);
                draw_text(cr, x - 22, H - 22, x_txt, 0.9, 0.9, 0.9);
            }
        }
    }

    if (!have_minmax) {
        draw_text(cr, x0 + 20, y0 + 30,
                  app->log_y_axis
                      ? "No enabled positive finite values in this window."
                      : "No enabled finite values in this Y range.",
                  1.0, 1.0, 1.0);
        return FALSE;
    }

    cairo_save(cr);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_clip(cr);

    if (app->selection_active) {
        double sa, sb;
        ordered_selection_times(app, &sa, &sb);
        double denom = (t_end - t_start);
        if (denom > 0.0) {
            double xa = x0 + (sa - t_start) / denom * (x1 - x0);
            double xb = x0 + (sb - t_start) / denom * (x1 - x0);
            if (xb < x0 || xa > x1) {
                /* selection is outside current view */
            } else {
                if (xa < x0) xa = x0;
                if (xb > x1) xb = x1;
                cairo_set_source_rgba(cr, 0.8, 0.8, 1.0, 0.18);
                cairo_rectangle(cr, xa, y0, xb - xa, y1 - y0);
                cairo_fill(cr);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.65);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, xa, y0);
                cairo_line_to(cr, xa, y1);
                cairo_move_to(cr, xb, y0);
                cairo_line_to(cr, xb, y1);
                cairo_stroke(cr);
            }
        }
    }

    for (int s = 0; s < app->n_series; s++) {
        TimeSeries *series = &app->series[s];
        int si0 = 0, si1 = 0;
        if (!series->enabled) continue;
        if (!series_index_range(series, t_start, t_end, &si0, &si1)) continue;
        double series_red, series_green, series_blue;
        get_series_display_color(app, series, &series_red, &series_green, &series_blue);
        cairo_set_source_rgb(cr, series_red, series_green, series_blue);
        cairo_set_line_width(cr, 1.4);
        apply_file_line_style(cr, series->source_file_index);
        int pen_down = FALSE_INT;
        double series_gap_threshold_s = 0.0;
        /* A generic numeric abscissa has no inferred sampling cadence.
           Connect consecutive records exactly as supplied.  Automatic gap
           detection is meaningful only for recognized calendar time data.

           Cache the threshold after computing it once.  The previous code
           bubble-sorted every positive time increment during every redraw,
           which made drawing O(n^2) and caused multi-second GUI delays. */
        if (app->axis_type == AXIS_TYPE_CALENDAR_TIME && series->n_records > 1) {
            if (!series->gap_threshold_computed) {
                double *dts = malloc((size_t)(series->n_records - 1) * sizeof(double));
                series->gap_threshold_s = 0.0;
                if (dts) {
                    int ndt = 0;
                    for (int k = 1; k < series->n_records; k++) {
                        double dt = series->time_values[k] - series->time_values[k - 1];
                        if (dt > 0.0) dts[ndt++] = dt;
                    }
                    if (ndt > 0) {
                        qsort(dts, (size_t)ndt, sizeof(double),
                              compare_double_for_qsort);
                        series->gap_threshold_s = 1.5 * dts[ndt / 2];
                    }
                    free(dts);
                }
                series->gap_threshold_computed = TRUE_INT;
            }
            series_gap_threshold_s = series->gap_threshold_s;
        }
        for (int i = si0; i < si1; i++) {
            double v = series_display_value(app, series, i);
            if (!value_is_visible_for_y_mode(app, v)) {
                pen_down = FALSE_INT;
                continue;
            }
            if (i > 0 && series_gap_threshold_s > 0.0) {
                double dt_s = (series->time_values[i] - series->time_values[i - 1]);
                if (dt_s > series_gap_threshold_s) pen_down = FALSE_INT;
            }
            double tx = (series->time_values[i] - t_start) / (t_end - t_start);
            double x = x0 + tx * (x1 - x0);
            double y = y_value_to_screen(app, v, ymin, ymax, y0, y1);
            if (!pen_down) {
                cairo_move_to(cr, x, y);
                pen_down = TRUE_INT;
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);
    }

    if (app->mouse_inside && app->mouse_x >= x0 && app->mouse_x <= x1 && app->mouse_y >= y0 && app->mouse_y <= y1) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.45);
        cairo_set_line_width(cr, 0.8);
        cairo_move_to(cr, app->mouse_x, y0);
        cairo_line_to(cr, app->mouse_x, y1);
        cairo_move_to(cr, x0, app->mouse_y);
        cairo_line_to(cr, x1, app->mouse_y);
        cairo_stroke(cr);
    }
    cairo_restore(cr);

    double legend_x = x1 - 180;
    double legend_y = y0 + 18;
    int legend_count = 0;
    for (int s = 0; s < app->n_series; s++) {
        TimeSeries *series = &app->series[s];
        if (!series->enabled) continue;
        double yy = legend_y + legend_count * 16.0;
        double series_red, series_green, series_blue;
        get_series_display_color(app, series, &series_red, &series_green, &series_blue);
        cairo_set_source_rgb(cr, series_red, series_green, series_blue);
        apply_file_line_style(cr, series->source_file_index);
        cairo_move_to(cr, legend_x, yy - 4);
        cairo_line_to(cr, legend_x + 24, yy - 4);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);
        snprintf(label, sizeof(label), "F%d: %s%s%s", series->source_file_index + 1, series->name, (series->units && series->units[0]) ? " (" : "", (series->units && series->units[0]) ? series->units : "");
        if (series->units && series->units[0]) {
            size_t len = strlen(label);
            if (len < sizeof(label) - 2) strcat(label, ")");
        }
        draw_text(cr, legend_x + 32, yy, label, series_red, series_green, series_blue);
        legend_count++;
        if (legend_count > 12) break;
    }

    return FALSE;
}

static gboolean button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    if (!event) return FALSE;

    if (event->button == 3) {
        reset_view_to_full_record(app);
        return TRUE;
    }

    if (event->button != 1) return FALSE;
    if (!point_inside_plot(widget, event->x, event->y)) return FALSE;

    app->is_dragging_selection = TRUE_INT;
    app->selection_active = TRUE_INT;
    app->selection_t0 = time_from_plot_x(app, widget, event->x);
    app->selection_t1 = app->selection_t0;
    app->mouse_inside = TRUE_INT;
    app->mouse_x = event->x;
    app->mouse_y = event->y;
    update_status(app);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    if (!event || event->button != 1) return FALSE;
    if (!app->is_dragging_selection) return FALSE;

    app->selection_t1 = time_from_plot_x(app, widget, event->x);
    app->is_dragging_selection = FALSE_INT;

    double sa, sb;
    ordered_selection_times(app, &sa, &sb);
    double selected_s = (sb - sa);
    double minimum_selection = (app->axis_type == AXIS_TYPE_CALENDAR_TIME) ? 60.0
                             : (app->last_time - app->first_time) * 1.0e-9;
    if (selected_s < minimum_selection) {
        app->selection_active = FALSE_INT;
    } else {
        app->selection_active = TRUE_INT;
        app->selection_t0 = sa;
        app->selection_t1 = sb;
        set_view_window(app, sa, sb);
    }

    update_status(app);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    app->mouse_inside = TRUE_INT;
    app->mouse_x = event->x;
    app->mouse_y = event->y;
    if (app->is_dragging_selection) {
        app->selection_t1 = time_from_plot_x(app, widget, event->x);
        app->selection_active = TRUE_INT;
    }
    update_status(app);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    GtkAllocation alloc;
    double x0;
    double x1;
    double cursor_fraction;
    double cursor_time;
    double zoom_factor;
    double total_span;
    double new_duration;
    double new_start;
    double new_end;

    if (!app || !event || app->n_records <= 0) return FALSE;
    if (!point_inside_plot(widget, event->x, event->y)) return FALSE;

    if (event->direction == GDK_SCROLL_UP) {
        zoom_factor = 0.80;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        zoom_factor = 1.25;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        if (event->delta_y < 0.0) {
            zoom_factor = 0.80;
        } else if (event->delta_y > 0.0) {
            zoom_factor = 1.25;
        } else {
            return TRUE;
        }
    } else {
        return FALSE;
    }

    total_span = app->last_time - app->first_time;
    if (total_span <= 0.0) return TRUE;

    if (zoom_factor > 1.0 && app->view_duration_s >= total_span) return TRUE;

    gtk_widget_get_allocation(widget, &alloc);
    x0 = LEFT_MARGIN;
    x1 = alloc.width - RIGHT_MARGIN;
    if (x1 <= x0) return TRUE;

    cursor_fraction = clamp_double((event->x - x0) / (x1 - x0), 0.0, 1.0);
    cursor_time = app->first_time + app->view_start_offset_s +
                  cursor_fraction * app->view_duration_s;

    new_duration = app->view_duration_s * zoom_factor;
    if (new_duration > total_span) new_duration = total_span;

    new_start = cursor_time - cursor_fraction * new_duration;
    new_end = new_start + new_duration;

    if (new_start < app->first_time) {
        new_start = app->first_time;
        new_end = new_start + new_duration;
    }
    if (new_end > app->last_time) {
        new_end = app->last_time;
        new_start = new_end - new_duration;
    }

    app->selection_active = FALSE_INT;
    app->is_dragging_selection = FALSE_INT;
    set_view_window(app, new_start, new_end);

    app->mouse_inside = TRUE_INT;
    app->mouse_x = event->x;
    app->mouse_y = event->y;
    update_status(app);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
    (void)event;
    AppData *app = (AppData *)user_data;
    app->mouse_inside = FALSE_INT;
    update_status(app);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static void update_series_labels(AppData *app)
{
    if (!app) return;
    for (int i = 0; i < app->n_series; i++) {
        TimeSeries *series = &app->series[i];
        char label_text[512];
        if (!series->label) continue;
        if (series->units && series->units[0])
            snprintf(label_text, sizeof(label_text), "F%d: %s (%s)",
                     series->source_file_index + 1, series->name, series->units);
        else
            snprintf(label_text, sizeof(label_text), "F%d: %s",
                     series->source_file_index + 1, series->name);
        gtk_label_set_text(GTK_LABEL(series->label), label_text);
    }
}

static int find_file1_matching_series(const AppData *app, const TimeSeries *series)
{
    if (!app || !series) return -1;
    for (int i = 0; i < app->n_series; i++) {
        if (app->series[i].source_file_index != 0) continue;
        if (normalized_names_equal(app->series[i].name, series->name)) return i;
    }
    return -1;
}

static void update_series_role_controls(AppData *app)
{
    int pair_sets_active;

    if (!app) return;
    pair_sets_active = (app->pair_sets_check &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pair_sets_check)))
        ? TRUE_INT : FALSE_INT;

    app->updating_controls = TRUE_INT;

    for (int i = 0; i < app->n_series; i++) {
        TimeSeries *series = &app->series[i];

        if (series->reference_button) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(series->reference_button),
                                         i == app->reference_series_index);
        }

        if (series->model_button) {
            int paired_model_active = FALSE_INT;

            if (pair_sets_active &&
                app->reference_series_index >= 0 &&
                app->reference_series_index < app->n_series &&
                series->source_file_index > 0 &&
                series->enabled &&
                normalized_names_equal(
                    series->name,
                    app->series[app->reference_series_index].name)) {
                paired_model_active = TRUE_INT;
            }

            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(series->model_button),
                                         pair_sets_active
                                         ? paired_model_active
                                         : i == app->model_series_index);
            gtk_widget_set_sensitive(series->model_button, TRUE);
        }
    }

    app->updating_controls = FALSE_INT;
}

static void reference_button_toggled(GtkToggleButton *button, gpointer user_data)
{
    TimeSeries *series = (TimeSeries *)user_data;
    AppData *app = g_object_get_data(G_OBJECT(button), "app");
    int index = -1;

    if (!app || app->updating_controls) return;
    for (int i = 0; i < app->n_series; i++) {
        if (&app->series[i] == series) {
            index = i;
            break;
        }
    }
    if (index < 0) return;

    if (gtk_toggle_button_get_active(button)) {
        if (app->pair_sets_check &&
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pair_sets_check))) {
            int file1_index = find_file1_matching_series(app, series);
            if (file1_index >= 0) index = file1_index;
        }
        app->reference_series_index = index;
        if (app->model_series_index == index) app->model_series_index = -1;
    }
    else if (app->reference_series_index == index) {
        app->reference_series_index = -1;
    }

    update_series_role_controls(app);
    update_status(app);
}

static void model_button_toggled(GtkToggleButton *button, gpointer user_data)
{
    TimeSeries *series = (TimeSeries *)user_data;
    AppData *app = g_object_get_data(G_OBJECT(button), "app");
    int index = -1;

    if (!app || app->updating_controls) return;
    if (app->pair_sets_check &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pair_sets_check))) {
        update_series_role_controls(app);
        update_status(app);
        return;
    }

    for (int i = 0; i < app->n_series; i++) {
        if (&app->series[i] == series) {
            index = i;
            break;
        }
    }
    if (index < 0) return;

    if (gtk_toggle_button_get_active(button)) {
        app->model_series_index = index;
        if (app->reference_series_index == index) app->reference_series_index = -1;
    }
    else if (app->model_series_index == index) {
        app->model_series_index = -1;
    }

    update_series_role_controls(app);
    update_status(app);
}

static void build_series_panel(AppData *app)
{
    GtkWidget *heading;

    heading = gtk_label_new("Plot     Variable                         Statistics");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_box_pack_start(GTK_BOX(app->series_box), heading, FALSE, FALSE, 2);

    for (int i = 0; i < app->n_series; i++) {
        TimeSeries *series = &app->series[i];
        GtkWidget *row_event_box = gtk_event_box_new();
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *check = gtk_check_button_new();
        GtkWidget *swatch = gtk_drawing_area_new();
        GtkWidget *reference_button = gtk_toggle_button_new_with_label("Ref");
        GtkWidget *model_button = gtk_toggle_button_new_with_label("Model");
        char label_text[256];

        if (series->units && series->units[0])
            snprintf(label_text, sizeof(label_text), "F%d: %s (%s)",
                     series->source_file_index + 1, series->name, series->units);
        else
            snprintf(label_text, sizeof(label_text), "F%d: %s",
                     series->source_file_index + 1, series->name);

        GtkWidget *label = gtk_label_new(label_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
        series->label = label;
        series->row_event_box = row_event_box;
        series->reference_button = reference_button;
        series->model_button = model_button;
        g_object_set_data(G_OBJECT(row_event_box), "app", app);

        gtk_widget_set_size_request(swatch, 16, 16);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), series->enabled);
        series->check_button = check;
        series->swatch = swatch;
        g_object_set_data(G_OBJECT(swatch), "app", app);
        g_object_set_data(G_OBJECT(check), "drawing_area", app->drawing_area);
        g_object_set_data(G_OBJECT(check), "app", app);
        g_object_set_data(G_OBJECT(reference_button), "app", app);
        g_object_set_data(G_OBJECT(model_button), "app", app);

        g_signal_connect(check, "toggled", G_CALLBACK(checkbox_toggled), series);
        g_signal_connect(swatch, "draw", G_CALLBACK(draw_swatch), series);
        g_signal_connect(reference_button, "toggled",
                         G_CALLBACK(reference_button_toggled), series);
        g_signal_connect(model_button, "toggled",
                         G_CALLBACK(model_button_toggled), series);

        gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), swatch, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), reference_button, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), model_button, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(row_event_box), row);
        gtk_box_pack_start(GTK_BOX(app->series_box), row_event_box, FALSE, FALSE, 2);
    }
    update_series_labels(app);
    update_series_role_controls(app);
}

static void help_window_destroyed(GtkWidget *widget, gpointer user_data)
{
    AppData *app = (AppData *)user_data;
    (void)widget;
    if (app) app->help_window = NULL;
}

static gboolean help_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)user_data;
    if (!event) return FALSE;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(widget);
        return TRUE;
    }
    return FALSE;
}

static void show_help_window(AppData *app)
{
    static const char help_text[] =
        "tsviewer " TSVIEWER_VERSION " - Interactive Scientific Time-Series Viewer\n\n"
        "COMMAND LINE\n"
        "  tsviewer file1 [file2] [file3] [file4]\n\n"
        "PLOTTING SERIES\n"
        "  Use the check button beside each variable to show or hide it.\n"
        "  'Select all' and 'Select none' change all plotted series at once.\n"
        "  If more than one file is opened and they have common series labels\n"
        "  then 'Pair sets' causes selection of like sets in each file.\n"
        "  Using 'Select none' and 'Pair sets' allows investigation of selected\n"
        "  series one at a time.  With 'Pair sets' active, newly selected series\n"
        "  assumes that the selected series in file one is the reference.\n"
        "  Color identifies a variable; line style identifies the source file.\n"
        "  'Match variable colors' uses same line colors to denote matching\n"
        "  series in each input file.   Line styles are different for each file\n"
        "  deselecting 'Match variable colors' can help disambiguate.\n\n"
        "NAVIGATION\n"
        "  Mouse wheel       Zoom in or out, centered on the cursor.\n"
        "  Left-click/drag   Select an interval and zoom to it.\n"
        "  Scroll bar        Pan horizontally after zooming.\n"
        "  Right-click       Reset to the full data record.\n"
        "  R                 Reset to the full data record.\n"
        "  F1                Open this help window.\n"
        "  Esc               Quit tsviewer.\n\n"
        "REFERENCE AND MODEL STATISTICS\n"
        "  Select one series as Ref and a different series as Model.\n"
        "  Statistics are calculated over the visible interval, or over the\n"
        "  selected interval while a selection is active.\n"
        "  NSE and KGE require exactly matching coordinates; no interpolation\n"
        "  or hidden alteration of the input data is performed.\n\n"
        "PAIR SETS\n"
        "  With multiple files loaded, Pair sets synchronizes variables having\n"
        "  matching names. File 1 supplies the statistical reference. Matching\n"
        "  selected series in Files 2-4 are evaluated as models. The Ref and\n"
        "  Model buttons show exactly which series participate in the comparison.\n\n"
        "LOG Y AXIS\n"
        "  Log Y axis plots positive values using log10 scaling. When Ref and\n"
        "  Model are selected, NSE_log(x) and KGE_log(x) are calculated from\n"
        "  log10-transformed values. This calculation is performed only if both\n"
        "  series are everywhere strictly greater than zero over the comparison\n"
        "  interval. No epsilon or offset is added to the data.\n\n"
        "Y RANGE\n"
        "  All, Positive only, and Negative only control which values determine\n"
        "  the displayed Y range. Negative-only display is unavailable with a\n"
        "  logarithmic Y axis.\n\n"
        "INTEGRATE\n"
        "  Integrate displays a cumulative trapezoidal integral. For calendar\n"
        "  time series, time increments are expressed in hours.\n\n"
        "INPUT DATA\n"
        "  tsviewer accepts common columnar scientific ASCII data, including\n"
        "  comma-, tab-, pipe-, and whitespace-delimited files, Campbell\n"
        "  Scientific TOA5 data, AmeriFlux timestamps, Julian Date, Modified\n"
        "  Julian Date, Unix time, common calendar timestamps, year/month and\n"
        "  year-only summaries, and generic numeric abscissas.\n\n"
        "  Press Esc to close this help window.\n\n"
        "  tsviewer, (C) 2026 Fred L. Ogden\n"
        "  Licensed under the Apache License, version 2.0.\n";
        
    GtkAllocation allocation;
    GtkWidget *scrolled;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    int help_width = 400;
    int help_height = 500;

    if (!app) return;
    if (app->help_window) {
        gtk_window_present(GTK_WINDOW(app->help_window));
        return;
    }

    if (app->drawing_area) {
        gtk_widget_get_allocation(app->drawing_area, &allocation);
        if (allocation.width > 0) help_width = allocation.width / 3;
        if (allocation.height > 0) help_height = (int)(0.80 * (double)allocation.height);
    }
    app->help_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->help_window), "tsviewer Help");
    gtk_window_set_transient_for(GTK_WINDOW(app->help_window), GTK_WINDOW(app->window));
    gtk_window_set_default_size(GTK_WINDOW(app->help_window), help_width, help_height);
    gtk_window_set_position(GTK_WINDOW(app->help_window), GTK_WIN_POS_CENTER_ON_PARENT);
    g_signal_connect(app->help_window, "destroy",
                     G_CALLBACK(help_window_destroyed), app);
    g_signal_connect(app->help_window, "key-press-event",
                     G_CALLBACK(help_key_press), app);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 8);
    gtk_container_add(GTK_CONTAINER(app->help_window), scrolled);

    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 10);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, help_text, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), text_view);

    gtk_widget_show_all(app->help_window);
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)widget;
    AppData *app = (AppData *)user_data;
    if (!event) return FALSE;
    if (event->keyval == GDK_KEY_F1) {
        show_help_window(app);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    if (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R) {
        reset_view_to_full_record(app);
        return TRUE;
    }
    return FALSE;
}

static void build_gui(AppData *app)
{
    int window_width = 1100;
    int window_height = 650;

    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "tsviewer " TSVIEWER_VERSION);

#if GTK_CHECK_VERSION(3, 22, 0)
    {
        GdkDisplay *display = gdk_display_get_default();
        GdkMonitor *monitor = NULL;

        if (display) monitor = gdk_display_get_primary_monitor(display);
        if (monitor) {
            GdkRectangle workarea;
            gdk_monitor_get_workarea(monitor, &workarea);
            window_width = (int)(0.90 * (double)workarea.width);
            window_height = (int)(0.75 * (double)workarea.height);
        }
    }
#else
    {
        GdkScreen *screen = gdk_screen_get_default();
        if (screen) {
            window_width = (int)(0.90 * (double)gdk_screen_get_width(screen));
            window_height = (int)(0.75 * (double)gdk_screen_get_height(screen));
        }
    }
#endif

    gtk_window_set_default_size(GTK_WINDOW(app->window),
                                window_width, window_height);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(key_press), app);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), main_box, TRUE, TRUE, 0);

    GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(left_panel, 390, -1);
    gtk_container_set_border_width(GTK_CONTAINER(left_panel), 8);
    gtk_box_pack_start(GTK_BOX(main_box), left_panel, FALSE, FALSE, 0);

    app->summary_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->summary_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(app->summary_label), 0.0);
    gtk_box_pack_start(GTK_BOX(left_panel), app->summary_label, FALSE, FALSE, 0);
    update_summary_label(app);

    GtkWidget *control_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *y_frame = gtk_frame_new("Y range");
    GtkWidget *y_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(y_box), 4);
    gtk_container_add(GTK_CONTAINER(y_frame), y_box);

    app->y_all_radio = gtk_radio_button_new_with_label(NULL, "All");
    GSList *y_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app->y_all_radio));
    app->y_positive_radio = gtk_radio_button_new_with_label(y_group, "Positive only");
    y_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app->y_positive_radio));
    app->y_negative_radio = gtk_radio_button_new_with_label(y_group, "Negative only");
    app->log_y_check = gtk_check_button_new_with_label("Log Y axis");
    app->integrate_check = gtk_check_button_new_with_label("Integrate");

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->y_all_radio), TRUE);
    gtk_box_pack_start(GTK_BOX(y_box), app->y_all_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(y_box), app->y_positive_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(y_box), app->y_negative_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(y_box), app->log_y_check, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(y_box), app->integrate_check, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(control_row), y_frame, TRUE, TRUE, 0);

    GtkWidget *selection_frame = gtk_frame_new("Set selection");
    GtkWidget *selection_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *select_all_button = gtk_button_new_with_label("Select all");
    GtkWidget *select_none_button = gtk_button_new_with_label("Select none");
    app->pair_sets_check = gtk_check_button_new_with_label("Pair sets");
    app->match_variable_colors_check =
        gtk_check_button_new_with_label("Match variable colors");
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(app->match_variable_colors_check), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(selection_box), 4);
    gtk_container_add(GTK_CONTAINER(selection_frame), selection_box);
    gtk_box_pack_start(GTK_BOX(selection_box), select_all_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(selection_box), select_none_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(selection_box), app->pair_sets_check, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(selection_box),
                       app->match_variable_colors_check, FALSE, FALSE, 2);
    gtk_widget_set_sensitive(app->pair_sets_check, app->n_files > 1);
    gtk_box_pack_start(GTK_BOX(control_row), selection_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_panel), control_row, FALSE, FALSE, 0);

    g_signal_connect(select_all_button, "clicked", G_CALLBACK(select_all_clicked), app);
    g_signal_connect(select_none_button, "clicked", G_CALLBACK(select_none_clicked), app);
    g_signal_connect(app->pair_sets_check, "toggled",
                     G_CALLBACK(pair_sets_toggled), app);
    g_signal_connect(app->y_all_radio, "toggled", G_CALLBACK(y_range_toggled), app);
    g_signal_connect(app->y_positive_radio, "toggled", G_CALLBACK(y_range_toggled), app);
    g_signal_connect(app->y_negative_radio, "toggled", G_CALLBACK(y_range_toggled), app);
    g_signal_connect(app->log_y_check, "toggled", G_CALLBACK(log_y_toggled), app);
    g_signal_connect(app->integrate_check, "toggled", G_CALLBACK(integrate_toggled), app);
    g_signal_connect(app->match_variable_colors_check, "toggled",
                     G_CALLBACK(match_variable_colors_toggled), app);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(left_panel), scrolled, TRUE, TRUE, 0);

    app->series_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(scrolled), app->series_box);

    app->stats_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->stats_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(app->stats_label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(app->stats_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app->stats_label), 48);
    gtk_label_set_line_wrap(GTK_LABEL(app->stats_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(app->stats_label), PANGO_WRAP_WORD_CHAR);
    PangoAttrList *stats_attributes = pango_attr_list_new();
    pango_attr_list_insert(stats_attributes, pango_attr_family_new("Monospace"));
    pango_attr_list_insert(stats_attributes, pango_attr_size_new(9 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(app->stats_label), stats_attributes);
    pango_attr_list_unref(stats_attributes);
    gtk_box_pack_start(GTK_BOX(left_panel), app->stats_label, FALSE, FALSE, 6);

    app->drawing_area = gtk_drawing_area_new();
    gtk_widget_add_events(app->drawing_area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK);
    gtk_box_pack_start(GTK_BOX(main_box), app->drawing_area, TRUE, TRUE, 0);
    g_signal_connect(app->drawing_area, "draw", G_CALLBACK(draw_plot), app);
    g_signal_connect(app->drawing_area, "button-press-event", G_CALLBACK(button_press), app);
    g_signal_connect(app->drawing_area, "button-release-event", G_CALLBACK(button_release), app);
    g_signal_connect(app->drawing_area, "motion-notify-event", G_CALLBACK(motion_notify), app);
    g_signal_connect(app->drawing_area, "scroll-event", G_CALLBACK(scroll_event), app);
    g_signal_connect(app->drawing_area, "leave-notify-event", G_CALLBACK(leave_notify), app);

    build_series_panel(app);

    double total_s = app->last_time - app->first_time;
    double upper = total_s - app->view_duration_s;
    if (upper < 0.0) upper = 0.0;
    GtkAdjustment *adj = gtk_adjustment_new(0.0, 0.0, upper, 3600.0, 86400.0, 0.0);
    app->scrollbar = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(app->scrollbar), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), app->scrollbar, FALSE, FALSE, 3);
    g_signal_connect(app->scrollbar, "value-changed", G_CALLBACK(scrollbar_changed), app);

    app->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(app->status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(app->status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app->status_label), 1);
    gtk_widget_set_hexpand(app->status_label, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), app->status_label, FALSE, TRUE, 4);
    update_status(app);
}

static void free_app(AppData *app)
{
    if (!app) return;
    free(app->time_values);
    for (int i = 0; i < app->n_series; i++) {
        free(app->series[i].name);
        free(app->series[i].units);
        free(app->series[i].values);
        free(app->series[i].integrated_values);
        free(app->series[i].time_values);
    }
    free(app->series);
    free(app->filename);
    for (int i=0; i<app->n_files; i++) free(app->filenames[i]);
}

static void usage(const char *progname)
{
    fprintf(stderr,
"\n"
"tsviewer " TSVIEWER_VERSION " -- Interactive Scientific Time-Series Viewer\n"
"\n"
"Usage:\n"
"    %s file1 [file2] [file3] [file4]\n"
"\n"
"Input contract:\n"
"    tsviewer assumes that each input file represents a valid time series\n"
"    or ordered numeric series.  The first column (or columns) must contain\n"
"    either a recognized timestamp or a numeric abscissa.\n"
"\n"
"    A recognized timestamp is displayed as calendar time.  An unrecognized\n"
"    finite numeric first column is plotted directly on the abscissa.  In that\n"
"    case, no units, calendar meaning, sampling interval, or time origin are\n"
"    implied or inferred.  The coordinate may be an index, elapsed time,\n"
"    simulation time, fractional day, or another user-defined quantity.\n"
"\n"
"Arguments:\n"
"    file1    Reference or observed series.\n"
"    file2    Optional model or comparison series.\n"
"    file3    Optional model or comparison series.\n"
"    file4    Optional model or comparison series.\n"
"\n"
"Supported columnar ASCII input includes:\n"
"    * Generic delimited text, with or without column headers\n"
"    * Campbell Scientific TOA5 files\n"
"    * AmeriFlux timestamp format\n"
"    * Daily, hourly, sub-hourly, monthly, and annual time series\n"
"    * Space-, tab-, comma-, or pipe-delimited files\n"
"    * Comment lines beginning with '#'\n"
"\n"
"Recognized abscissa formats include:\n"
"    YYYY-MM-DD\n"
"    YYYY-MM-DD HH:MM[:SS]\n"
"    YYYYMMDDHHMM[SS]\n"
"    Julian Date (standard astronomical JD; integer changes at noon UTC)\n"
"    Modified Julian Date (MJD = JD - 2400000.5; integer changes at midnight)\n"
"    Unix seconds or milliseconds\n"
"    Year/month and year-only summary coordinates\n"
"    Generic numeric values\n"
"\n"
"Multiple files:\n"
"    All files must use the same abscissa class: either calendar time or\n"
"    generic numeric coordinates.  Calendar and numeric axes cannot be mixed.\n"
"    Color identifies variable; line style identifies source file.\n"
"\n"
"Time series evaluation metrics:\n"
"   Use Ref and Model to compare any two series, including two series from one\n"
"   file.  With Pair sets enabled, file1 is the reference and every selected\n"
"   matching series in files 2-4 is evaluated as a model.  NSE and KGE use\n"
"   exactly matching coordinates over the displayed interval; no interpolation\n"
"   is used.  With Log Y axis enabled, NSE_log(x) and KGE_log(x) are calculated\n"
"   from log10-transformed values when both series are everywhere >0 over the\n"
"   evaluation interval.  Values <= 0 in the evaluation interval make\n"
"   calculation of these metrics impossible.\n"
"\n"
"Examples:\n"
"    %s observed.csv\n"
"    %s observed.csv model.csv\n"
"    %s fig2.dat\n"
"\n"
"For additional documentation, see README.md.\n"
"\n",
        progname, progname, progname, progname);
}

int main(int argc, char **argv)
{

    if (argc == 1) {
        usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    if (argc > MAX_INPUT_FILES + 1) {
        fprintf(stderr,
                "Error: a maximum of %d input files may be specified.\n\n",
                MAX_INPUT_FILES);
        usage(argv[0]);
        return 1;
    }

    g_set_prgname("tsviewer");
    g_set_application_name("tsviewer");
    gtk_init(&argc, &argv);
    
    AppData app;
    memset(&app, 0, sizeof(app));
    app.reference_series_index = -1;
    app.model_series_index = -1;
    app.y_range_mode = Y_RANGE_ALL;
    app.log_y_axis = FALSE_INT;
    app.integrate_series = FALSE_INT;
    app.match_variable_colors = TRUE_INT;
    app.status_notice[0] = '\0';

    if (!load_time_series_file(&app, argv[1])) {
        fprintf(stderr, "Error: failed to load '%s'\n", argv[1]);
        free_app(&app);
        return 1;
    }
    
    app.n_files = 1;
    app.filenames[0] = strdup(argv[1]);
    for (int i=0; i<app.n_series; i++) {
        app.series[i].source_file_index = 0;
        app.series[i].n_records = app.n_records;
        app.series[i].time_values = malloc((size_t)app.n_records * sizeof(double));
        if (!app.series[i].time_values) { free_app(&app); return 1; }
        memcpy(app.series[i].time_values, app.time_values, (size_t)app.n_records * sizeof(double));
    }
    for (int file_index=1; file_index<argc-1; file_index++) {
        if (!append_loaded_file(&app, argv[file_index+1], file_index)) {
            free_app(&app);
            return 1;
        }
    }
    app.view_start_offset_s = 0.0;
    app.view_duration_s = app.last_time - app.first_time;
    if (app.n_series > 0) app.reference_series_index = 0;

    build_gui(&app);
    gtk_widget_show_all(app.window);
    gtk_main();
    free_app(&app);
    return 0;
}
