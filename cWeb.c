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
static int off, link_mode, img_i, run = 1;

enum { MAXHIT = 160 };
typedef struct {
    int y, x0, x1, id;
} Hit;
static Hit hits[MAXHIT];
static int nhit;

static void hit_add(int y, int x0, int x1, int id)
{
    if (nhit >= MAXHIT || x1 <= x0)
        return;
    hits[nhit].y = y;
    hits[nhit].x0 = x0;
    hits[nhit].x1 = x1;
    hits[nhit].id = id;
    nhit++;
}

static int hit_at(int y, int x)
{
    int i;
    for (i = nhit - 1; i >= 0; i--)
        if (hits[i].y == y && x >= hits[i].x0 && x < hits[i].x1)
            return hits[i].id;
    return 0;
}

static int fetch_file(const char *u, const char *path)
{
    char cmd[800];
    if (!strncmp(u, "data:", 5))
        return -1;
    snprintf(cmd, sizeof cmd,
             "curl -L --max-time 8 -sS -A 'cWeb/0.1' --max-filesize 120000 -o '%s' '%s' 2>/dev/null",
             path, u);
    return system(cmd) == 0 ? 0 : -1;
}

static int skip_img(const char *u)
{
    if (!u || !u[0] || !strncmp(u, "data:", 5) || !strncmp(u, "javascript:", 11))
        return 1;
    if (strstr(u, "favicon") || strstr(u, "1x1") || strstr(u, "pixel") ||
        strstr(u, "tracker") || strstr(u, "spin.") || strstr(u, ".svg"))
        return 1;
    return 0;
}

static int img_magic_ok(const char *path)
{
    unsigned char b[12];
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return 0;
    n = fread(b, 1, 12, f);
    fclose(f);
    if (n >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff)
        return 1;
    if (n >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return 1;
    if (n >= 6 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F')
        return 1;
    if (n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4))
        return 1;
    return 0;
}

static int decode_gray(const char *path, unsigned char *pix)
{
    char cmd[640];
    FILE *p;
    size_t got;
    snprintf(cmd, sizeof cmd,
             "ffmpeg -nostdin -loglevel error -i '%s' -vf scale=%d:%d -f rawvideo -pix_fmt rgb24 - 2>/dev/null",
             path, IMG_W, IMG_H);
    p = popen(cmd, "r");
    if (p) {
        unsigned char raw[IMG_W * IMG_H * 3];
        got = fread(raw, 1, sizeof raw, p);
        pclose(p);
        if (got == sizeof raw) {
            size_t i;
            for (i = 0; i < (size_t)IMG_W * IMG_H; i++) {
                int r = raw[i * 3], g = raw[i * 3 + 1], b = raw[i * 3 + 2];
                int R = r > 110, G = g > 110, B = b > 110;
                pix[i] = (unsigned char)((R || G || B) ? ((R ? 1 : 0) | (G ? 2 : 0) | (B ? 4 : 0)) : 0);
            }
            return 1;
        }
    }
    return 0;
}

static void load_one_image(int i)
{
    char path[128];
    if (i < 0 || i >= page.nimg || page.img[i].ok)
        return;
    if (skip_img(page.img[i].href))
        return;
    mkdir("/home/working/cweb-img", 0755);
    snprintf(path, sizeof path, "/home/working/cweb-img/%d", i + 1);
    if (fetch_file(page.img[i].href, path) != 0)
        return;
    if (!img_magic_ok(path))
        return;
    page.img[i].ok = decode_gray(path, page.img[i].pix);
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
        img_i = 0;
        load_one_image(0);
    }
    snprintf(url, sizeof url, "%s", abs);
    push_hist(abs);
    snprintf(msg, sizeof msg, "%d links  %d img  %s", page.nlink, page.nimg,
             page.title[0] ? page.title : "");
}

static void search(const char *q);
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

