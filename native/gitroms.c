/* GitROMs 3.0: native 640x480 SDL2 GitHub ROM browser for R36S / ArkOS.
 *
 * No browser, no login. HTTPS goes through the system curl (ArkOS ships it
 * with CA certificates), run as a child process so the UI never blocks.
 * SDL2 is declared here by hand (no SDL headers needed to build) and the real
 * libSDL2-2.0.so.0 on the device is loaded at run time, same as the CryMon port.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include "font8.h"

/* ---------------------------------------------------------------- SDL2 */
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
const char *SDL_GetCurrentVideoDriver(void);
int SDL_NumJoysticks(void);
int SDL_IsGameController(int);
void *SDL_GameControllerOpen(int);

/* ------------------------------------------------------------ constants */
#define VW 640
#define VH 480
#define MAXITEMS 1500
#define ROWS 12
#define ROWH 30
#define VERSION "3.0"

static uint32_t fb[VW * VH];

enum { K_REPO, K_DIR, K_FILE };
enum { SC_BUSY, SC_LIST, SC_CONFIRM, SC_CONFLICT, SC_DOWNLOAD, SC_RESULT,
       SC_ERROR, SC_HISTORY, SC_EXIT };
enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_START, A_SELECT,
       A_PGUP, A_PGDN };
enum { T_NONE, T_REPOS, T_DIR, T_DL };

static const char *EXTS[] = {
	".nes", ".smc", ".sfc", ".gba", ".sgba", ".gbc", ".gb", ".md", ".gen",
	".bin", ".sms", ".gg", ".pce", ".z64", ".n64", ".v64", ".nds", ".chd",
	".pbp", ".iso", ".cso", ".cue", ".pak", ".zip", ".7z", NULL};
static const char *EXT_SYS[][2] = {
	{".nes", "nes"}, {".smc", "snes"}, {".sfc", "snes"}, {".gba", "gba"},
	{".sgba", "gba"}, {".gbc", "gbc"}, {".gb", "gb"}, {".md", "megadrive"},
	{".gen", "megadrive"}, {".bin", "megadrive"}, {".sms", "mastersystem"},
	{".gg", "gamegear"}, {".pce", "pcengine"}, {".z64", "n64"}, {".n64", "n64"},
	{".v64", "n64"}, {".nds", "nds"}, {".chd", "psx"}, {".pbp", "psx"},
	{".cue", "psx"}, {".iso", "psp"}, {".cso", "psp"}, {".pak", "gba"},
	{".zip", "arcade"}, {".7z", "arcade"}, {NULL, NULL}};
static const char *SYSTEMS[] = {
	"gba", "gbc", "gb", "nes", "snes", "megadrive", "mastersystem", "gamegear",
	"pcengine", "n64", "nds", "psx", "psp", "arcade", "fbneo", "mame", "ports",
	NULL};
static const char *ROM_ROOTS[] = {
	"/roms", "/roms2", "/storage/roms", "/mnt/SDCARD/roms", "/mnt/sdcard/roms",
	"/media/roms", "/EASYROMS", NULL};

/* ------------------------------------------------------------- globals */
static char g_user[64] = "xenodorian";
static char g_api[128] = "https://api.github.com";
static char g_home[400] = ".";
static char g_romroot[300] = "";
static char g_tmp[420];
static char g_logpath[440], g_histpath[440];
static int g_run = 1;

static void logf_(const char *fmt, ...) {
	FILE *f = fopen(g_logpath, "ab");
	if (!f) return;
	time_t t = time(NULL);
	char ts[32];
	strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S ", localtime(&t));
	fputs(ts, f);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}

/* --------------------------------------------------------------- utils */
static void scpy(char *dst, size_t n, const char *src) {
	if (!n) return;
	snprintf(dst, n, "%s", src ? src : "");
}

static int is_dir(const char *p) {
	DIR *d = opendir(p);
	if (!d) return 0;
	closedir(d);
	return 1;
}

static int path_exists(const char *p) { return access(p, F_OK) == 0; }

static int mkdir_p(const char *path) {
	char tmp[600];
	scpy(tmp, sizeof tmp, path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 0;
	return is_dir(path);
}

static long long file_size(const char *p) {
	FILE *f = fopen(p, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	long long n = ftell(f);
	fclose(f);
	return n < 0 ? 0 : n;
}

static char *read_file(const char *p, size_t max, size_t *outn) {
	FILE *f = fopen(p, "rb");
	if (!f) return NULL;
	char *buf = malloc(max + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t n = fread(buf, 1, max + 1, f);
	fclose(f);
	if (n > max) { free(buf); return NULL; } /* too large */
	buf[n] = 0;
	if (outn) *outn = n;
	return buf;
}

static void human(char *out, size_t n, long long v) {
	if (v >= 1073741824LL) snprintf(out, n, "%.1f GB", v / 1073741824.0);
	else if (v >= 1048576) snprintf(out, n, "%.1f MB", v / 1048576.0);
	else if (v >= 1024) snprintf(out, n, "%lld KB", v / 1024);
	else snprintf(out, n, "%lld B", v);
}

static const char *ext_of(const char *name, char *buf, size_t n) {
	const char *dot = strrchr(name, '.');
	buf[0] = 0;
	if (!dot || strchr(dot, '/')) return buf;
	for (size_t i = 0; i + 1 < n && dot[i]; i++) { buf[i] = (char)tolower((unsigned char)dot[i]); buf[i + 1] = 0; }
	return buf;
}

static int ext_allowed(const char *ext) {
	for (int i = 0; EXTS[i]; i++) if (!strcmp(EXTS[i], ext)) return 1;
	return 0;
}

static const char *default_system(const char *name) {
	char e[32];
	ext_of(name, e, sizeof e);
	for (int i = 0; EXT_SYS[i][0]; i++) if (!strcmp(EXT_SYS[i][0], e)) return EXT_SYS[i][1];
	return "arcade";
}

static int strncasecmp_(const char *a, const char *b, size_t n) {
	for (size_t i = 0; i < n; i++) {
		int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
		if (x != y) return x - y;
		if (!x) return 0;
	}
	return 0;
}

static int visible_file(const char *name) {
	char e[32];
	if (name[0] == '.') return 0;
	/* ".md" is also Markdown; Mega Drive ROMs are never named README etc. */
	if (!strncasecmp_(name, "readme", 6) || !strncasecmp_(name, "license", 7) ||
	    !strncasecmp_(name, "changelog", 9)) return 0;
	return ext_allowed(ext_of(name, e, sizeof e));
}

/* Strip control characters and slashes, keep ordinary names intact. */
static void sanitize(char *dst, size_t n, const char *src) {
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 1 < n; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c < 32 || c == 127) continue;
		dst[j++] = (c == '/' || c == '\\') ? '_' : (char)c;
	}
	dst[j] = 0;
	size_t s = 0;
	while (dst[s] == '.' || dst[s] == ' ') s++;
	if (s) memmove(dst, dst + s, strlen(dst + s) + 1);
	size_t l = strlen(dst);
	while (l && dst[l - 1] == ' ') dst[--l] = 0;
	if (!dst[0]) scpy(dst, n, "download");
}

static void unique_name(char *out, size_t n, const char *dir, const char *name) {
	char p[900];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	if (!path_exists(p)) { scpy(out, n, name); return; }
	char base[300], ext[40];
	const char *dot = strrchr(name, '.');
	if (dot && dot != name) {
		snprintf(base, sizeof base, "%.*s", (int)(dot - name), name);
		scpy(ext, sizeof ext, dot);
	} else {
		scpy(base, sizeof base, name);
		ext[0] = 0;
	}
	for (int i = 1; i < 1000; i++) {
		snprintf(out, n, "%s (%d)%s", base, i, ext);
		snprintf(p, sizeof p, "%s/%s", dir, out);
		if (!path_exists(p)) return;
	}
	snprintf(out, n, "%s (%ld)%s", base, (long)time(NULL), ext);
}

static void url_path(char *out, size_t n, const char *p) {
	size_t j = 0;
	for (size_t i = 0; p[i] && j + 4 < n; i++) {
		unsigned char c = (unsigned char)p[i];
		if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') out[j++] = (char)c;
		else j += (size_t)snprintf(out + j, n - j, "%%%02X", c);
	}
	out[j] = 0;
}

/* ---------------------------------------------------------------- JSON */
typedef struct J {
	int t; /* 0 null 1 bool 2 num 3 str 4 arr 5 obj */
	char *s, *key;
	double n;
	struct J *kids, *next;
} J;

static void j_free(J *j) {
	while (j) {
		J *nx = j->next;
		j_free(j->kids);
		free(j->s);
		free(j->key);
		free(j);
		j = nx;
	}
}

typedef struct { const char *p, *end; int depth, err; } JP;

static void jp_ws(JP *p) { while (p->p < p->end && strchr(" \t\r\n", *p->p)) p->p++; }

static int put_utf8(char *o, unsigned cp) {
	if (cp < 0x80) { o[0] = (char)cp; return 1; }
	if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) { o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
	o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | ((cp >> 12) & 63));
	o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63)); return 4;
}

