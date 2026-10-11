/* WiFiReport: native SDL2 viewer for the read-only Wi-Fi/kernel report.
 * Runs collect.sh (read-only shell commands) as a child, then shows the text
 * on screen (scrollable) and leaves the file on the SD card.
 * SDL2 is declared by hand, like gitroms.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "font8.h"

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct { uint32_t type; uint8_t pad[60]; } __attribute__((aligned(8))) SDL_Event;
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS 0x00004000u
#define SDL_WINDOW_FULLSCREEN 0x00000001u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_RENDERER_SOFTWARE 0x00000001u
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_PIXELFORMAT_ABGR8888 376840196u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_CONTROLLERBUTTONDOWN 0x650
#define SDL_CONTROLLERDEVICEADDED 0x653
int SDL_Init(uint32_t);
void SDL_Quit(void);
const char *SDL_GetError(void);
int SDL_SetHint(const char *, const char *);
void SDL_ShowCursor(int);
SDL_Window *SDL_CreateWindow(const char *, int, int, int, int, uint32_t);
void SDL_DestroyWindow(SDL_Window *);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *, int, uint32_t);
void SDL_DestroyRenderer(SDL_Renderer *);
int SDL_RenderSetLogicalSize(SDL_Renderer *, int, int);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *, uint32_t, int, int, int);
void SDL_DestroyTexture(SDL_Texture *);
int SDL_UpdateTexture(SDL_Texture *, const void *, const void *, int);
int SDL_RenderClear(SDL_Renderer *);
int SDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const void *, const void *);
void SDL_RenderPresent(SDL_Renderer *);
int SDL_PollEvent(SDL_Event *);
uint32_t SDL_GetTicks(void);
void SDL_Delay(uint32_t);
int SDL_NumJoysticks(void);
int SDL_IsGameController(int);
void *SDL_GameControllerOpen(int);

#define VW 640
#define VH 480
#define MAXL 700
#define LW 200
#define RGB(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | 0xFF000000u)
static uint32_t fb[VW * VH];
static char lines[MAXL][LW];
static int nlines;

static void fill(int x, int y, int w, int h, uint32_t c) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > VW) w = VW - x;
	if (y + h > VH) h = VH - y;
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) fb[(y + j) * VW + x + i] = c;
}
static void text(int x, int y, const char *s, uint32_t c, int sc) {
	for (int k = 0; s[k]; k++) {
		unsigned char ch = (unsigned char)s[k];
		if (ch > 127) ch = '?';
		for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++)
			if (FONT8[ch][j] & (1 << (7 - i))) fill(x + (k * 8 + i) * sc, y + j * sc, sc, sc, c);
	}
}

static void header(const char *t) {
	fill(0, 0, VW, 36, RGB(24, 28, 24));
	fill(0, 34, VW, 2, RGB(230, 162, 60));
	text(12, 10, t, RGB(231, 239, 228), 2);
}
static void footer(const char *t) {
	fill(0, VH - 28, VW, 28, RGB(24, 28, 24));
	text(12, VH - 22, t, RGB(168, 181, 166), 1 + 1);
}

static int load_report(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	char buf[1024];
	nlines = 0;
	while (nlines < MAXL && fgets(buf, sizeof buf, f)) {
		size_t n = strlen(buf);
		while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
		/* wrap long lines at 38 chars */
		size_t off = 0;
		do {
			size_t take = n - off > 38 ? 38 : n - off;
			if (nlines >= MAXL) break;
			for (size_t i = 0; i < take && i < LW - 1; i++) {
				unsigned char c = (unsigned char)buf[off + i];
				lines[nlines][i] = (c < 32 || c > 126) ? '?' : (char)c;
				lines[nlines][i + 1] = 0;
			}
			if (!take) lines[nlines][0] = 0;
			nlines++;
			off += take;
		} while (off < n);
	}
	fclose(f);
	return 1;
}

static int key_dir(int sc) {
	switch (sc) {
	case 82: return -1; case 81: return 1;      /* up, down */
	case 20: case 80: return -10; case 8: case 79: return 10; /* q/left, e/right */
	}
	return 0;
}
static int is_quit_key(int sc) { return sc == 27 || sc == 41 || sc == 40 || sc == 88; } /* x esc return */

