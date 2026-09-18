/* cWeb — ncurses reader-browser for SharkDeck. */
#define _GNU_SOURCE
#include "html.h"
#include <ctype.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HIST 24
#define BMFILE "/home/working/cweb.bm"

static char url[512] = "https://example.com";
static char hist[HIST][512];
static int nhist, hist_i = -1;
static Page page;
static int off, link_mode, img_i;

static int fetch_file(const char *u, const char *path)
{
    char cmd[800];
    if (!strncmp(u, "data:", 5))
        return -1;
    snprintf(cmd, sizeof cmd,
             "curl -L --max-time 15 -sS -A 'cWeb/0.1' --max-filesize 400000 -o '%s' '%s' 2>/dev/null",
             path, u);
    return system(cmd) == 0 ? 0 : -1;
}

static int decode_gray(const char *path, unsigned char *pix)
{
    char cmd[640];
    FILE *p;
    size_t got;
    snprintf(cmd, sizeof cmd,
             "ffmpeg -nostdin -loglevel error -i '%s' -vf scale=%d:%d -f rawvideo -pix_fmt gray - 2>/dev/null",
             path, IMG_W, IMG_H);
    p = popen(cmd, "r");
    if (!p)
        return 0;
    got = fread(pix, 1, IMG_W * IMG_H, p);
    pclose(p);
    if (got == (size_t)(IMG_W * IMG_H))
        return 1;
    snprintf(cmd, sizeof cmd,
             "convert '%s' -resize %dx%d! -depth 8 gray:- 2>/dev/null",
             path, IMG_W, IMG_H);
    p = popen(cmd, "r");
    if (!p)
        return 0;
    got = fread(pix, 1, IMG_W * IMG_H, p);
    pclose(p);
    return got == (size_t)(IMG_W * IMG_H);
}

static void load_images(void)
{
    int i;
    mkdir("/home/working/cweb-img", 0755);
    for (i = 0; i < page.nimg; i++) {
        char path[128];
        snprintf(path, sizeof path, "/home/working/cweb-img/%d", i + 1);
        page.img[i].ok = 0;
        if (fetch_file(page.img[i].href, path) == 0)
            page.img[i].ok = decode_gray(path, page.img[i].pix);
    }
}
static char msg[96] = "g go  / search  b back  # follow  q";

static void push_hist(const char *u)
{
    if (hist_i >= 0 && !strcmp(hist[hist_i], u))
        return;
    if (hist_i + 1 < HIST)
        hist_i++;
    else {
        memmove(hist[0], hist[1], (HIST - 1) * sizeof hist[0]);
        hist_i = HIST - 1;
    }
    snprintf(hist[hist_i], sizeof hist[0], "%s", u);
    nhist = hist_i + 1;
}

static int fetch(const char *u, char **out, size_t *n)
{
    char cmd[640], tmp[] = "/tmp/cwebXXXXXX";
    int fd;
    FILE *f;
    long sz;
    *out = NULL;
    *n = 0;
    fd = mkstemp(tmp);
    if (fd < 0)
        return -1;
    close(fd);
    snprintf(cmd, sizeof cmd,
             "curl -L --max-time 25 -sS -A 'cWeb/0.1' --compressed -o '%s' '%s' 2>/tmp/cweb.err",
             tmp, u);
    if (system(cmd) != 0) {
        unlink(tmp);
        return -1;
    }
    f = fopen(tmp, "rb");
    if (!f) {
        unlink(tmp);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 2 * 1024 * 1024)
        sz = 0;
    *out = malloc((size_t)sz + 1);
    *n = fread(*out, 1, (size_t)sz, f);
    (*out)[*n] = 0;
    fclose(f);
    unlink(tmp);
    return 0;
}

static void load(const char *u)
{
    char *raw = NULL;
    size_t n = 0;
    char abs[512];
    snprintf(abs, sizeof abs, "%s", u);
    snprintf(msg, sizeof msg, "loading…");
    page_free(&page);
    off = 0;
    if (fetch(abs, &raw, &n) || !raw) {
        snprintf(msg, sizeof msg, "fetch failed");
        free(raw);
        return;
    }
    html_parse(raw, &page);
    free(raw);
    {
        int i;
        for (i = 0; i < page.nlink; i++) {
            char j[MAX_HREF];
            url_join(j, sizeof j, abs, page.link[i].href);
            snprintf(page.link[i].href, sizeof page.link[i].href, "%s", j);
        }
        for (i = 0; i < page.nimg; i++) {
            char j[MAX_HREF];
            url_join(j, sizeof j, abs, page.img[i].href);
            snprintf(page.img[i].href, sizeof page.img[i].href, "%s", j);
        }
        load_images();
        img_i = 0;
    }
    snprintf(url, sizeof url, "%s", abs);
    push_hist(abs);
    snprintf(msg, sizeof msg, "%d links  %d img  %s", page.nlink, page.nimg,
             page.title[0] ? page.title : "");
}