static int hex4(const char *s, unsigned *out) {
	unsigned v = 0;
	for (int i = 0; i < 4; i++) {
		int c = (unsigned char)s[i];
		if (!isxdigit(c)) return 0;
		v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
	}
	*out = v;
	return 1;
}

static char *jp_string(JP *p) {
	size_t cap = 64, n = 0;
	char *o = malloc(cap);
	if (!o) { p->err = 1; return NULL; }
	p->p++; /* opening quote */
	while (p->p < p->end && *p->p != '"') {
		if (n + 8 >= cap) { cap *= 2; char *t = realloc(o, cap); if (!t) { free(o); p->err = 1; return NULL; } o = t; }
		if (*p->p == '\\') {
			p->p++;
			if (p->p >= p->end) break;
			char e = *p->p++;
			switch (e) {
			case 'n': o[n++] = '\n'; break;
			case 't': o[n++] = '\t'; break;
			case 'r': o[n++] = '\r'; break;
			case 'b': o[n++] = '\b'; break;
			case 'f': o[n++] = '\f'; break;
			case 'u': {
				unsigned cp;
				if (p->end - p->p < 4 || !hex4(p->p, &cp)) { p->err = 1; free(o); return NULL; }
				p->p += 4;
				if (cp >= 0xD800 && cp <= 0xDBFF) {
					unsigned lo;
					if (p->end - p->p >= 6 && p->p[0] == '\\' && p->p[1] == 'u' && hex4(p->p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						p->p += 6;
					} else cp = 0xFFFD;
				} else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
				n += (size_t)put_utf8(o + n, cp);
				break;
			}
			default: o[n++] = e; break;
			}
		} else o[n++] = *p->p++;
	}
	if (p->p >= p->end) { p->err = 1; free(o); return NULL; }
	p->p++; /* closing quote */
	o[n] = 0;
	return o;
}

static J *jp_value(JP *p);

static J *jp_container(JP *p, int obj) {
	if (++p->depth > 32) { p->err = 1; return NULL; }
	J *c = calloc(1, sizeof *c);
	if (!c) { p->err = 1; return NULL; }
	c->t = obj ? 5 : 4;
	char close = obj ? '}' : ']';
	p->p++;
	jp_ws(p);
	if (p->p < p->end && *p->p == close) { p->p++; p->depth--; return c; }
	J **tail = &c->kids;
	for (;;) {
		jp_ws(p);
		char *key = NULL;
		if (obj) {
			if (p->p >= p->end || *p->p != '"') { p->err = 1; free(key); break; }
			key = jp_string(p);
			if (!key) break;
			jp_ws(p);
			if (p->p >= p->end || *p->p != ':') { p->err = 1; free(key); break; }
			p->p++;
		}
		J *v = jp_value(p);
		if (!v) { free(key); break; }
		v->key = key;
		*tail = v;
		tail = &v->next;
		jp_ws(p);
		if (p->p >= p->end) { p->err = 1; break; }
		char d = *p->p++;
		if (d == close) { p->depth--; return c; }
		if (d != ',') { p->err = 1; break; }
	}
	p->err = 1;
	j_free(c);
	return NULL;
}

static J *jp_value(JP *p) {
	jp_ws(p);
	if (p->p >= p->end) { p->err = 1; return NULL; }
	char c = *p->p;
	if (c == '{') return jp_container(p, 1);
	if (c == '[') return jp_container(p, 0);
	J *j = calloc(1, sizeof *j);
	if (!j) { p->err = 1; return NULL; }
	if (c == '"') {
		j->t = 3;
		j->s = jp_string(p);
		if (!j->s) { free(j); return NULL; }
	} else if (!strncmp(p->p, "true", 4) && p->end - p->p >= 4) { j->t = 1; j->n = 1; p->p += 4; }
	else if (!strncmp(p->p, "false", 5) && p->end - p->p >= 5) { j->t = 1; p->p += 5; }
	else if (!strncmp(p->p, "null", 4) && p->end - p->p >= 4) { j->t = 0; p->p += 4; }
	else {
		char *e;
		j->t = 2;
		j->n = strtod(p->p, &e);
		if (e == p->p) { p->err = 1; free(j); return NULL; }
		p->p = e;
	}
	return j;
}

static J *json_parse(const char *s, size_t n) {
	JP p = {s, s + n, 0, 0};
	J *v = jp_value(&p);
	if (!v || p.err) { j_free(v); return NULL; }
	jp_ws(&p);
	if (p.p != p.end) { j_free(v); return NULL; }
	return v;
}

static J *jget(J *o, const char *key) {
	if (!o || o->t != 5) return NULL;
	for (J *k = o->kids; k; k = k->next) if (k->key && !strcmp(k->key, key)) return k;
	return NULL;
}
static const char *jstr(J *o, const char *key) {
	J *v = jget(o, key);
	return (v && v->t == 3) ? v->s : NULL;
}

/* ---------------------------------------------------------------- curl */
typedef struct {
	pid_t pid;
	int running, rc;
	char out[600], err[640], errtext[200];
} Job;

static int job_start(Job *j, const char *url, const char *out, const char *errpath, int maxtime) {
	memset(j, 0, sizeof *j);
	scpy(j->out, sizeof j->out, out);
	scpy(j->err, sizeof j->err, errpath);
	char mt[16];
	snprintf(mt, sizeof mt, "%d", maxtime);
	pid_t pid = fork();
	if (pid < 0) return 0;
	if (pid == 0) {
		int fd = open(j->err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { dup2(fd, 2); close(fd); }
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); close(nul); }
		execlp("curl", "curl", "-sS", "-L", "-f", "--connect-timeout", "15",
		       "--max-time", mt, "--retry", "2", "--retry-delay", "2",
		       "-A", "GitROMs-R36S/" VERSION,
		       "-H", "Accept: application/vnd.github+json",
		       "-o", j->out, url, (char *)NULL);
		_exit(127);
	}
	j->pid = pid;
	j->running = 1;
	return 1;
}

