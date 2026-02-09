#include "klog.h"
#include "framebuffer.h"

typedef struct {
    unsigned int* fb;
    unsigned int pitch;
    unsigned int width;
    unsigned int height;
    unsigned int window_x;
    unsigned int window_y;
    unsigned int window_width;
    unsigned int window_height;
    unsigned int cursor_y;
    unsigned int line_height;
    int enabled;
} KlogState;

static KlogState g_klog = {0};

static void fb_clear_rect(unsigned int* fb, unsigned int pitch, unsigned int width,
                          unsigned int x, unsigned int y, unsigned int w, unsigned int h,
                          unsigned int color) {
    for (unsigned int row = 0; row < h; row++) {
        for (unsigned int col = 0; col < w; col++) {
            if (x + col < width) {
                unsigned int* pixel = fb + (y + row) * (pitch / 4) + (x + col);
                *pixel = color;
            }
        }
    }
}

static void klog_scroll(void) {
    if (!g_klog.fb) {
        return;
    }

    unsigned int line = g_klog.line_height;
    unsigned int window_x = g_klog.window_x;
    unsigned int window_y = g_klog.window_y;
    unsigned int window_w = g_klog.window_width;
    unsigned int window_h = g_klog.window_height;
    unsigned int pitch_words = g_klog.pitch / 4;

    for (unsigned int y = 0; y + line < window_h; y++) {
        unsigned int* dst = g_klog.fb + (window_y + y) * pitch_words + window_x;
        unsigned int* src = g_klog.fb + (window_y + y + line) * pitch_words + window_x;
        for (unsigned int x = 0; x < window_w; x++) {
            dst[x] = src[x];
        }
    }

    fb_clear_rect(g_klog.fb, g_klog.pitch, g_klog.width,
                  window_x, window_y + window_h - line, window_w, line, 0x000000);

    if (g_klog.cursor_y >= line) {
        g_klog.cursor_y -= line;
    } else {
        g_klog.cursor_y = g_klog.window_y;
    }
}

static void klog_write_line(const char* prefix, const char* line, unsigned int color) {
    if (!g_klog.enabled || !g_klog.fb) {
        return;
    }

    if (g_klog.cursor_y + g_klog.line_height > g_klog.window_y + g_klog.window_height) {
        klog_scroll();
    }

    fb_clear_rect(g_klog.fb, g_klog.pitch, g_klog.width,
                  g_klog.window_x, g_klog.cursor_y,
                  g_klog.window_width, g_klog.line_height, 0x000000);

    char buf[192];
    int pos = 0;
    while (prefix && *prefix && pos < (int)sizeof(buf) - 1) {
        buf[pos++] = *prefix++;
    }
    while (line && *line && pos < (int)sizeof(buf) - 1) {
        buf[pos++] = *line++;
    }
    buf[pos] = 0;

    fb_print(g_klog.fb, g_klog.pitch, g_klog.window_x, g_klog.cursor_y, buf, color);
    g_klog.cursor_y += g_klog.line_height;
}

static void klog_write(const char* prefix, const char* msg, unsigned int color) {
    if (!msg || !g_klog.enabled || !g_klog.fb) {
        return;
    }

    char line[160];
    int pos = 0;
    const char* p = msg;

    while (*p) {
        if (*p == '\n') {
            line[pos] = 0;
            klog_write_line(prefix, line, color);
            pos = 0;
            p++;
            continue;
        }

        if (pos < (int)sizeof(line) - 1) {
            line[pos++] = *p;
        }
        p++;
    }

    if (pos > 0) {
        line[pos] = 0;
        klog_write_line(prefix, line, color);
    }
}

void klog_init_fb(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height) {
    if (!fb || pitch == 0 || width == 0 || height == 0) {
        return;
    }

    g_klog.fb = fb;
    g_klog.pitch = pitch;
    g_klog.width = width;
    g_klog.height = height;
    g_klog.window_x = 10;
    g_klog.window_y = 10;
    g_klog.line_height = 10;
    g_klog.window_width = (width > 20) ? (width - 20) : width;
    g_klog.window_height = (height > 140) ? 120 : (height > 80 ? height / 2 : height - 20);
    if (g_klog.window_height < g_klog.line_height * 3) {
        g_klog.window_height = g_klog.line_height * 3;
    }
    g_klog.cursor_y = g_klog.window_y;
    g_klog.enabled = 1;

    fb_clear_rect(fb, pitch, width, g_klog.window_x, g_klog.window_y,
                  g_klog.window_width, g_klog.window_height, 0x000000);
}

void klog_enable(int enabled) {
    g_klog.enabled = enabled ? 1 : 0;
}

void klog_info(const char* msg) {
    klog_write("I: ", msg, 0x00AAFFAA);
}

void klog_error(const char* msg) {
    klog_write("E: ", msg, 0x00FF5555);
}