static void btn(int y, int *x, const char *lab, int id)
{
    int n = (int)strlen(lab);
    attron(COLOR_PAIR(4) | A_BOLD);
    mvaddstr(y, *x, lab);
    attroff(COLOR_PAIR(4) | A_BOLD);
    hit_add(y, *x, *x + n, id);
    *x += n + 1;
}

static void draw(void)
{
    int h, w, row, col, i, bx;
    const char *s;
    getmaxyx(stdscr, h, w);
    nhit = 0;
    erase();
    {
        char top[160];
        snprintf(top, sizeof top, " cWeb  %s", page.title[0] ? page.title : "reader");
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, 0, "%-*.*s", w, w, top);
        mvaddch(0, w > 2 ? w - 3 : 0, '[');
        mvaddch(0, w > 1 ? w - 2 : 0, 'x');
        mvaddch(0, w > 0 ? w - 1 : 0, ']');
        attroff(COLOR_PAIR(1) | A_BOLD);
        hit_add(0, w > 3 ? w - 3 : 0, w, -6);
    }
    bx = 2;
    btn(1, &bx, " <Back ", -1);
    btn(1, &bx, " Go ", -2);
    btn(1, &bx, " Search ", -3);
    btn(1, &bx, link_mode ? " Page " : " Links ", -4);
    btn(1, &bx, " Img ", -5);
    attron(COLOR_PAIR(5));
    mvprintw(2, 0, " %-*.*s", w - 1, w - 1, url);
    attroff(COLOR_PAIR(5));
    hit_add(2, 0, w, -2);

    if (!link_mode) {
        row = 3;
        if (page.nimg) {
            Img *im = &page.img[img_i < page.nimg ? img_i : 0];
            attron(COLOR_PAIR(3));
            mvprintw(row, 0, "img %d/%d  %s", img_i + 1, page.nimg,
                     im->alt[0] ? im->alt : "");
            attroff(COLOR_PAIR(3));
            hit_add(row, 0, w, -5);
            row++;
            if (im->ok) {
                int y, x;
                for (y = 0; y < IMG_H && row < h - 1; y++, row++) {
                    for (x = 0; x < IMG_W && x < w - 2; x++) {
                        int idx = im->pix[y * IMG_W + x] & 7;
                        attron(COLOR_PAIR(10 + idx));
                        mvaddch(row, 1 + x, ' ');
                        attroff(COLOR_PAIR(10 + idx));
                    }
                }
            } else
                mvprintw(row++, 1, "(image queued — tap Img)");
        }
        s = page.body ? page.body : "(empty)";
        col = 0;
        {
            int skip = off;
            while (*s && row < h - 1) {
                if (*s == '\n') {
                    if (skip)
                        skip--;
                    else {
                        row++;
                        col = 0;
                    }
                    s++;
                    continue;
                }
                if (skip) {
                    s++;
                    continue;
                }
                if (col >= w) {
                    row++;
                    col = 0;
                    if (row >= h - 1)
                        break;
                }
                if (*s == '[') {
                    const char *p = s + 1;
                    int n = 0;
                    while (*p >= '0' && *p <= '9')
                        n = n * 10 + (*p++ - '0');
                    if (*p == ']' && n >= 1 && n <= page.nlink) {
                        int x0 = col;
                        while (s <= p) {
                            if (col >= w) {
                                hit_add(row, x0, col, n);
                                row++;
                                col = 0;
                                x0 = 0;
                                if (row >= h - 1)
                                    break;
                            }
                            attron(COLOR_PAIR(2) | A_UNDERLINE);
                            mvaddch(row, col++, (unsigned char)*s++);
                            attroff(COLOR_PAIR(2) | A_UNDERLINE);
                        }
                        hit_add(row, x0, col, n);
                        continue;
                    }
                }
                mvaddch(row, col++, (unsigned char)*s++);
            }
        }
    } else {
        mvprintw(3, 0, "  #  text                  href");
        for (i = 0; i < page.nlink && 4 + i < h - 1; i++) {
            int y = 4 + i;
            if (i % 2)
                attron(COLOR_PAIR(3));
            else
                attron(COLOR_PAIR(2));
            mvprintw(y, 0, "%3d %-20.20s %-.*s", i + 1, page.link[i].text, w > 26 ? w - 26 : 8,
                     page.link[i].href);
            attroff(COLOR_PAIR(2) | COLOR_PAIR(3));
            hit_add(y, 0, w, i + 1);
        }
    }
    attron(COLOR_PAIR(1));
    mvprintw(h - 1, 0, "%-*.*s", w, w, msg);
    attroff(COLOR_PAIR(1));
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