/* Returns 1 once finished (rc and errtext set). */
static int job_poll(Job *j) {
	if (!j->running) return 1;
	int st;
	pid_t r = waitpid(j->pid, &st, WNOHANG);
	if (r == 0) return 0;
	j->running = 0;
	if (r < 0) j->rc = 255;
	else if (WIFEXITED(st)) j->rc = WEXITSTATUS(st);
	else j->rc = 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
	j->errtext[0] = 0;
	FILE *f = fopen(j->err, "rb");
	if (f) {
		size_t n = fread(j->errtext, 1, sizeof j->errtext - 1, f);
		j->errtext[n] = 0;
		fclose(f);
	}
	remove(j->err);
	return 1;
}

static void job_cancel(Job *j) {
	if (!j->running) return;
	kill(j->pid, SIGTERM);
	waitpid(j->pid, NULL, 0);
	j->running = 0;
	remove(j->err);
}

static void explain(const Job *j, char *out, size_t n) {
	const char *e = strstr(j->errtext, "error: ");
	if (e && isdigit((unsigned char)e[7])) {
		int code = atoi(e + 7);
		if (code == 403 || code == 429)
			snprintf(out, n, "GitHub refused the request (%d). Anonymous limit is 60 per hour. Try again later.", code);
		else if (code == 404) snprintf(out, n, "Not found on GitHub (404).");
		else snprintf(out, n, "GitHub returned error %d.", code);
		return;
	}
	switch (j->rc) {
	case 6: snprintf(out, n, "Cannot find github.com. Is Wi-Fi connected?"); break;
	case 7: snprintf(out, n, "Cannot connect. Is Wi-Fi connected?"); break;
	case 28: snprintf(out, n, "Timed out. Wi-Fi may be slow or off."); break;
	case 35: case 51: case 58:
		snprintf(out, n, "Secure connection failed. Check the device date and time."); break;
	case 60: snprintf(out, n, "Certificate check failed. Check the device date and time."); break;
	case 18: snprintf(out, n, "Download was cut short. Try again."); break;
	case 56: snprintf(out, n, "Connection dropped. Try again."); break;
	case 127: snprintf(out, n, "curl is not installed on this device."); break;
	default: snprintf(out, n, "Network error (curl %d).", j->rc);
	}
}

/* ------------------------------------------------------------ app state */
typedef struct {
	int kind;
	char name[200], raw[260], path[400], url[700], desc[120], branch[64];
	long long size;
} Item;

typedef struct {
	int kind; /* K_REPO = repo list, else directory */
	char repo[100], branch[64], path[400];
	int sel, scroll;
} Frame;

typedef struct { char time[16], status[8], name[200], sys[24], msg[160]; } Hist;

static Item *items;
static int nitems, itemcap;
static Frame stack[40];
static int sp;
static Frame cur;
static int have_cur;
static int screen = SC_BUSY;
static char busy_label[200] = "Loading...";
static char err_text[400];
static int err_fatal;
static Hist hist[100];
static int nhist, hist_scroll;
static int prev_screen = SC_LIST;

/* running task */
static struct {
	int type;
	Job job;
	int cancel;
	Frame target; /* frame being loaded */
	int push;     /* push current frame on success */
	int keep_sel;
	int page;
	char outpath[640];
	Item *acc;    /* accumulated items (repos) */
	int nacc;
} task;

/* confirm / download */
static struct {
	Item item;
	int sys_idx;
	char name[260], copy_name[300];
	int conflict_sel;
} cf;
static struct {
	char name[300], sys[24], part[800], final[800];
	long long total, got;
	uint32_t poll_at;
} dl;
static struct { int ok; char text[400], note[160]; } result;

static int nsystems(void) { int n = 0; while (SYSTEMS[n]) n++; return n; }

static void items_reset(void) { nitems = 0; }
static Item *items_add(void) {
	if (nitems >= MAXITEMS) return NULL;
	if (nitems >= itemcap) {
		int nc = itemcap ? itemcap * 2 : 128;
		Item *t = realloc(items, (size_t)nc * sizeof *items);
		if (!t) return NULL;
		items = t;
		itemcap = nc;
	}
	memset(&items[nitems], 0, sizeof items[nitems]);
	return &items[nitems++];
}

static void sanitize_text(char *s) {
	for (; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) *s = (*s == '\n' || *s == '\r' || *s == '\t') ? ' ' : '?';
}

/* ---- history ---- */
static void hist_add(const char *status, const char *name, const char *sys, const char *msg) {
	if (nhist < 100) nhist++;
	memmove(&hist[1], &hist[0], (size_t)(nhist - 1) * sizeof hist[0]);
	time_t t = time(NULL);
	strftime(hist[0].time, sizeof hist[0].time, "%m-%d %H:%M", localtime(&t));
	scpy(hist[0].status, sizeof hist[0].status, status);
	scpy(hist[0].name, sizeof hist[0].name, name);
	scpy(hist[0].sys, sizeof hist[0].sys, sys);
	scpy(hist[0].msg, sizeof hist[0].msg, msg);
	sanitize_text(hist[0].name);
	sanitize_text(hist[0].msg);
	FILE *f = fopen(g_histpath, "ab");
	if (f) {
		fprintf(f, "%s\t%s\t%s\t%s\t%s\n", hist[0].time, hist[0].status, hist[0].name, hist[0].sys, hist[0].msg);
		fclose(f);
	}
}

static void hist_load(void) {
	nhist = 0;
	char *s = read_file(g_histpath, 64 * 1024, NULL);
	if (!s) return;
	for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[5];
		int k = 0;
		char *p = line;
		while (k < 5) {
			f[k++] = p;
			char *t = strchr(p, '\t');
			if (!t) break;
			*t = 0;
			p = t + 1;
		}
		if (k < 5) continue;
		if (nhist < 100) nhist++;
		memmove(&hist[1], &hist[0], (size_t)(nhist - 1) * sizeof hist[0]);
		scpy(hist[0].time, sizeof hist[0].time, f[0]);
		scpy(hist[0].status, sizeof hist[0].status, f[1]);
		scpy(hist[0].name, sizeof hist[0].name, f[2]);
		scpy(hist[0].sys, sizeof hist[0].sys, f[3]);
		scpy(hist[0].msg, sizeof hist[0].msg, f[4]);
	}
	free(s);
}

/* ---- task machinery ---- */
static int task_active(void) { return task.type != T_NONE; }

static void show_error(const char *msg, int back) {
	scpy(err_text, sizeof err_text, msg);
	sanitize_text(err_text);
	err_fatal = 0;
	screen = SC_ERROR;
	(void)back;
}