static void prompt(const char *label, char *out, size_t n)
{
    int y, x;
    echo();
    curs_set(1);
    getmaxyx(stdscr, y, x);
    (void)x;
    mvprintw(y - 1, 0, "%s ", label);
    clrtoeol();
    out[0] = 0;
    wgetnstr(stdscr, out, (int)n - 1);
    noecho();
    curs_set(0);
}

static void draw(void)
{
    int h, w, row, col, i;
    const char *s;
    getmaxyx(stdscr, h, w);
    erase();
    attron(A_REVERSE);
    mvprintw(0, 0, "%-*.*s", w, w, url);
    attroff(A_REVERSE);
    if (!link_mode) {
        static const char ramp[] = " .:-=+*#%@";
        row = 1;
        if (page.nimg) {
            Img *im = &page.img[img_i < page.nimg ? img_i : 0];
            mvprintw(row++, 0, "img %d/%d %s", img_i + 1, page.nimg,
                     im->alt[0] ? im->alt : im->href);
            if (im->ok) {
                int y, x;
                for (y = 0; y < IMG_H && row < h - 2; y++, row++) {
                    move(row, 0);
                    for (x = 0; x < IMG_W && x < w; x++) {
                        int v = im->pix[y * IMG_W + x] * 9 / 255;
                        addch((unsigned char)ramp[v]);
                    }
                }
            } else
                mvprintw(row++, 0, "(no decode — need ffmpeg or convert)");
        }
        s = page.body ? page.body : "(empty)";
        {
            int line = 0, skip = off;
            col = 0;
            while (*s && row < h - 2) {
                if (*s == '\n') {
                    if (skip)
                        skip--;
                    else
                        row++, col = 0;
                    s++;
                    line++;
                    continue;
                }
                if (skip) {
                    s++;
                    continue;
                }
                if (col >= w) {
                    row++;
                    col = 0;
                    if (row >= h - 2)
                        break;
                }
                mvaddch(row, col++, (unsigned char)*s++);
            }
        }
    } else {
        mvprintw(1, 0, "links");
        for (i = 0; i < page.nlink && i < h - 3; i++)
            mvprintw(2 + i, 0, "%2d %-20.20s %s", i + 1, page.link[i].text,
                     page.link[i].href);
    }
    attron(A_REVERSE);
    mvprintw(h - 1, 0, "%-*.*s", w, w, msg);
    attroff(A_REVERSE);
    refresh();
}

static void follow(int n)
{
    if (n < 1 || n > page.nlink) {
        snprintf(msg, sizeof msg, "no link %d", n);
        return;
    }
    load(page.link[n - 1].href);
}

static void search(const char *q)
{
    char enc[400], dst[512];
    size_t i, j = 0;
    for (i = 0; q[i] && j + 4 < sizeof enc; i++) {
        unsigned char c = (unsigned char)q[i];
        if (isalnum(c) || c == '-' || c == '_')
            enc[j++] = (char)c;
        else if (c == ' ')
            enc[j++] = '+';
        else {
            snprintf(enc + j, 4, "%%%02X", c);
            j += 3;
        }
    }
    enc[j] = 0;
    snprintf(dst, sizeof dst, "https://html.duckduckgo.com/html/?q=%s", enc);
    load(dst);
}

int main(int argc, char **argv)
{
    int ch;
    if (argc > 1)
        snprintf(url, sizeof url, "%s", argv[1]);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    load(url);
    draw();
    for (;;) {
        ch = getch();
        if (ch == 'q')
            break;
        else if (ch == 'g') {
            char u[512];
            prompt("url", u, sizeof u);
            if (u[0]) {
                if (!strstr(u, "://")) {
                    char t[512];
                    snprintf(t, sizeof t, "https://%s", u);
                    load(t);
                } else
                    load(u);
            }
        } else if (ch == '/') {
            char q[200];
            prompt("search", q, sizeof q);
            if (q[0])
                search(q);
        } else if (ch == 'b') {
            if (hist_i > 0) {
                hist_i--;
                load(hist[hist_i]);
                hist_i--; /* load pushed again */
            } else
                snprintf(msg, sizeof msg, "no back");
        } else if (ch == 'l')
            link_mode ^= 1;
        else if (ch == 'i') {
            if (page.nimg)
                img_i = (img_i + 1) % page.nimg;
        } else if (ch == 'r')
            load(url);
        else if (ch == KEY_DOWN || ch == 'j')
            off++;
        else if (ch == KEY_UP || ch == 'k') {
            if (off)
                off--;
        } else if (ch == KEY_NPAGE)
            off += 10;
        else if (ch == KEY_PPAGE) {
            off -= 10;
            if (off < 0)
                off = 0;
        } else if (ch >= '1' && ch <= '9')
            follow(ch - '0');
        else if (ch == '0')
            follow(10);
        draw();
    }
    page_free(&page);
    endwin();
    return 0;
}