static void go_typed(void)
{
    char u[512];
    prompt("url", u, sizeof u);
    if (!u[0])
        return;
    if (!strstr(u, "://")) {
        char t[520];
        snprintf(t, sizeof t, "https://%s", u);
        load(t);
    } else
        load(u);
}

static void do_id(int id)
{
    if (id > 0)
        follow(id);
    else if (id == -1) {
        if (hist_i > 0) {
            hist_i--;
            load(hist[hist_i]);
            hist_i--;
        } else
            snprintf(msg, sizeof msg, "no back");
    } else if (id == -2)
        go_typed();
    else if (id == -3) {
        char q[200];
        prompt("search", q, sizeof q);
        if (q[0])
            search(q);
    } else if (id == -4)
        link_mode ^= 1;
    else if (id == -5) {
        if (page.nimg) {
            img_i = (img_i + 1) % page.nimg;
            load_one_image(img_i);
        }
    } else if (id == -6)
        run = 0;
}

static void on_mouse(void)
{
    MEVENT e;
    int id;
    if (getmouse(&e) != OK)
        return;
    if (e.bstate & BUTTON4_PRESSED) {
        if (off)
            off--;
        return;
    }
    if (e.bstate & BUTTON5_PRESSED) {
        off++;
        return;
    }
    id = hit_at(e.y, e.x);
    if (e.bstate & REPORT_MOUSE_POSITION) {
        if (id > 0 && id <= page.nlink)
            snprintf(msg, sizeof msg, "→ %s", page.link[id - 1].href);
        return;
    }
    if (!(e.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED | BUTTON1_PRESSED)))
        return;
    if (id)
        do_id(id);
    else if (e.y >= 3)
        snprintf(msg, sizeof msg, "tap [n]  or  Back Go Search Links");
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
    meta(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);
        init_pair(2, COLOR_BLUE, COLOR_WHITE);
        init_pair(3, COLOR_BLACK, COLOR_WHITE);
        init_pair(4, COLOR_BLACK, COLOR_CYAN);
        init_pair(5, COLOR_BLACK, COLOR_CYAN);
        init_pair(10, COLOR_BLACK, COLOR_BLACK);
        init_pair(11, COLOR_RED, COLOR_RED);
        init_pair(12, COLOR_GREEN, COLOR_GREEN);
        init_pair(13, COLOR_YELLOW, COLOR_YELLOW);
        init_pair(14, COLOR_BLUE, COLOR_BLUE);
        init_pair(15, COLOR_MAGENTA, COLOR_MAGENTA);
        init_pair(16, COLOR_CYAN, COLOR_CYAN);
        init_pair(17, COLOR_WHITE, COLOR_WHITE);
    }
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
    printf("\033[?1000h\033[?1006h");
    fflush(stdout);
    load(url);
    draw();
    while (run) {
        ch = getch();
        if (ch == 'q')
            run = 0;
        else if (ch == KEY_MOUSE)
            on_mouse();
        else if (ch == 'g')
            go_typed();
        else if (ch == '/') {
            char q[200];
            prompt("search", q, sizeof q);
            if (q[0])
                search(q);
        } else if (ch == 'b')
            do_id(-1);
        else if (ch == 'l')
            link_mode ^= 1;
        else if (ch == 'i')
            do_id(-5);
        else if (ch == 'r')
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
        if (run)
            draw();
    }
    printf("\033[?1000l\033[?1006l");
    fflush(stdout);
    page_free(&page);
    endwin();
    return 0;
}