static void set_list(const Frame *f, int keep_sel) {
	Frame nf = *f;
	if (!keep_sel) { nf.sel = 0; nf.scroll = 0; }
	if (nf.sel >= nitems) nf.sel = nitems ? nitems - 1 : 0;
	cur = nf;
	have_cur = 1;
	screen = SC_LIST;
}

static int item_cmp(const void *a, const void *b) {
	const Item *x = a, *y = b;
	if (x->kind != y->kind) return x->kind == K_DIR ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

/* ---- small metadata cache (bounded, disposable) ---- */
#define NCACHE 6
static struct { char key[700]; Item *it; int n; } cache[NCACHE];
static int cache_next;

static void frame_key(const Frame *f, char *out, size_t n) {
	if (f->kind == K_REPO) snprintf(out, n, "repos:%s", g_user);
	else snprintf(out, n, "dir:%s@%s:%s", f->repo, f->branch, f->path);
}
static int cache_find(const char *key) {
	for (int i = 0; i < NCACHE; i++) if (cache[i].it && !strcmp(cache[i].key, key)) return i;
	return -1;
}
static void cache_drop(const char *key) {
	int i = cache_find(key);
	if (i >= 0) { free(cache[i].it); cache[i].it = NULL; cache[i].n = 0; }
}
static void cache_put(const char *key, const Item *src, int n) {
	cache_drop(key);
	int i = cache_next;
	cache_next = (cache_next + 1) % NCACHE;
	free(cache[i].it);
	cache[i].it = malloc((size_t)(n ? n : 1) * sizeof *src);
	if (!cache[i].it) return;
	memcpy(cache[i].it, src, (size_t)n * sizeof *src);
	cache[i].n = n;
	scpy(cache[i].key, sizeof cache[i].key, key);
}

/* ---- loading lists ---- */
enum { M_REPLACE, M_PUSH, M_POP };
static int task_mode;
static char resp_path[460], resp_err[460];

static void start_fetch(void) {
	char url[900], ep[420];
	if (task.type == T_REPOS) {
		snprintf(url, sizeof url, "%s/users/%s/repos?per_page=100&sort=updated&type=owner&page=%d", g_api, g_user, task.page);
	} else {
		url_path(ep, sizeof ep, task.target.path);
		char br[200];
		url_path(br, sizeof br, task.target.branch);
		snprintf(url, sizeof url, "%s/repos/%s/contents%s%s?ref=%s", g_api, task.target.repo,
		         task.target.path[0] ? "/" : "", ep, br);
	}
	logf_("GET %s", url);
	remove(resp_path);
	if (!job_start(&task.job, url, resp_path, resp_err, 40)) {
		task.type = T_NONE;
		show_error("Could not start curl.", 0);
	}
}

static void push_frame(void) {
	if (!have_cur) return;
	if (sp < 40) stack[sp++] = cur;
	else { memmove(&stack[0], &stack[1], 39 * sizeof stack[0]); stack[39] = cur; }
}

static void apply_list(const Frame *f, const Item *src, int n, int keep_sel) {
	items_reset();
	for (int i = 0; i < n; i++) { Item *d = items_add(); if (!d) break; *d = src[i]; }
	if (task_mode == M_PUSH) push_frame();
	else if (task_mode == M_POP && sp > 0) sp--;
	set_list(f, keep_sel);
}

static void begin_load(const Frame *f, int mode, int keep_sel, int force) {
	char key[700];
	frame_key(f, key, sizeof key);
	task_mode = mode;
	task.keep_sel = keep_sel;
	if (!force) {
		int c = cache_find(key);
		if (c >= 0) { apply_list(f, cache[c].it, cache[c].n, keep_sel); return; }
	}
	free(task.acc);
	task.acc = NULL;
	task.nacc = 0;
	task.cancel = 0;
	task.target = *f;
	task.page = 1;
	task.type = f->kind == K_REPO ? T_REPOS : T_DIR;
	if (f->kind == K_REPO) snprintf(busy_label, sizeof busy_label, "Loading %s repositories...", g_user);
	else snprintf(busy_label, sizeof busy_label, "Loading %s...", f->path[0] ? f->path : f->repo);
	screen = SC_BUSY;
	start_fetch();
}

static void acc_add(const Item *it) {
	if (!task.acc) task.acc = malloc(MAXITEMS * sizeof *task.acc);
	if (!task.acc || task.nacc >= MAXITEMS) return;
	task.acc[task.nacc++] = *it;
}

static void finish_load(void) {
	char key[700];
	frame_key(&task.target, key, sizeof key);
	if (task.target.kind != K_REPO) qsort(task.acc, (size_t)task.nacc, sizeof *task.acc, item_cmp);
	if (task.acc) cache_put(key, task.acc, task.nacc);
	task.type = T_NONE;
	apply_list(&task.target, task.acc, task.nacc, task.keep_sel);
	free(task.acc);
	task.acc = NULL;
	task.nacc = 0;
}

static void parse_page(void) {
	size_t n = 0;
	char *body = read_file(resp_path, 4 * 1024 * 1024, &n);
	remove(resp_path);
	if (!body) { task.type = T_NONE; show_error("Response from GitHub was missing or too large.", 0); return; }
	J *root = json_parse(body, n);
	free(body);
	if (!root) { task.type = T_NONE; show_error("GitHub sent a response I could not read.", 0); return; }
	J *arr = root;
	J single;
	if (root->t == 5) {
		if (jget(root, "message") && !jget(root, "type")) {
			j_free(root); task.type = T_NONE; show_error("Unexpected reply from GitHub.", 0); return;
		}
		single = *root; single.next = NULL; single.key = NULL;
		/* wrap a lone file object into a one-element list */
		static J holder;
		holder = (J){0};
		holder.t = 4;
		holder.kids = &single;
		arr = &holder;
	}
	if (arr->t != 4) { j_free(root); task.type = T_NONE; show_error("Unexpected reply from GitHub.", 0); return; }
	int count = 0;
	for (J *e = arr->kids; e; e = e->next) {
		count++;
		const char *name = jstr(e, "name");
		if (!name) continue;
		Item it;
		memset(&it, 0, sizeof it);
		scpy(it.name, sizeof it.name, name);
		sanitize_text(it.name);
		if (task.type == T_REPOS) {
			it.kind = K_REPO;
			J *own = jget(e, "owner");
			const char *login = own ? jstr(own, "login") : NULL;
			snprintf(it.path, sizeof it.path, "%s/%s", login ? login : g_user, name);
			scpy(it.branch, sizeof it.branch, jstr(e, "default_branch") ? jstr(e, "default_branch") : "main");
			scpy(it.desc, sizeof it.desc, jstr(e, "description"));
			sanitize_text(it.desc);
			acc_add(&it);
		} else {
			const char *type = jstr(e, "type");
			const char *path = jstr(e, "path");
			if (!type) continue;
			scpy(it.path, sizeof it.path, path ? path : name);
			if (!strcmp(type, "dir")) {
				if (name[0] == '.') continue;
				it.kind = K_DIR;
				acc_add(&it);
			} else if (!strcmp(type, "file") && visible_file(name) && jstr(e, "download_url")) {
				it.kind = K_FILE;
				scpy(it.url, sizeof it.url, jstr(e, "download_url"));
				J *sz = jget(e, "size");
				it.size = (sz && sz->t == 2) ? (long long)sz->n : 0;
				scpy(it.raw, sizeof it.raw, name); /* unmodified name for saving */
				acc_add(&it);
			}
		}
	}
	/* detach the stack-allocated wrapper before freeing */
	j_free(root);
	if (task.type == T_REPOS && count >= 100 && task.page < 10) {
		task.page++;
		start_fetch();
	} else {
		finish_load();
	}
}

/* ---- downloads ---- */
static void dest_dir(char *out, size_t n) { snprintf(out, n, "%s/%s", g_romroot, SYSTEMS[cf.sys_idx]); }

static void open_confirm(const Item *it) {
	cf.item = *it;
	const char *sys = default_system(it->raw[0] ? it->raw : it->name);
	cf.sys_idx = 0;
	for (int i = 0; SYSTEMS[i]; i++) if (!strcmp(SYSTEMS[i], sys)) cf.sys_idx = i;
	sanitize(cf.name, sizeof cf.name, it->raw[0] ? it->raw : it->name);
	screen = SC_CONFIRM;
}

static void begin_download(const char *final_name) {
	char dir[400];
	dest_dir(dir, sizeof dir);
	if (!mkdir_p(dir) || access(dir, W_OK) != 0) {
		char m[500];
		snprintf(m, sizeof m, "Cannot write to %s", dir);
		show_error(m, 1);
		return;
	}
	if (cf.item.size > 0) {
		struct statvfs sv;
		if (statvfs(dir, &sv) == 0) {
			long long freeb = (long long)sv.f_bavail * (long long)sv.f_frsize;
			if (freeb < cf.item.size + 5 * 1024 * 1024) {
				char a[32], b[32], m[200];
				human(a, sizeof a, cf.item.size);
				human(b, sizeof b, freeb);
				snprintf(m, sizeof m, "Not enough free space. Need %s, have %s.", a, b);
				show_error(m, 1);
				return;
			}
		}
	}
	memset(&dl, 0, sizeof dl);
	scpy(dl.name, sizeof dl.name, final_name);
	scpy(dl.sys, sizeof dl.sys, SYSTEMS[cf.sys_idx]);
	snprintf(dl.final, sizeof dl.final, "%s/%s", dir, final_name);
	snprintf(dl.part, sizeof dl.part, "%s.gitroms-part", dl.final);
	dl.total = cf.item.size;
	remove(dl.part);
	logf_("download %s -> %s", cf.item.url, dl.final);
	task.type = T_DL;
	task.cancel = 0;
	if (!job_start(&task.job, cf.item.url, dl.part, resp_err, 3600)) {
		task.type = T_NONE;
		show_error("Could not start curl.", 1);
		return;
	}
	screen = SC_DOWNLOAD;
}

static void finish_download(void) {
	char msg[420];
	int ok = 0;
	task.type = T_NONE;
	if (task.job.rc != 0) {
		explain(&task.job, msg, sizeof msg);
		logf_("curl %d: %s", task.job.rc, task.job.errtext);
		remove(dl.part);
	} else {
		long long sz = file_size(dl.part);
		if (sz == 0) { snprintf(msg, sizeof msg, "The downloaded file was empty."); remove(dl.part); }
		else if (path_exists(dl.final)) { snprintf(msg, sizeof msg, "A file with that name appeared. Not overwritten."); remove(dl.part); }
		else if (rename(dl.part, dl.final) != 0) { snprintf(msg, sizeof msg, "Could not save file: %s", strerror(errno)); remove(dl.part); }
		else {
			char h[32];
			human(h, sizeof h, sz);
			snprintf(msg, sizeof msg, "Saved %s (%s) to %s.", dl.name, h, dl.sys);
			result.note[0] = 0;
			if (cf.item.size > 0 && sz != cf.item.size) snprintf(result.note, sizeof result.note, "Size differs from the listing (possibly a Git LFS file).");
			hist_add("OK", dl.name, dl.sys, h);
			ok = 1;
		}
	}
	if (!ok) {
		if (!msg[0]) snprintf(msg, sizeof msg, "Download failed.");
		result.note[0] = 0;
		hist_add("FAIL", dl.name, dl.sys, msg);
	}
	result.ok = ok;
	scpy(result.text, sizeof result.text, msg);
	screen = SC_RESULT;
}

static void update_task(uint32_t now) {
	if (!task_active()) return;
	if (task.cancel) {
		job_cancel(&task.job);
		if (task.type == T_DL) {
			remove(dl.part);
			task.type = T_NONE;
			hist_add("FAIL", dl.name, dl.sys, "Cancelled");
			result.ok = 0;
			result.note[0] = 0;
			scpy(result.text, sizeof result.text, "Download cancelled.");
			screen = SC_RESULT;
		} else {
			task.type = T_NONE;
			remove(resp_path);
			show_error("Cancelled.", 0);
		}
		return;
	}
	if (task.type == T_DL && now >= dl.poll_at) {
		dl.poll_at = now + 250;
		dl.got = file_size(dl.part);
	}
	if (!job_poll(&task.job)) return;
	if (task.type == T_DL) { finish_download(); return; }
	if (task.job.rc != 0) {
		char m[300];
		explain(&task.job, m, sizeof m);
		logf_("curl %d: %s", task.job.rc, task.job.errtext);
		remove(resp_path);
		if (task.type == T_DIR && task.target.path[0] == 0 && strstr(m, "404"))
			snprintf(m, sizeof m, "This repository is empty or has no files on its default branch.");
		task.type = T_NONE;
		show_error(m, 0);
		return;
	}
	parse_page();
}

/* ---------------------------------------------------------------- input */
static void do_refresh(void) {
	if (!have_cur) return;
	char key[700];
	frame_key(&cur, key, sizeof key);
	cache_drop(key);
	begin_load(&cur, M_REPLACE, 1, 1);
}

static void move_sel(int d) {
	if (!nitems) return;
	cur.sel += d;
	if (cur.sel < 0) cur.sel = 0;
	if (cur.sel >= nitems) cur.sel = nitems - 1;
	if (cur.sel < cur.scroll) cur.scroll = cur.sel;
	if (cur.sel >= cur.scroll + ROWS) cur.scroll = cur.sel - ROWS + 1;
}

static void go_back(void) {
	if (sp > 0) begin_load(&stack[sp - 1], M_POP, 1, 0);
	else screen = SC_EXIT;
}

static void load_root(void) {
	Frame f;
	memset(&f, 0, sizeof f);
	f.kind = K_REPO;
	sp = 0;
	have_cur = 0;
	begin_load(&f, M_REPLACE, 0, 1);
}

static void act(int a) {
	switch (screen) {
	case SC_BUSY:
		if (a == A_BACK && task_active()) task.cancel = 1;
		break;
	case SC_LIST:
		if (a == A_UP) move_sel(-1);
		else if (a == A_DOWN) move_sel(1);
		else if (a == A_PGUP || a == A_LEFT) move_sel(-ROWS);
		else if (a == A_PGDN || a == A_RIGHT) move_sel(ROWS);
		else if (a == A_BACK) go_back();
		else if (a == A_SELECT) do_refresh();
		else if (a == A_START) { prev_screen = SC_LIST; hist_scroll = 0; screen = SC_HISTORY; }
		else if (a == A_OK && nitems) {
			Item *it = &items[cur.sel];
			Frame f;
			memset(&f, 0, sizeof f);
			f.kind = K_DIR;
			if (it->kind == K_REPO) {
				scpy(f.repo, sizeof f.repo, it->path);
				scpy(f.branch, sizeof f.branch, it->branch);
				begin_load(&f, M_PUSH, 0, 0);
			} else if (it->kind == K_DIR) {
				scpy(f.repo, sizeof f.repo, cur.repo);
				scpy(f.branch, sizeof f.branch, cur.branch);
				scpy(f.path, sizeof f.path, it->path);
				begin_load(&f, M_PUSH, 0, 0);
			} else if (!g_romroot[0]) {
				show_error("ROM folder not found (looked in /roms, /roms2, ...).", 1);
			} else open_confirm(it);
		}
		break;
	case SC_CONFIRM: {
		int n = nsystems();
		if (a == A_LEFT) cf.sys_idx = (cf.sys_idx + n - 1) % n;
		else if (a == A_RIGHT) cf.sys_idx = (cf.sys_idx + 1) % n;
		else if (a == A_BACK) screen = SC_LIST;
		else if (a == A_OK) {
			char dir[400], p[900];
			dest_dir(dir, sizeof dir);
			snprintf(p, sizeof p, "%s/%s", dir, cf.name);
			if (path_exists(p)) {
				unique_name(cf.copy_name, sizeof cf.copy_name, dir, cf.name);
				cf.conflict_sel = 0;
				screen = SC_CONFLICT;
			} else begin_download(cf.name);
		}
		break;
	}
	case SC_CONFLICT:
		if (a == A_UP || a == A_DOWN) cf.conflict_sel = 1 - cf.conflict_sel;
		else if (a == A_BACK) screen = SC_LIST;
		else if (a == A_OK) { if (cf.conflict_sel == 0) begin_download(cf.copy_name); else screen = SC_LIST; }
		break;
	case SC_DOWNLOAD:
		if (a == A_BACK) task.cancel = 1;
		break;
	case SC_RESULT:
		if (a == A_OK || a == A_BACK) screen = SC_LIST;
		break;
	case SC_ERROR:
		if (err_fatal) { if (a == A_OK || a == A_BACK) g_run = 0; break; }
		if (a == A_OK) { if (have_cur) screen = SC_LIST; else load_root(); }
		else if (a == A_BACK) { if (have_cur) screen = SC_LIST; else screen = SC_EXIT; }
		else if (a == A_START || a == A_SELECT) load_root();
		break;
	case SC_HISTORY:
		if (a == A_UP && hist_scroll > 0) hist_scroll--;
		else if (a == A_DOWN && hist_scroll < nhist - 10) hist_scroll++;
		else if (a == A_BACK || a == A_START) screen = prev_screen;
		break;
	case SC_EXIT:
		if (a == A_OK) g_run = 0;
		else if (a == A_BACK) screen = have_cur ? SC_LIST : SC_ERROR;
		break;
	}
}

static uint32_t last_act_at[16];
static void fire(int a, int repeated, uint32_t now) {
	if (a <= A_NONE || a > A_PGDN) return;
	/* gptokeyb keys and controller events can both arrive for one press */
	if (!repeated && last_act_at[a] && now - last_act_at[a] < 50) return;
	last_act_at[a] = now;
	act(a);
}

static int key_action(int sc) {
	switch (sc) {
	case 82: return A_UP; case 81: return A_DOWN; case 80: return A_LEFT; case 79: return A_RIGHT;
	case 29: case 44: return A_OK;           /* z, space */
	case 27: case 41: return A_BACK;         /* x, escape */
	case 40: case 88: return A_START;        /* return, keypad enter */
	case 43: return A_SELECT;                /* tab */
	case 20: return A_PGUP; case 8: return A_PGDN; /* q, e */
	}
	return A_NONE;
}
static int pad_action(int b) {
	switch (b) {
	case 0: return A_OK; case 1: return A_BACK; case 6: return A_START;
	case 4: case 3: return A_SELECT; case 9: return A_PGUP; case 10: return A_PGDN;
	case 11: return A_UP; case 12: return A_DOWN; case 13: return A_LEFT; case 14: return A_RIGHT;
	}
	return A_NONE;
}

/* ------------------------------------------------------------- drawing */
#define RGB(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | 0xFF000000u)
#define C_BG RGB(16, 18, 16)
#define C_SURF RGB(24, 28, 24)
#define C_RAISED RGB(35, 41, 36)
#define C_FG RGB(231, 239, 228)
#define C_MUTED RGB(168, 181, 166)
#define C_LINE RGB(49, 56, 50)
#define C_SIGNAL RGB(230, 162, 60)
#define C_INK RGB(22, 19, 12)
#define C_BAD RGB(224, 100, 90)
#define C_GOOD RGB(120, 200, 120)

static void fill(int x, int y, int w, int h, uint32_t c) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > VW) w = VW - x;
	if (y + h > VH) h = VH - y;
	for (int j = 0; j < h; j++) {
		uint32_t *row = fb + (y + j) * VW + x;
		for (int i = 0; i < w; i++) row[i] = c;
	}
}

