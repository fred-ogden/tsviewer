/* tsviewer_win32_compat.h -- Windows-only compatibility shims for tsviewer.
 *
 * tsviewer_main.c includes this file, and nothing else in it, when building
 * for Windows:
 *
 *     #ifdef _WIN32
 *     #include "windows/tsviewer_win32_compat.h"
 *     #endif
 *
 * It exists so that Windows support costs the portable source three lines
 * rather than a hundred.  Nothing here is compiled on Linux.
 *
 * Include it after <gtk/gtk.h>, <time.h>, <ctype.h> and <string.h>.
 */

#ifndef TSVIEWER_WIN32_COMPAT_H
#define TSVIEWER_WIN32_COMPAT_H

#ifndef _WIN32
#error "tsviewer_win32_compat.h is only for Windows builds"
#endif

/* mingw-w64 supplies neither timegm() nor strptime().  The replacements below
   cover exactly the directives used by this program: %Y %m %d %H %M %S and %%,
   plus literal and whitespace matching.  Field ranges are validated so that
   out-of-range components are rejected rather than silently normalized. */

#define timegm _mkgmtime

static int win_read_number(const char **p, int max_digits, int min_value,
                           int max_value, int *out)
{
    const char *s = *p;
    int digits = 0;
    int value = 0;

    while (*s && isspace((unsigned char)*s)) s++;
    while (digits < max_digits && isdigit((unsigned char)*s)) {
        value = value * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (digits == 0) return 0;
    if (value < min_value || value > max_value) return 0;

    *out = value;
    *p = s;
    return 1;
}

static char *strptime(const char *s, const char *format, struct tm *tm_value)
{
    const char *p = s;
    const char *f = format;

    while (*f) {
        if (*f == '%') {
            int value = 0;
            f++;
            switch (*f) {
            case 'Y':
                if (!win_read_number(&p, 4, 0, 9999, &value)) return NULL;
                tm_value->tm_year = value - 1900;
                break;
            case 'm':
                if (!win_read_number(&p, 2, 1, 12, &value)) return NULL;
                tm_value->tm_mon = value - 1;
                break;
            case 'd':
                if (!win_read_number(&p, 2, 1, 31, &value)) return NULL;
                tm_value->tm_mday = value;
                break;
            case 'H':
                if (!win_read_number(&p, 2, 0, 23, &value)) return NULL;
                tm_value->tm_hour = value;
                break;
            case 'M':
                if (!win_read_number(&p, 2, 0, 59, &value)) return NULL;
                tm_value->tm_min = value;
                break;
            case 'S':
                if (!win_read_number(&p, 2, 0, 60, &value)) return NULL;
                tm_value->tm_sec = value;
                break;
            case '%':
                if (*p != '%') return NULL;
                p++;
                break;
            default:
                return NULL;
            }
            f++;
        } else if (isspace((unsigned char)*f)) {
            while (*p && isspace((unsigned char)*p)) p++;
            while (*f && isspace((unsigned char)*f)) f++;
        } else {
            if (*p != *f) return NULL;
            p++;
            f++;
        }
    }

    return (char *)p;
}

/* The prebuilt Windows GTK3 bundle downloaded by windows/windows.mk ships
   3.24 DLLs alongside 3.10 headers, so a few functions the DLLs
   export are not declared.  Declaring them here avoids implicit declarations,
   which would otherwise pass floating-point arguments with the wrong ABI. */
#if !GTK_CHECK_VERSION(3, 16, 0)
void gtk_label_set_xalign(GtkLabel *label, gfloat xalign);
void gtk_label_set_yalign(GtkLabel *label, gfloat yalign);
#endif
#if !GTK_CHECK_VERSION(3, 18, 0)
void gtk_text_view_set_top_margin(GtkTextView *text_view, gint top_margin);
void gtk_text_view_set_bottom_margin(GtkTextView *text_view, gint bottom_margin);
#endif


#endif /* TSVIEWER_WIN32_COMPAT_H */
