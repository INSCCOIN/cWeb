# cWeb

A small C reader-browser for constrained Linux handhelds (SharkDeck / WalnutPi, 480×320, no X required).

cWeb fetches pages with `curl`, strips them to text + numbered links, and optionally dithers the first few `<img>` tags in the terminal. There is no JavaScript, no CSS layout engine, and no Chrome.

## Why

The deck already had a Tk experiment (`dBrowser`) that died without `DISPLAY`. cWeb is the same job in two C files and ncurses, so it runs on the device console and over SSH/PuTTY.

## Features

- HTTP(S) via `curl` (`-L`, 25 s timeout, 2 MB HTML cap)
- HTML subset: headings, paragraphs, lists, `<a>`, `<img>`, entity decode
- Numbered links `[1]`… in the body; `1`–`9` / `0` follow 1–10
- Link list view (`l`) with href + link text
- Omnibox search (`/`) → [DuckDuckGo HTML](https://html.duckduckgo.com/html/)
- History back (`b`), reload (`r`)
- First 8 images saved under `/home/working/cweb-img/` and previewed as 48×18 ASCII gray
- Relative URL join for links and images
- Skips `<script>` / `<style>`

Not in v1: cookies, POST forms, HTTP/2, JS, tables-as-layout, framebuffer photos.

## Build

```bash
sudo apt install gcc make libncurses-dev curl
# optional, for image rasterize:
sudo apt install ffmpeg
# or: sudo apt install imagemagick

make
sudo make install   # → /usr/local/bin/cWeb
```

```
cWeb.c   UI, fetch, image load
html.c   parser + url_join
html.h   Page / Link / Img
```

Link: `-lncurses` only. TLS is `curl`’s problem.

## Run

```bash
./cWeb
./cWeb https://example.com
```

Needs a real TTY (the deck console or PuTTY). Not a framebuffer toy; do not expect it to paint `/dev/fb0`.

## Keys

| Key | Action |
|-----|--------|
| `g` | Go to URL (bare host becomes `https://`) |
| `/` | Search (DuckDuckGo html) |
| `1`–`9`, `0` | Follow link 1–10 |
| `l` | Toggle link list |
| `i` | Next loaded image |
| `j` `k` / arrows | Scroll |
| PgUp / PgDn | Jump 10 lines |
| `b` | Back |
| `r` | Reload |
| `q` | Quit |

Status line: `N links  M img  <title>`.


## Mouse

Needs a terminal that sends xterm mouse events (deck console or SSH with mouse reporting). Taps count as clicks if the tty maps them.

- Click a cyan underlined `[n]` to open that link
- Click a row in **Links** view
- Toolbar: **Back · Go · Search · Links/Page · Img · Quit**
- Click the blue URL bar to type an address
- Wheel scrolls when the terminal reports buttons 4/5



## Images

On each load, cWeb:

1. Collects up to 8 `<img src>`
2. `curl`s each (15 s, 400 KB cap), skips `data:` URLs
3. Writes `/home/working/cweb-img/1` … `8`
4. Scales to 48×18 gray with `ffmpeg`, or ImageMagick `convert` if ffmpeg is missing
5. Dithers with ` .:-=+*#%@` at the top of the page

If neither decoder is installed you still get the files and `{imgN}` markers in the text.

## Limits

| | |
|--|--|
| Links stored | 64 |
| Images fetched | 8 |
| Image preview | 48×18 |
| HTML download | 2 MB |
| History | 24 URLs |
| Title | 80 chars |

Weird pages (heavy SPA, Google’s JS search box) will look empty. Use `/` instead of Google’s homepage field.

## Install on SharkDeck

```bash
mkdir -p /home/working/cWeb
# copy sources, then:
cd /home/working/cWeb
make
install -m 755 cWeb /usr/local/bin/cWeb
```

Optional color-key bind (keyd), same pattern as `pcmd` / `cHeat`:

```
f5 = command(/usr/local/bin/cWeb)
```

## License

Use it. No warranty. It will mangle layouts; that is the point.