static void glyph(int x, int y, unsigned char ch, uint32_t c, int sc) {
	if (ch > 127) ch = '?';
	const unsigned char *rows = FONT8[ch];
	for (int j = 0; j < 8; j++)
		for (int i = 0; i < 8; i++)
			if (rows[j] & (1 << (7 - i))) fill(x + i * sc, y + j * sc, sc, sc, c);
}

static void text(int x, int y, const char *s, uint32_t c, int sc) {
	for (int i = 0; s[i]; i++) glyph(x + i * 8 * sc, y, (unsigned char)s[i], c, sc);
}

static void text_fit(int x, int y, const char *s, uint32_t c, int sc, int maxw) {
	int cap = maxw / (8 * sc), n = (int)strlen(s);
	if (n <= cap) { text(x, y, s, c, sc); return; }
	char buf[300];
	if (cap < 4) cap = 4;
	if (cap > 290) cap = 290;
	memcpy(buf, s, (size_t)(cap - 3));
	memcpy(buf + cap - 3, "...", 4);
	text(x, y, buf, c, sc);
}

static int text_wrap(int x, int y, const char *s, uint32_t c, int sc, int maxw, int maxlines) {
	int cpl = maxw / (8 * sc), i = 0, line = 0;
	char buf[200];
	if (cpl > 190) cpl = 190;
	while (s[i] && line < maxlines) {
		int n = 0, lastsp = -1;
		while (s[i + n] && n < cpl) { if (s[i + n] == ' ') lastsp = n; n++; }
		if (s[i + n] && lastsp > 0) n = lastsp;
		memcpy(buf, s + i, (size_t)n);
		buf[n] = 0;
		text(x, y + line * (8 * sc + 6), buf, c, sc);
		i += n;
		while (s[i] == ' ') i++;
		line++;
	}
	return y + line * (8 * sc + 6);
}