static void on_term(int s) { (void)s; _exit(0); }

int main(void) {
	signal(SIGTERM, on_term);
	const char *out = getenv("WIFIREPORT_OUT");
	const char *script = getenv("WIFIREPORT_SCRIPT");
	if (!out || !script) { fprintf(stderr, "WIFIREPORT_OUT / WIFIREPORT_SCRIPT not set\n"); return 2; }

	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0 &&
	    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 3;
	}
	uint32_t wf = SDL_WINDOW_SHOWN | (getenv("WIFIREPORT_WINDOWED") ? 0 : SDL_WINDOW_FULLSCREEN);
	SDL_Window *win = SDL_CreateWindow("WiFiReport", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, VW, VH, wf);
	if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
	SDL_RenderSetLogicalSize(ren, VW, VH);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, VW, VH);
	if (!tex) { SDL_Quit(); return 3; }
	SDL_ShowCursor(0);
	for (int i = 0, n = SDL_NumJoysticks(); i < n; i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

	pid_t pid = fork();
	if (pid == 0) {
		execlp("sh", "sh", script, out, (char *)NULL);
		_exit(127);
	}
	int done = 0, run = 1, top = 0;
	uint32_t t0 = SDL_GetTicks();
	while (run) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) run = 0;
			else if (ev.type == SDL_KEYDOWN) {
				int32_t sc;
				memcpy(&sc, (uint8_t *)&ev + 16, 4);
				if (is_quit_key((int)sc)) run = 0;
				top += key_dir((int)sc);
			} else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
				int b = ((uint8_t *)&ev)[12];
				if (b == 1 || b == 6) run = 0;           /* B, Start */
				else if (b == 11) top -= 1;              /* up */
				else if (b == 12) top += 1;              /* down */
				else if (b == 13 || b == 9) top -= 10;   /* left, L1 */
				else if (b == 14 || b == 10) top += 10;  /* right, R1 */
			} else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
				int32_t w;
				memcpy(&w, (uint8_t *)&ev + 8, 4);
				SDL_GameControllerOpen(w);
			}
		}
		if (!done) {
			int st;
			if (waitpid(pid, &st, WNOHANG) == pid) { done = 1; load_report(out); }
		}
		fill(0, 0, VW, VH, RGB(16, 18, 16));
		if (!done) {
			header("WiFiReport");
			text(32, 150, "Collecting information...", RGB(231, 239, 228), 2);
			int pos = (int)(((SDL_GetTicks() - t0) / 12) % 400);
			fill(120, 210, 400, 18, RGB(35, 41, 36));
			fill(120 + pos, 210, 80 > 400 - pos ? 400 - pos : 80, 18, RGB(230, 162, 60));
			footer("Please wait");
		} else {
			int rows = 12;
			if (top > nlines - rows) top = nlines - rows;
			if (top < 0) top = 0;
			char t[64];
			snprintf(t, sizeof t, "WiFiReport  %d-%d / %d", nlines ? top + 1 : 0, top + rows > nlines ? nlines : top + rows, nlines);
			header(t);
			if (!nlines) text(12, 60, "No report was written.", RGB(224, 100, 90), 2);
			for (int i = 0; i < rows && top + i < nlines; i++)
				text(12, 46 + i * 32, lines[top + i], RGB(231, 239, 228), 2);
			footer("UP/DOWN scroll  L/R page  B quit");
		}
		if (done && getenv("WIFIREPORT_SHOT")) { /* PC testing only */
			FILE *sf = fopen(getenv("WIFIREPORT_SHOT"), "wb");
			if (sf) {
				fprintf(sf, "P6\n%d %d\n255\n", VW, VH);
				for (int i = 0; i < VW * VH; i++) { fputc(fb[i] & 255, sf); fputc((fb[i] >> 8) & 255, sf); fputc((fb[i] >> 16) & 255, sf); }
				fclose(sf);
			}
			run = 0;
		}
		SDL_UpdateTexture(tex, NULL, fb, VW * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(33);
	}
	if (!done) { kill(pid, SIGTERM); waitpid(pid, NULL, 0); }
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