static void header(const char *title, const char *right) {
	fill(0, 0, VW, 48, C_SURF);
	fill(0, 46, VW, 2, C_SIGNAL);
	int rw = right ? (int)strlen(right) * 8 * 2 + 8 : 0;
	text_fit(16, 16, title, C_FG, 2, VW - 32 - rw);
	if (right) text(VW - 16 - (int)strlen(right) * 16, 16, right, C_MUTED, 2);
}

static void footer(const char *s) {
	fill(0, VH - 36, VW, 36, C_SURF);
	fill(0, VH - 36, VW, 1, C_LINE);
	text_fit(12, VH - 26, s, C_MUTED, 2, VW - 24);
}

static void progress(int x, int y, int w, double frac, uint32_t now) {
	fill(x, y, w, 18, C_RAISED);
	if (frac >= 0) {
		if (frac > 1) frac = 1;
		int bw = (int)(w * frac);
		fill(x, y, bw < 4 ? 4 : bw, 18, C_SIGNAL);
	} else {
		int pos = (int)((now / 12) % (uint32_t)(w - 80));
		fill(x + pos, y, 80, 18, C_SIGNAL);
	}
}

static void draw_list(void) {
	char right[40], title[300];
	if (cur.kind == K_REPO) {
		snprintf(title, sizeof title, "%s", g_user);
		snprintf(right, sizeof right, "%d repos", nitems);
	} else {
		const char *r = strchr(cur.repo, '/');
		snprintf(title, sizeof title, "%s%s%s", r ? r + 1 : cur.repo, cur.path[0] ? "/" : "", cur.path);
		scpy(right, sizeof right, cur.branch);
	}
	header(title, right);
	if (!nitems) text(32, 90, cur.kind == K_REPO ? "No public repositories found." : "No ROM files or folders here.", C_MUTED, 2);
	for (int i = 0; i < ROWS; i++) {
		int idx = cur.scroll + i;
		if (idx >= nitems) break;
		Item *it = &items[idx];
		int y = 56 + i * ROWH, sel = idx == cur.sel;
		if (sel) fill(8, y, VW - 24, 28, C_SIGNAL);
		uint32_t fg = sel ? C_INK : C_FG;
		char nm[220];
		if (it->kind == K_FILE) {
			text_fit(16, y + 6, it->name, fg, 2, VW - 170);
			char h[32];
			human(h, sizeof h, it->size);
			text(VW - 24 - (int)strlen(h) * 16, y + 6, h, sel ? C_INK : C_MUTED, 2);
		} else {
			snprintf(nm, sizeof nm, "%s%s", it->name, it->kind == K_DIR ? "/" : "");
			text_fit(16, y + 6, nm, fg, 2, VW - 40);
		}
	}
	if (nitems > ROWS) {
		int th = ROWS * ROWH, bh = th * ROWS / nitems;
		if (bh < 16) bh = 16;
		fill(VW - 10, 56, 4, th, C_LINE);
		fill(VW - 10, 56 + (th - bh) * cur.scroll / (nitems - ROWS), 4, bh, C_MUTED);
	}
	if (cur.kind == K_REPO && nitems && items[cur.sel].desc[0])
		text_fit(16, 420, items[cur.sel].desc, C_MUTED, 2, VW - 32);
	footer("A open  B back  SEL refresh  START log");
}

static void draw_screen(uint32_t now) {
	fill(0, 0, VW, VH, C_BG);
	char buf[500];
	switch (screen) {
	case SC_LIST: draw_list(); break;
	case SC_BUSY:
		header("GitROMs", NULL);
		text_fit(32, 150, busy_label, C_FG, 2, VW - 64);
		progress(120, 210, VW - 240, -1, now);
		footer("B cancel");
		break;
	case SC_CONFIRM: {
		header("Download", NULL);
		int y = text_wrap(32, 70, cf.name, C_FG, 2, VW - 64, 3) + 8;
		char h[32];
		human(h, sizeof h, cf.item.size);
		text(32, y, "Size", C_MUTED, 2);
		text(160, y, cf.item.size > 0 ? h : "unknown", C_FG, 2);
		y += 28;
		text(32, y, "Save to", C_MUTED, 2);
		text_fit(160, y, g_romroot, C_FG, 2, VW - 186);
		y += 40;
		text(32, y + 6, "System", C_MUTED, 2);
		fill(160, y, 300, 30, C_RAISED);
		snprintf(buf, sizeof buf, "<  %s  >", SYSTEMS[cf.sys_idx]);
		text_fit(176, y + 7, buf, C_SIGNAL, 2, 270);
		footer("A save  B cancel  L/R folder");
		break;
	}
	case SC_CONFLICT: {
		header("File already exists", NULL);
		int y = text_wrap(32, 70, cf.name, C_FG, 2, VW - 64, 2);
		text(32, y + 6, "It will not be overwritten.", C_MUTED, 2);
		const char *opt[2];
		char o1[340];
		snprintf(o1, sizeof o1, "Save as %s", cf.copy_name);
		opt[0] = o1;
		opt[1] = "Keep existing file (cancel)";
		y += 50;
		for (int i = 0; i < 2; i++) {
			if (i == cf.conflict_sel) fill(24, y, VW - 48, 34, C_SIGNAL);
			text_fit(36, y + 9, opt[i], i == cf.conflict_sel ? C_INK : C_FG, 2, VW - 72);
			y += 42;
		}
		footer("A choose  B cancel  UP/DOWN move");
		break;
	}
	case SC_DOWNLOAD: {
		header("Downloading", NULL);
		int y = text_wrap(32, 80, dl.name, C_FG, 2, VW - 64, 2);
		progress(32, y + 20, VW - 64, dl.total > 0 ? (double)dl.got / (double)dl.total : -1, now);
		char a[32], b[32];
		human(a, sizeof a, dl.got);
		if (dl.total > 0) { human(b, sizeof b, dl.total); snprintf(buf, sizeof buf, "%s of %s", a, b); }
		else scpy(buf, sizeof buf, a);
		text(32, y + 54, buf, C_MUTED, 2);
		snprintf(buf, sizeof buf, "Saving to %s", dl.sys);
		text(32, y + 84, buf, C_MUTED, 2);
		footer("B cancel download");
		break;
	}
	case SC_RESULT:
		header(result.ok ? "Done" : "Download failed", NULL);
		{
			int y = text_wrap(32, 90, result.text, result.ok ? C_GOOD : C_BAD, 2, VW - 64, 6);
			if (result.note[0]) text_wrap(32, y + 10, result.note, C_MUTED, 2, VW - 64, 3);
		}
		footer("A / B continue");
		break;
	case SC_ERROR:
		header("Problem", NULL);
		text_wrap(32, 90, err_text, C_BAD, 2, VW - 64, 8);
		footer(err_fatal ? "A quit" : have_cur ? "A/B back  START retry" : "A retry  B quit");
		break;
	case SC_HISTORY:
		snprintf(buf, sizeof buf, "%d saved", nhist);
		header("Downloads", buf);
		if (!nhist) text(32, 90, "Nothing downloaded yet.", C_MUTED, 2);
		for (int i = 0; i < 10; i++) {
			int idx = hist_scroll + i;
			if (idx >= nhist) break;
			int y = 54 + i * 38;
			fill(8, y, VW - 24, 36, C_SURF);
			text_fit(16, y + 2, hist[idx].name, hist[idx].status[0] == 'O' ? C_FG : C_BAD, 2, VW - 44);
			snprintf(buf, sizeof buf, "%s %s %s %s", hist[idx].time, hist[idx].sys, hist[idx].status, hist[idx].msg);
			text_fit(16, y + 22, buf, C_MUTED, 1, VW - 44);
		}
		footer("UP/DOWN scroll  B back");
		break;
	case SC_EXIT:
		header("Quit GitROMs?", NULL);
		text(32, 100, "Press A to quit, B to stay.", C_FG, 2);
		footer("A quit  B stay");
		break;
	}
}

/* ----------------------------------------------------------- test driver */
static char *tsteps[64];
static int ntsteps, tstep, test_mode;
static uint32_t twait_until;
static char *tshot;

static int act_from_name(const char *n) {
	static const char *names[] = {"", "up", "down", "left", "right", "confirm", "cancel", "start", "select", "pgup", "pgdn"};
	for (int i = 1; i <= A_PGDN; i++) if (!strcmp(n, names[i])) return i;
	return 0;
}

static void write_ppm(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", VW, VH);
	for (int i = 0; i < VW * VH; i++) {
		uint32_t c = fb[i];
		fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f);
	}
	fclose(f);
}

static void test_init(const char *spec) {
	static char copy[4096];
	scpy(copy, sizeof copy, spec);
	for (char *s = strtok(copy, ";"); s && ntsteps < 64; s = strtok(NULL, ";")) tsteps[ntsteps++] = s;
	test_mode = 1;
}

static void test_step(uint32_t now) {
	if (!test_mode || tshot) return;
	if (now < twait_until) return;
	if (tstep >= ntsteps) { g_run = 0; return; }
	char *st = tsteps[tstep];
	if (!strcmp(st, "idle")) {
		if (!task_active()) { tstep++; twait_until = now + 200; }
		return;
	}
	tstep++;
	if (!strncmp(st, "wait=", 5)) twait_until = now + (uint32_t)(atof(st + 5) * 1000);
	else if (!strncmp(st, "act=", 4)) { act(act_from_name(st + 4)); twait_until = now + 100; }
	else if (!strncmp(st, "shot=", 5)) tshot = st + 5;
}

/* ----------------------------------------------------------------- main */
static void on_term(int s) { (void)s; g_run = 0; }

static void load_conf(const char *path) {
	char *s = read_file(path, 8192, NULL);
	if (!s) return;
	for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#') continue;
		*eq = 0;
		char *v = eq + 1;
		size_t l = strlen(v);
		while (l && (v[l - 1] == '\r' || v[l - 1] == ' ')) v[--l] = 0;
		if (!strcmp(line, "user") && v[0]) scpy(g_user, sizeof g_user, v);
		else if (!strcmp(line, "api") && v[0]) scpy(g_api, sizeof g_api, v);
	}
	free(s);
}

int main(void) {
	const char *e;
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);

	if ((e = getenv("GITROMS_HOME")) && *e) scpy(g_home, sizeof g_home, e);
	mkdir_p(g_home);
	snprintf(g_logpath, sizeof g_logpath, "%s/gitroms.log", g_home);
	snprintf(g_histpath, sizeof g_histpath, "%s/history.txt", g_home);
	snprintf(g_tmp, sizeof g_tmp, "%s/tmp", g_home);
	mkdir_p(g_tmp);
	snprintf(resp_path, sizeof resp_path, "%s/resp.json", g_tmp);
	snprintf(resp_err, sizeof resp_err, "%s/resp.err", g_tmp);
	{ FILE *f = fopen(g_logpath, "wb"); if (f) fclose(f); }
	logf_("GitROMs %s start, home=%s", VERSION, g_home);

	if ((e = getenv("GITROMS_CONF")) && *e) load_conf(e);
	if ((e = getenv("GITROMS_USER")) && *e) scpy(g_user, sizeof g_user, e);
	if ((e = getenv("GITROMS_API")) && *e) scpy(g_api, sizeof g_api, e);

	if ((e = getenv("GITROMS_ROMS")) && *e && access(e, W_OK) == 0) scpy(g_romroot, sizeof g_romroot, e);
	for (int i = 0; !g_romroot[0] && ROM_ROOTS[i]; i++)
		if (is_dir(ROM_ROOTS[i]) && access(ROM_ROOTS[i], W_OK) == 0) scpy(g_romroot, sizeof g_romroot, ROM_ROOTS[i]);
	logf_("rom root: %s", g_romroot[0] ? g_romroot : "(none)");
	hist_load();

	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
		logf_("SDL_Init (with controllers) failed: %s", SDL_GetError());
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
			logf_("SDL_Init failed: %s", SDL_GetError());
			fprintf(stderr, "GitROMs: SDL_Init failed: %s\n", SDL_GetError());
			return 3;
		}
	}
	uint32_t wflags = SDL_WINDOW_SHOWN;
	if (!getenv("GITROMS_WINDOWED")) wflags |= SDL_WINDOW_FULLSCREEN;
	SDL_Window *win = SDL_CreateWindow("GitROMs", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, VW, VH, wflags);
	if (!win) {
		logf_("SDL_CreateWindow failed: %s", SDL_GetError());
		fprintf(stderr, "GitROMs: window failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 3;
	}
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (!ren) { logf_("accelerated renderer failed: %s", SDL_GetError()); ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); }
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	if (!ren) {
		logf_("SDL_CreateRenderer failed: %s", SDL_GetError());
		fprintf(stderr, "GitROMs: renderer failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 3;
	}
	SDL_RenderSetLogicalSize(ren, VW, VH);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, VW, VH);
	if (!tex) {
		logf_("SDL_CreateTexture failed: %s", SDL_GetError());
		SDL_Quit();
		return 3;
	}
	SDL_ShowCursor(0);
	{
		const char *drv = SDL_GetCurrentVideoDriver();
		logf_("video driver: %s", drv ? drv : "?");
	}
	for (int i = 0, n = SDL_NumJoysticks(); i < n; i++)
		if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

	if ((e = getenv("GITROMS_TEST"))) test_init(e);

	if (system("command -v curl >/dev/null 2>&1") != 0) {
		scpy(err_text, sizeof err_text, "curl was not found on this device, so GitROMs cannot reach GitHub.");
		err_fatal = 1;
		screen = SC_ERROR;
	} else load_root();

	while (g_run) {
		uint32_t t0 = SDL_GetTicks();
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) g_run = 0;
			else if (ev.type == SDL_KEYDOWN) {
				int32_t scan;
				memcpy(&scan, (uint8_t *)&ev + 16, 4);
				int rep = ((uint8_t *)&ev)[13];
				int a = key_action((int)scan);
				if (!(rep && a != A_UP && a != A_DOWN && a != A_LEFT && a != A_RIGHT)) fire(a, rep, t0);
			} else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
				fire(pad_action(((uint8_t *)&ev)[12]), 0, t0);
			} else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
				int32_t which;
				memcpy(&which, (uint8_t *)&ev + 8, 4);
				SDL_GameControllerOpen(which);
			}
		}
		update_task(t0);
		test_step(t0);
		draw_screen(t0);
		if (tshot) { write_ppm(tshot); tshot = NULL; }
		SDL_UpdateTexture(tex, NULL, fb, VW * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		uint32_t spent = SDL_GetTicks() - t0;
		if (spent < 33) SDL_Delay(33 - spent);
	}

	if (task_active()) {
		job_cancel(&task.job);
		if (task.type == T_DL) remove(dl.part);
	}
	logf_("exit");
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
