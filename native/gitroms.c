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
#include "zipx.h"

/* ---------------------------------------------------------------- SDL2 */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct { uint32_t type; uint8_t pad[60]; } __attribute__((aligned(8))) SDL_Event;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u
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
#define SDL_KEYUP 0x301
#define SDL_JOYHATMOTION 0x602
#define SDL_JOYBUTTONDOWN 0x603
#define SDL_JOYBUTTONUP 0x604
#define SDL_JOYDEVICEADDED 0x605

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
void *SDL_JoystickOpen(int);
const uint8_t *SDL_GetKeyboardState(int *);
uint8_t SDL_JoystickGetButton(void *, int);
uint8_t SDL_JoystickGetHat(void *, int);
int SDL_PushEvent(SDL_Event *);

/* ------------------------------------------------------------ constants */
#define VW 640
#define VH 480
#define MAXITEMS 1500
#define ROWS 12
#define ROWH 30
#define VERSION "3.7"

static uint32_t fb[VW * VH];

enum { K_REPO, K_DIR, K_FILE, K_REL, K_UPD };
#define FK_REL 2 /* frame kind: latest-release asset list */
enum { SC_BUSY, SC_LIST, SC_CONFIRM, SC_CONFLICT, SC_DOWNLOAD, SC_RESULT,
       SC_ERROR, SC_HISTORY, SC_EXIT, SC_SYSINFO, SC_UPDATES, SC_UPDATING };
enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_START, A_SELECT,
       A_PGUP, A_PGDN, A_INFO };
enum { T_NONE, T_REPOS, T_DIR, T_DL, T_REL };

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

/* Remove a file or a whole folder. Callers only pass paths below the ports folder. */
static void remove_tree(const char *p) {
	struct stat st;
	if (lstat(p, &st) != 0) return;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(p);
		struct dirent *e;
		while (d && (e = readdir(d))) {
			char child[1024];
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(child, sizeof child, "%s/%s", p, e->d_name);
			remove_tree(child);
		}
		if (d) closedir(d);
		rmdir(p);
	} else remove(p);
}

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

static int job_start_ex(Job *j, const char *url, const char *out, const char *errpath, int maxtime, int api,
                        const char *range, const char *hdrpath, int maxfilesize) {
	memset(j, 0, sizeof *j);
	scpy(j->out, sizeof j->out, out);
	scpy(j->err, sizeof j->err, errpath);
	char mt[16], mf[16];
	snprintf(mt, sizeof mt, "%d", maxtime);
	snprintf(mf, sizeof mf, "%d", maxfilesize);
	pid_t pid = fork();
	if (pid < 0) return 0;
	if (pid == 0) {
		int fd = open(j->err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { dup2(fd, 2); close(fd); }
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); close(nul); }
		const char *av[40];
		int n = 0;
		av[n++] = "curl"; av[n++] = "-sS"; av[n++] = "-L"; av[n++] = "-f";
		av[n++] = "--connect-timeout"; av[n++] = "15";
		av[n++] = "--max-time"; av[n++] = mt;
		av[n++] = "--retry"; av[n++] = "2"; av[n++] = "--retry-delay"; av[n++] = "2";
		av[n++] = "-A"; av[n++] = "GitROMs-R36S/" VERSION;
		av[n++] = "-H"; av[n++] = api ? "Accept: application/vnd.github+json" : "Accept: */*";
		if (range) { av[n++] = "-r"; av[n++] = range; }
		if (hdrpath) { av[n++] = "-D"; av[n++] = hdrpath; }
		if (maxfilesize > 0) { av[n++] = "--max-filesize"; av[n++] = mf; }
		av[n++] = "-o"; av[n++] = j->out; av[n++] = url; av[n] = NULL;
		execvp("curl", (char *const *)av);
		_exit(127);
	}
	j->pid = pid;
	j->running = 1;
	return 1;
}

static int job_start(Job *j, const char *url, const char *out, const char *errpath, int maxtime, int api) {
	return job_start_ex(j, url, out, errpath, maxtime, api, NULL, NULL, 0);
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
	case 33: snprintf(out, n, "The server could not send that part of the file."); break;
	case 56: snprintf(out, n, "Connection dropped. Try again."); break;
	case 127: snprintf(out, n, "curl is not installed on this device."); break;
	default: snprintf(out, n, "Network error (curl %d).", j->rc);
	}
}

/* ------------------------------------------------------------ app state */
typedef struct {
	int kind;
	char name[200], raw[260], path[400], url[700], desc[120], branch[64], token[96];
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
/* ---- installed-file manifest: lets the list show which files have updates ---- */
typedef struct { char repo[100], remote[400], token[96], sys[24], name[260]; } Inst;
#define MAXINST 500
static Inst inst[MAXINST];
static int ninst;
static char g_instpath[440];

static void inst_load(void) {
	ninst = 0;
	char *buf = read_file(g_instpath, 256 * 1024, NULL);
	if (!buf) return;
	for (char *line = strtok(buf, "\n"); line && ninst < MAXINST; line = strtok(NULL, "\n")) {
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
		Inst *e = &inst[ninst++];
		scpy(e->repo, sizeof e->repo, f[0]);
		scpy(e->remote, sizeof e->remote, f[1]);
		scpy(e->token, sizeof e->token, f[2]);
		scpy(e->sys, sizeof e->sys, f[3]);
		scpy(e->name, sizeof e->name, f[4]);
	}
	free(buf);
}

static Inst *inst_find(const char *repo, const char *remote) {
	for (int i = ninst - 1; i >= 0; i--)
		if (!strcmp(inst[i].repo, repo) && !strcmp(inst[i].remote, remote)) return &inst[i];
	return NULL;
}

static void inst_record(const Inst *n) {
	int at = -1;
	for (int i = 0; i < ninst; i++)
		if (!strcmp(inst[i].repo, n->repo) && !strcmp(inst[i].remote, n->remote) &&
		    !strcmp(inst[i].sys, n->sys) && !strcmp(inst[i].name, n->name)) at = i;
	if (at < 0) {
		if (ninst >= MAXINST) { memmove(&inst[0], &inst[1], (MAXINST - 1) * sizeof inst[0]); ninst--; }
		at = ninst++;
	}
	inst[at] = *n;
	char tmp[460];
	snprintf(tmp, sizeof tmp, "%s.tmp", g_instpath);
	FILE *f = fopen(tmp, "wb");
	if (!f) return;
	for (int i = 0; i < ninst; i++)
		fprintf(f, "%s\t%s\t%s\t%s\t%s\n", inst[i].repo, inst[i].remote, inst[i].token, inst[i].sys, inst[i].name);
	fclose(f);
	rename(tmp, g_instpath);
}

static struct {
	Item item;
	int sys_idx;
	char name[260], copy_name[300], repo[100];
	int conflict_sel;
	int has_inst, upd;
} cf;
static struct {
	char name[300], sys[24], part[800], final[800];
	long long total, got;
	uint32_t poll_at;
	int replace, existed;
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
	int rx = x->kind == K_REL ? 0 : x->kind == K_DIR ? 1 : 2;
	int ry = y->kind == K_REL ? 0 : y->kind == K_DIR ? 1 : 2;
	if (rx != ry) return rx < ry ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

/* ---- small metadata cache (bounded, disposable) ---- */
#define NCACHE 6
static struct { char key[700]; Item *it; int n; } cache[NCACHE];
static int cache_next;

static void frame_key(const Frame *f, char *out, size_t n) {
	if (f->kind == K_REPO) snprintf(out, n, "repos:%s", g_user);
	else if (f->kind == FK_REL) snprintf(out, n, "rel:%s", f->repo);
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
	} else if (task.type == T_REL) {
		snprintf(url, sizeof url, "%s/repos/%s/releases/latest", g_api, task.target.repo);
	} else {
		url_path(ep, sizeof ep, task.target.path);
		char br[200];
		url_path(br, sizeof br, task.target.branch);
		snprintf(url, sizeof url, "%s/repos/%s/contents%s%s?ref=%s", g_api, task.target.repo,
		         task.target.path[0] ? "/" : "", ep, br);
	}
	logf_("GET %s", url);
	remove(resp_path);
	if (!job_start(&task.job, url, resp_path, resp_err, 40, 1)) {
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
	task.type = f->kind == K_REPO ? T_REPOS : f->kind == FK_REL ? T_REL : T_DIR;
	if (f->kind == K_REPO) snprintf(busy_label, sizeof busy_label, "Loading %s repositories...", g_user);
	else if (f->kind == FK_REL) snprintf(busy_label, sizeof busy_label, "Loading latest release...");
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
	if (task.target.kind == K_REPO) {
		Item u;
		memset(&u, 0, sizeof u);
		u.kind = K_UPD;
		scpy(u.name, sizeof u.name, "[Check for updates]");
		scpy(u.desc, sizeof u.desc, "GitROMs, the SGBA emulator and your games");
		if (!task.acc) task.acc = malloc(MAXITEMS * sizeof *task.acc);
		if (task.acc && task.nacc < MAXITEMS) {
			memmove(task.acc + 1, task.acc, (size_t)task.nacc * sizeof *task.acc);
			task.acc[0] = u;
			task.nacc++;
		}
	}
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
	if (task.type == T_REL) {
		J *assets = jget(root, "assets");
		const char *tag = jstr(root, "tag_name");
		if (!assets || assets->t != 4) { j_free(root); task.type = T_NONE; show_error("Unexpected reply from GitHub.", 0); return; }
		for (J *e = assets->kids; e; e = e->next) {
			const char *name = jstr(e, "name"), *url = jstr(e, "browser_download_url");
			if (!name || !url || !visible_file(name)) continue;
			Item it;
			memset(&it, 0, sizeof it);
			it.kind = K_FILE;
			scpy(it.name, sizeof it.name, name);
			sanitize_text(it.name);
			scpy(it.raw, sizeof it.raw, name);
			snprintf(it.path, sizeof it.path, "release/%s", name);
			scpy(it.url, sizeof it.url, url);
			J *sz = jget(e, "size"), *id = jget(e, "id");
			it.size = (sz && sz->t == 2) ? (long long)sz->n : 0;
			snprintf(it.token, sizeof it.token, "%.0f:%s", (id && id->t == 2) ? id->n : 0.0, jstr(e, "updated_at") ? jstr(e, "updated_at") : "");
			scpy(it.desc, sizeof it.desc, tag ? tag : "");
			sanitize_text(it.desc);
			acc_add(&it);
		}
		logf_("release %s: %d usable assets", tag ? tag : "?", task.nacc);
		j_free(root);
		finish_load();
		return;
	}
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
				scpy(it.token, sizeof it.token, jstr(e, "sha"));
				acc_add(&it);
			}
		}
	}
	/* detach the stack-allocated wrapper before freeing */
	j_free(root);
	if (task.type == T_DIR && task.target.path[0] == 0) {
		Item rel;
		memset(&rel, 0, sizeof rel);
		rel.kind = K_REL;
		scpy(rel.name, sizeof rel.name, "[Releases]");
		scpy(rel.path, sizeof rel.path, "@releases");
		acc_add(&rel);
	}
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
	scpy(cf.repo, sizeof cf.repo, cur.repo);
	cf.has_inst = cf.upd = 0;
	Inst *e = inst_find(cf.repo, it->path);
	if (e) {
		cf.has_inst = 1;
		cf.upd = it->token[0] && strcmp(e->token, it->token) != 0;
		for (int i = 0; SYSTEMS[i]; i++) if (!strcmp(SYSTEMS[i], e->sys)) cf.sys_idx = i;
		scpy(cf.name, sizeof cf.name, e->name);
	}
	screen = SC_CONFIRM;
}

static void begin_download(const char *final_name, int replace) {
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
	dl.replace = replace;
	dl.existed = path_exists(dl.final);
	remove(dl.part);
	logf_("download %s -> %s", cf.item.url, dl.final);
	task.type = T_DL;
	task.cancel = 0;
	if (!job_start(&task.job, cf.item.url, dl.part, resp_err, 3600, 0)) {
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
		else if (!dl.replace && path_exists(dl.final)) { snprintf(msg, sizeof msg, "A file with that name appeared. Not overwritten."); remove(dl.part); }
		else if (rename(dl.part, dl.final) != 0) { snprintf(msg, sizeof msg, "Could not save file: %s", strerror(errno)); remove(dl.part); }
		else {
			char h[32];
			human(h, sizeof h, sz);
			snprintf(msg, sizeof msg, "%s %s (%s) in %s.", dl.replace && dl.existed ? "Updated" : "Saved", dl.name, h, dl.sys);
			{
				Inst n;
				memset(&n, 0, sizeof n);
				scpy(n.repo, sizeof n.repo, cf.repo);
				scpy(n.remote, sizeof n.remote, cf.item.path);
				scpy(n.token, sizeof n.token, cf.item.token);
				scpy(n.sys, sizeof n.sys, dl.sys);
				scpy(n.name, sizeof n.name, dl.name);
				inst_record(&n);
			}
			result.note[0] = 0;
			if (cf.item.size > 0 && sz != cf.item.size) snprintf(result.note, sizeof result.note, "Size differs from the listing (possibly a Git LFS file).");
			hist_add("OK", dl.name, dl.sys, dl.replace && dl.existed ? "updated" : h);
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
		if (task.type == T_REL && strstr(m, "404"))
			snprintf(m, sizeof m, "No release has been published for this repository yet.");
		task.type = T_NONE;
		show_error(m, 0);
		return;
	}
	parse_page();
}

/* ---- system info (read-only shell collector, shown on screen) ---- */
#define SI_MAX 700
static char si_lines[SI_MAX][48];
static int si_n, si_top, si_running;
static pid_t sysi_pid;
static char si_out[460];

static void si_start(void) {
	const char *script = getenv("GITROMS_COLLECT");
	si_n = 0;
	si_top = 0;
	if (!script || access(script, R_OK) != 0) {
		scpy(si_lines[0], sizeof si_lines[0], "collect.sh not found.");
		si_n = 1;
		si_running = 0;
		return;
	}
	snprintf(si_out, sizeof si_out, "%s/SystemInfo.txt", g_home);
	sysi_pid = fork();
	if (sysi_pid == 0) {
		execlp("sh", "sh", script, si_out, (char *)NULL);
		_exit(127);
	}
	si_running = sysi_pid > 0;
}

static void si_load(void) {
	FILE *f = fopen(si_out, "rb");
	char buf[1024];
	si_n = 0;
	if (!f) { scpy(si_lines[0], sizeof si_lines[0], "No report was written."); si_n = 1; return; }
	while (si_n < SI_MAX && fgets(buf, sizeof buf, f)) {
		size_t n = strlen(buf), off = 0;
		while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
		do {
			size_t take = n - off > 38 ? 38 : n - off;
			if (si_n >= SI_MAX) break;
			for (size_t i = 0; i < take; i++) {
				unsigned char c = (unsigned char)buf[off + i];
				si_lines[si_n][i] = (c < 32 || c > 126) ? '?' : (char)c;
			}
			si_lines[si_n][take] = 0;
			si_n++;
			off += take;
		} while (off < n);
	}
	fclose(f);
}

static void si_poll(void) {
	if (!si_running) return;
	int st;
	if (waitpid(sysi_pid, &st, WNOHANG) == sysi_pid) { si_running = 0; si_load(); logf_("system info written to %s", si_out); }
}

static void si_stop(void) {
	if (si_running) { kill(sysi_pid, SIGTERM); waitpid(sysi_pid, NULL, 0); si_running = 0; }
}

/* ================================================================ updates
 * Push-button updates for GitROMs itself, the SGBA emulator and your games.
 * A "component" is a zip bundle (many files) or a single file at a URL. For a
 * zip, only its file list (central directory) is read, every file on the card
 * is compared by size and CRC32, and only the files that differ are fetched
 * (HTTP byte ranges, merged into a few requests) and written over the old
 * ones. Nothing is stored: the files on the card are the record of what is
 * installed, so it also works for installs made by hand. */
#define MAXCOMP 6
#define TAIL_BYTES 131072
enum { CS_IDLE, CS_CHECKING, CS_OK, CS_UPDATE, CS_NEW, CS_ERROR, CS_UPDATING };
enum { UPH_NONE, UPH_SIZE, UPH_TAIL, UPH_CD, UPH_CMP, UPH_FETCH, UPH_EXTRACT, UPH_FILEDL, UPH_STAMP };

typedef struct {
	char name[48], url[360], dest[240], keep[240], src[200], retire[640];
	char stamp[96], rec[96], pretty[40], sent[3][200];
	int nsent;
	int kind, self;          /* kind 0 = zip bundle, 1 = single file */
	int status;
	char msg[200];
	long long total;
	int rangeok;
	ZxDir dir;
	unsigned char *need;
	int nneed, nlocal;
	long long need_c, need_u;
} Comp;

static Comp comp[MAXCOMP];
static int ncomp, comp_sel, up_from = SC_LIST;   /* screen to return to */
static char g_ports[300];

static struct {
	int phase, ci, mode;     /* mode 0 = check, 1 = update */
	int cancel;
	Job job;
	uint8_t *tail;
	long long tail_start, tail_file_off;
	uint32_t cd_off, cd_size, cd_count;
	int cmp_i, ex_i;
	struct { long long s, e; } reg[40];
	int nreg, ri, full;
	long long p_done, p_total, p_base;
	char step[120];
	int restart_needed;
	int flag[MAXCOMP], updated[MAXCOMP];
} up;

static void comp_add(const char *name, int kind, const char *url, const char *dest, const char *keep, int self, const char *src, const char *retire) {
	if (ncomp >= MAXCOMP) return;
	Comp *c = &comp[ncomp++];
	memset(c, 0, sizeof *c);
	scpy(c->name, sizeof c->name, name);
	c->kind = kind;
	scpy(c->url, sizeof c->url, url);
	scpy(c->dest, sizeof c->dest, dest);
	scpy(c->keep, sizeof c->keep, keep);
	scpy(c->src, sizeof c->src, src);
	scpy(c->retire, sizeof c->retire, retire);
	c->self = self;
}

static void comp_runtime_free(Comp *c) {
	zx_free(&c->dir);
	free(c->need);
	c->need = NULL;
	c->nneed = c->nlocal = 0;
	c->need_c = c->need_u = 0;
}

/* What was last verified as installed: name, the remote modified-time stamp at that
 * moment, and up to three files that must still exist. A later check that sees the
 * same stamp (and those files) skips the slow file-by-file comparison. */
static char g_statepath[440];

static void state_save(void) {
	char tmp[460];
	snprintf(tmp, sizeof tmp, "%s.tmp", g_statepath);
	FILE *f = fopen(tmp, "wb");
	if (!f) return;
	for (int i = 0; i < ncomp; i++) {
		if (!comp[i].rec[0]) continue;
		fprintf(f, "%s\t%s", comp[i].name, comp[i].rec);
		for (int k = 0; k < comp[i].nsent; k++) fprintf(f, "\t%s", comp[i].sent[k]);
		fputc('\n', f);
	}
	fclose(f);
	rename(tmp, g_statepath);
}

static void state_load(void) {
	char *buf = read_file(g_statepath, 65536, NULL);
	if (!buf) return;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[5] = {0};
		int k = 0;
		char *p = line;
		while (k < 5) {
			f[k++] = p;
			char *t = strchr(p, '\t');
			if (!t) break;
			*t = 0;
			p = t + 1;
		}
		if (k < 2) continue;
		for (int i = 0; i < ncomp; i++)
			if (!strcmp(comp[i].name, f[0])) {
				scpy(comp[i].rec, sizeof comp[i].rec, f[1]);
				comp[i].nsent = 0;
				for (int j = 2; j < k && comp[i].nsent < 3; j++) scpy(comp[i].sent[comp[i].nsent++], sizeof comp[i].sent[0], f[j]);
			}
	}
	free(buf);
}

/* Human text for a stamp: "13 Oct 05:18" (empty for ETag-style stamps; shown right-aligned on the name line). */
static void stamp_pretty(const char *st, char *out, size_t n) {
	static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	int d, y, h, mi, mo;
	char mname[8];
	out[0] = 0;
	if (sscanf(st, "%*3s, %d %3s %d %d:%d", &d, mname, &y, &h, &mi) == 5) snprintf(out, n, "%d %s %02d:%02d", d, mname, h, mi);
	else if (sscanf(st, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) == 5 && mo >= 1 && mo <= 12) snprintf(out, n, "%d %s %02d:%02d", d, mon[mo - 1], h, mi);
}

/* components.conf lines:  name|zip or file|url|destination|keep list|self|stamp source
 * destination is relative to the ports folder ("." = the ports folder itself;
 * for a single file it is the file's path). Files in the keep list (comma
 * separated, paths as inside the zip) are installed only if missing. */
static void comps_load(void) {
	const char *path = getenv("GITROMS_COMPONENTS");
	char *buf = path ? read_file(path, 16384, NULL) : NULL;
	for (int i = 0; i < ncomp; i++) comp_runtime_free(&comp[i]);
	ncomp = 0;
	if (buf) {
		for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
			char *f[8] = {0};
			int k = 0;
			size_t l = strlen(line);
			while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
			if (!l || line[0] == '#') continue;
			char *p = line;
			while (k < 8) {
				f[k++] = p;
				char *t = strchr(p, '|');
				if (!t) break;
				*t = 0;
				p = t + 1;
			}
			if (k < 4 || !f[0][0] || !f[2][0]) continue;
			comp_add(f[0], !strcmp(f[1], "file"), f[2], f[3], k > 4 ? f[4] : "", k > 5 && !strcmp(f[5], "self"), k > 6 ? f[6] : "", k > 7 ? f[7] : "");
		}
		free(buf);
	}
	/* Built-in entries are added when no line of components.conf uses the same URL, so a
	 * card with an older components.conf still gets new ones (an unwanted one can be
	 * hidden by pointing a line at the same URL and giving it another name). */
	static const struct { const char *name, *url, *dest, *keep, *src, *retire; int kind, self; } def[] = {
		{"GitROMs", "https://github.com/xenodorian/GitROMs/raw/main/dist/GitROMs.zip", ".",
		 "gitroms/gitroms.conf,gitroms/components.conf", "commit:xenodorian/GitROMs:dist/GitROMs.zip", "", 0, 1},
		{"SGBA emulator", "https://github.com/xenodorian/SGBA/releases/download/latest/SGBA-arkos-aarch64.zip", ".",
		 "README.txt,sgba/controls.json", "",
		 /* the old Python emulator's files, removed once the native one is verified */
		 "sgba/python,sgba/site,sgba/cache/sgba-package-cache,sgba/runtime.py,sgba/sgba_audio.py,sgba/sgba_canvas.py,sgba/sgba_gfx.py,"
		 "sgba/sgba_js.py,sgba/sgba_js_shim.js,sgba/sgba_js_ui.py,sgba/sgba_package.py,sgba/sgba_shell.py", 0, 0},
		/* A game lives in its own package: "Parry Perry.sh" plus the one compiled .sgba in its own
		 * folder (parry-perry-game/), run on the SGBA emulator above, which holds no real games.
		 * Earlier copies are removed after a verified install: the file and folder inside the
		 * emulator's games folder, and the loose unpacked project in parry-perry/. */
		{"Parry Perry (game)", "https://github.com/xenodorian/BibooSGBA/releases/download/latest/parry-perry-port.zip",
		 ".", "", "", "sgba/games/parry-perry.sgba,sgba/games/parry-perry,parry-perry", 0, 0},
		{"Slingshot Orbits", "https://github.com/xenodorian/SGBA/releases/download/latest/slingshot-orbits-port.zip",
		 ".", "", "", "slingshot_orbits.sgba,sgba/games/slingshot_orbits.sgba", 0, 0},
		{"Pokemon War and Murder", "https://github.com/xenodorian/Pokemon-War-and-Murder/raw/main/roms/Pokemon%20War%20and%20Murder.gba",
		 "../gba/Pokemon War and Murder.gba", "", "commit:xenodorian/Pokemon-War-and-Murder:roms/Pokemon%20War%20and%20Murder.gba", "", 1, 0},
	};
	/* Entries of older components.conf files that a built-in entry replaced. */
	static const char *obsolete[] = {
		"https://github.com/xenodorian/BibooSGBA/releases/download/latest/parry-perry.sgba",
	};
	if (!getenv("GITROMS_NODEFAULTS"))
		for (int i = 0; i < ncomp;) {
			int gone = 0;
			for (size_t o = 0; o < sizeof obsolete / sizeof obsolete[0]; o++) if (!strcmp(comp[i].url, obsolete[o])) gone = 1;
			if (!gone) { i++; continue; }
			for (int j = i; j + 1 < ncomp; j++) comp[j] = comp[j + 1];
			ncomp--;
		}
	/* Built-in entries are authoritative for their URL: a line of an older components.conf
	 * with the same URL is brought up to date (name, kind, destination), and a missing one is added. */
	for (size_t d = 0; d < sizeof def / sizeof def[0] && !getenv("GITROMS_NODEFAULTS"); d++) {
		int have = 0;
		for (int i = 0; i < ncomp; i++)
			if (!strcmp(comp[i].url, def[d].url)) {
				have = 1;
				Comp *c = &comp[i];
				scpy(c->name, sizeof c->name, def[d].name);
				c->kind = def[d].kind;
				scpy(c->dest, sizeof c->dest, def[d].dest);
				scpy(c->keep, sizeof c->keep, def[d].keep);
				scpy(c->src, sizeof c->src, def[d].src);
				scpy(c->retire, sizeof c->retire, def[d].retire);
				c->self = def[d].self;
			}
		if (!have) comp_add(def[d].name, def[d].kind, def[d].url, def[d].dest, def[d].keep, def[d].self, def[d].src, def[d].retire);
	}
	for (int i = 0; i < ncomp; i++) comp[i].status = CS_IDLE;
	state_load();
}

static void comp_path(const Comp *c, const char *name, char *out, size_t n) {
	if (c->kind == 1) snprintf(out, n, "%s/%s", g_ports, c->dest);
	else if (!c->dest[0] || !strcmp(c->dest, ".")) snprintf(out, n, "%s/%s", g_ports, name);
	else snprintf(out, n, "%s/%s/%s", g_ports, c->dest, name);
}

static int comp_keep(const Comp *c, const char *name) {
	const char *p = c->keep;
	size_t nl = strlen(name);
	while (*p) {
		const char *q = strchr(p, ',');
		size_t l = q ? (size_t)(q - p) : strlen(p);
		if (l == nl && !strncmp(p, name, nl)) return 1;
		if (!q) break;
		p = q + 1;
	}
	return 0;
}

static void mkparent(const char *path) {
	char d[900];
	scpy(d, sizeof d, path);
	char *s = strrchr(d, '/');
	if (s) { *s = 0; mkdir_p(d); }
}

static void up_tmp(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", g_tmp, name); }

/* Value of the LAST header line starting with `key` (redirects give several blocks). */
static int hdr_find(const char *path, const char *key, char *out, size_t n) {
	char *s = read_file(path, 65536, NULL);
	int found = 0;
	if (!s) return 0;
	size_t kl = strlen(key);
	for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n"))
		if (!strncasecmp_(line, key, kl)) {
			const char *v = line + kl;
			while (*v == ' ') v++;
			scpy(out, n, v);
			size_t l = strlen(out);
			while (l && (out[l - 1] == '\r' || out[l - 1] == ' ')) out[--l] = 0;
			found = 1;
		}
	free(s);
	return found;
}

static long long hdr_total(const char *path, int *rangeok) {
	char v[200];
	*rangeok = 0;
	if (hdr_find(path, "content-range:", v, sizeof v)) {
		const char *sl = strrchr(v, '/');
		if (sl && isdigit((unsigned char)sl[1])) { *rangeok = 1; return atoll(sl + 1); }
	}
	if (hdr_find(path, "content-length:", v, sizeof v)) return atoll(v);
	return 0;
}

static int up_job(const Comp *c, const char *outname, long long a, long long b, const char *hdrname, int maxfs, int maxtime) {
	char out[480], hdr[480], err[480], rng[64];
	up_tmp(out, sizeof out, outname);
	up_tmp(err, sizeof err, "up.err");
	remove(out);
	hdr[0] = 0;
	if (hdrname) { up_tmp(hdr, sizeof hdr, hdrname); remove(hdr); }
	if (b >= 0) snprintf(rng, sizeof rng, "%lld-%lld", a, b);
	return job_start_ex(&up.job, c->url, out, err, maxtime, 0, b >= 0 ? rng : NULL, hdrname ? hdr : NULL, maxfs);
}

static void up_cleanup_tmp(void) {
	char p[480];
	static const char *fixed[] = {"up.size", "up.hdr", "up.tail", "up.cd", "up.err", "up.stamp"};
	for (int i = 0; i < 6; i++) { up_tmp(p, sizeof p, fixed[i]); remove(p); }
	for (int i = 0; i < 40; i++) { char n[24]; snprintf(n, sizeof n, "up.r%d", i); up_tmp(p, sizeof p, n); remove(p); }
	free(up.tail);
	up.tail = NULL;
}

static void up_advance(void);
static void up_begin(Comp *c);

static void up_fail(Comp *c, const char *msg) {
	c->status = CS_ERROR;
	scpy(c->msg, sizeof c->msg, msg);
	logf_("update %s: %s", c->name, msg);
	comp_runtime_free(c);
	up_cleanup_tmp();
	up_advance();
}

static void up_fail_job(Comp *c) {
	char m[300];
	explain(&up.job, m, sizeof m);
	logf_("curl %d: %s", up.job.rc, up.job.errtext);
	up_fail(c, m);
}

static void up_finish_comp(Comp *c, int st, const char *msg) {
	c->status = st;
	if (st == CS_OK && c->retire[0] && c->kind == 0) {            /* verified install: drop the old copies it replaces */
		char list[640];
		scpy(list, sizeof list, c->retire);
		for (char *t = strtok(list, ","); t; t = strtok(NULL, ",")) {
			char old[900];
			if (!zx_safe_name(t)) continue;                          /* never outside the ports folder */
			snprintf(old, sizeof old, "%s/%s", g_ports, t);
			if (path_exists(old)) { remove_tree(old); logf_("removed old copy %s", old); }
		}
	}
	if (msg != c->msg) scpy(c->msg, sizeof c->msg, msg);   /* callers often pass c->msg itself */
	up_cleanup_tmp();
	up_advance();
}

static void up_advance(void) {
	for (;;) {
		up.ci++;
		if (up.ci >= ncomp) {
			if (up.mode == 1) {
				int any = 0;
				for (int i = 0; i < ncomp; i++) { up.flag[i] = up.updated[i]; any |= up.updated[i]; }
				if (any) { up.mode = 0; up.ci = -1; continue; }   /* re-check what was just updated */
			}
			up.phase = UPH_NONE;
			if (screen == SC_UPDATING) screen = SC_UPDATES;
			return;
		}
		Comp *c = &comp[up.ci];
		if (up.mode == 0 ? up.flag[up.ci] : (c->status == CS_UPDATE || c->status == CS_NEW)) { up_begin(c); return; }
	}
}

static int up_space_ok(long long need) {
	struct statvfs sv;
	if (statvfs(g_ports, &sv) != 0) return 1;
	return (long long)sv.f_bavail * (long long)sv.f_frsize >= need + 16LL * 1048576;
}

static ZxEnt *qs_e;
static int cmp_off(const void *a, const void *b) {
	uint32_t x = qs_e[*(const int *)a].off, y = qs_e[*(const int *)b].off;
	return x < y ? -1 : x > y;
}

static void up_begin_update(Comp *c) {
	c->status = CS_UPDATING;
	up.p_done = up.p_base = 0;
	up.cancel = 0;
	snprintf(up.step, sizeof up.step, "Downloading");
	if (c->kind == 1) {
		char path[900], part[920], err[480];
		comp_path(c, "", path, sizeof path);
		if (!up_space_ok(c->total * 2)) { up_fail(c, "Not enough free space."); return; }
		mkparent(path);
		snprintf(part, sizeof part, "%s.gitroms-part", path);
		remove(part);
		up_tmp(err, sizeof err, "up.err");
		up.p_total = c->total;
		if (!job_start_ex(&up.job, c->url, part, err, 3600, 0, NULL, NULL, 0)) { up_fail(c, "Could not start curl."); return; }
		up.phase = UPH_FILEDL;
		return;
	}
	if (!up_space_ok(c->need_u + c->need_c)) { up_fail(c, "Not enough free space."); return; }
	int m = 0;
	int *ord = malloc((size_t)(c->dir.n ? c->dir.n : 1) * sizeof *ord);
	if (!ord) { up_fail(c, "Out of memory."); return; }
	for (int i = 0; i < c->dir.n; i++) if (c->need[i]) ord[m++] = i;
	qs_e = c->dir.e;
	qsort(ord, (size_t)m, sizeof *ord, cmp_off);
	up.nreg = 0;
	int too_many = 0;
	for (int k = 0; k < m; k++) {
		const ZxEnt *e = &c->dir.e[ord[k]];
		long long s = e->off, en = (long long)e->off + 30 + (long long)strlen(e->name) + 256 + e->csize;
		if (en > c->total) en = c->total;
		if (up.nreg && s - up.reg[up.nreg - 1].e <= 65536 && en - up.reg[up.nreg - 1].s <= 24LL * 1048576) {
			if (en - 1 > up.reg[up.nreg - 1].e) up.reg[up.nreg - 1].e = en - 1;
		} else if (up.nreg < 40) {
			up.reg[up.nreg].s = s;
			up.reg[up.nreg].e = en - 1;
			up.nreg++;
		} else { too_many = 1; break; }
	}
	free(ord);
	long long want = 0;
	for (int r = 0; r < up.nreg; r++) want += up.reg[r].e - up.reg[r].s + 1;
	up.full = !c->rangeok || too_many || up.nreg > 30 || want > c->total * 6 / 10;
	if (up.full) { up.nreg = 1; up.reg[0].s = 0; up.reg[0].e = c->total - 1; want = c->total; }
	up.p_total = want;
	up.ri = 0;
	logf_("update %s: %d files, %d region(s), %s, %lld bytes to fetch", c->name, c->nneed, up.nreg, up.full ? "whole file" : "ranges", want);
	if (!up_job(c, "up.r0", up.reg[0].s, up.full && !c->rangeok ? -1 : up.reg[0].e, NULL, 0, 3600)) { up_fail(c, "Could not start curl."); return; }
	up.phase = UPH_FETCH;
}

static void up_begin(Comp *c) {
	if (up.mode == 1) { up_begin_update(c); return; }
	comp_runtime_free(c);
	c->status = CS_CHECKING;
	c->msg[0] = 0;
	up.cancel = 0;
	snprintf(up.step, sizeof up.step, "Checking %s", c->name);
	if (!up_job(c, "up.size", 0, 0, "up.hdr", 4096, 60)) { up_fail(c, "Could not start curl."); return; }
	up.phase = UPH_SIZE;
}

static void up_cd_ready(Comp *c, const uint8_t *cd) {
	if (zx_parse_cd(cd, up.cd_size, up.cd_count, &c->dir)) { up_fail(c, "Could not read the package file list."); return; }
	c->need = calloc((size_t)(c->dir.n ? c->dir.n : 1), 1);
	if (!c->need) { up_fail(c, "Out of memory."); return; }
	free(up.tail);
	up.tail = NULL;
	up.cmp_i = 0;
	up.phase = UPH_CMP;
}

static uint8_t *read_range(const char *path, long long off, size_t len) {
	FILE *f = fopen(path, "rb");
	uint8_t *b = malloc(len ? len : 1);
	if (!f || !b) { if (f) fclose(f); free(b); return NULL; }
	if (fseek(f, (long)off, SEEK_SET) != 0 || fread(b, 1, len, f) != len) { fclose(f); free(b); return NULL; }
	fclose(f);
	return b;
}

static void comp_record(Comp *c) {
	if (!c->stamp[0]) return;
	scpy(c->rec, sizeof c->rec, c->stamp);
	c->nsent = 0;
	if (c->kind == 0 && c->dir.n) {
		int idx[3] = {0, c->dir.n / 2, c->dir.n - 1};
		for (int k = 0; k < 3; k++) {
			const char *nm = c->dir.e[idx[k]].name;
			size_t nl = strlen(nm);
			if (nl && nm[nl - 1] != '/' && !comp_keep(c, nm) && c->nsent < 3) scpy(c->sent[c->nsent++], sizeof c->sent[0], nm);
		}
	}
	state_save();
}

static int up_fast_ok(Comp *c) {
	char path[900];
	if (!c->stamp[0] || strcmp(c->stamp, c->rec)) return 0;
	if (c->kind == 1) { comp_path(c, "", path, sizeof path); return path_exists(path); }
	if (!c->nsent) return 0;
	for (int k = 0; k < c->nsent; k++) { comp_path(c, c->sent[k], path, sizeof path); if (!path_exists(path)) return 0; }
	return 1;
}

static void up_mod_msg(Comp *c, const char *base, char *out, size_t n) {
	(void)c;                       /* the date is drawn on the name line, not in this text */
	if (base != out) scpy(out, n, base);
}

/* The remote "last modified" stamp is known: decide, or go on to the file-by-file compare. */
static void up_after_stamp(Comp *c) {
	char m[200], h[32];
	stamp_pretty(c->stamp, c->pretty, sizeof c->pretty);
	human(h, sizeof h, c->total);
	if (c->kind == 1) {
		char path[900];
		comp_path(c, "", path, sizeof path);
		c->need_c = c->total;
		if (!path_exists(path)) { snprintf(m, sizeof m, "Not installed (%s)", h); up_finish_comp(c, CS_NEW, m); return; }
		if (up_fast_ok(c)) { up_mod_msg(c, "Up to date", m, sizeof m); up_finish_comp(c, CS_OK, m); return; }
		snprintf(m, sizeof m, c->rec[0] ? "Changed on the server (%s)" : "New version (%s)", h);
		up_mod_msg(c, m, c->msg, sizeof c->msg);
		up_finish_comp(c, CS_UPDATE, c->msg);
		return;
	}
	/* A bundle is never judged by its stamp alone: every file is compared (size, CRC32). */
	long long T = c->total < TAIL_BYTES ? c->total : TAIL_BYTES;
	up.tail_start = c->total - T;
	up.tail_file_off = c->rangeok ? 0 : up.tail_start;
	if (!up_job(c, "up.tail", up.tail_start, c->rangeok ? c->total - 1 : -1, NULL, 0, c->rangeok ? 120 : 1800)) { up_fail(c, "Could not start curl."); return; }
	up.phase = UPH_TAIL;
}

static void up_tail_ready(Comp *c) {
	char tp[480];
	up_tmp(tp, sizeof tp, "up.tail");
	size_t len = (size_t)(c->total - up.tail_start);
	up.tail = read_range(tp, up.tail_file_off, len);
	if (!up.tail) { up_fail(c, "The download was incomplete."); return; }
	if (zx_find_eocd(up.tail, len, &up.cd_off, &up.cd_size, &up.cd_count)) { up_fail(c, "Not a valid zip file."); return; }
	if ((long long)up.cd_off >= up.tail_start && (long long)up.cd_off + up.cd_size <= c->total) {
		up_cd_ready(c, up.tail + (up.cd_off - up.tail_start));
		return;
	}
	if (c->rangeok) {
		if (!up_job(c, "up.cd", up.cd_off, (long long)up.cd_off + up.cd_size - 1, NULL, 0, 120)) { up_fail(c, "Could not start curl."); return; }
		up.phase = UPH_CD;
		return;
	}
	up_fail(c, "Could not read the package file list.");
}

static void up_cmp_step(Comp *c) {
	uint32_t t0 = SDL_GetTicks();
	while (up.cmp_i < c->dir.n) {
		const ZxEnt *e = &c->dir.e[up.cmp_i];
		size_t nl = strlen(e->name);
		if (!(nl && e->name[nl - 1] == '/')) {
			if (!zx_safe_name(e->name)) { up_fail(c, "The package has an unsafe file path."); return; }
			char path[900];
			comp_path(c, e->name, path, sizeof path);
			int exists = path_exists(path), need;
			if (exists) c->nlocal++;
			if (!exists) need = 1;
			else if (comp_keep(c, e->name)) need = 0;
			else if ((uint32_t)file_size(path) != e->usize) need = 1;
			else { int ok; uint32_t crc = zx_crc32_file(path, &ok); need = !ok || crc != e->crc; }
			if (need) { c->need[up.cmp_i] = 1; c->nneed++; c->need_c += e->csize; c->need_u += e->usize; }
		}
		up.cmp_i++;
		if (SDL_GetTicks() - t0 > 25) { up.p_done = up.cmp_i; up.p_total = c->dir.n; return; }
	}
	char h[32];
	human(h, sizeof h, c->need_c);
	if (!c->nneed) { comp_record(c); char b[100]; snprintf(b, sizeof b, "Up to date (%d files)", c->dir.n); up_mod_msg(c, b, c->msg, sizeof c->msg); up_finish_comp(c, CS_OK, c->msg); }
	else if (!c->nlocal) { snprintf(c->msg, sizeof c->msg, "Not installed: %d files, %s", c->nneed, h); up_finish_comp(c, CS_NEW, c->msg); }
	else { char b[100]; snprintf(b, sizeof b, "%d of %d files changed, %s", c->nneed, c->dir.n, h); up_mod_msg(c, b, c->msg, sizeof c->msg); up_finish_comp(c, CS_UPDATE, c->msg); }
	/* keep dir/need for the update step */
}

static const char *up_extract_entry(Comp *c, const ZxEnt *e) {
	int r;
	for (r = 0; r < up.nreg; r++)
		if ((long long)e->off >= up.reg[r].s && (long long)e->off + 30 <= up.reg[r].e + 1) break;
	if (r >= up.nreg) return "A file was not downloaded.";
	char rp[480], rn[24];
	snprintf(rn, sizeof rn, "up.r%d", r);
	up_tmp(rp, sizeof rp, rn);
	uint8_t lh[30];
	FILE *f = fopen(rp, "rb");
	if (!f) return "Could not read the download.";
	if (fseek(f, (long)((long long)e->off - up.reg[r].s), SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || zx_u32(lh) != 0x04034b50u) {
		fclose(f);
		return "The package changed while downloading. Check again.";
	}
	long long dstart = (long long)e->off + 30 + zx_u16(lh + 26) + zx_u16(lh + 28);
	if (dstart + (long long)e->csize - 1 > up.reg[r].e) { fclose(f); return "The download was too short."; }
	if (e->method != 0 && e->method != 8) { fclose(f); return "Unsupported compression."; }
	uint8_t *cbuf = malloc(e->csize ? e->csize : 1), *out = malloc(e->usize ? e->usize : 1);
	if (!cbuf || !out) { fclose(f); free(cbuf); free(out); return "Out of memory."; }
	const char *err = NULL;
	if (fseek(f, (long)(dstart - up.reg[r].s), SEEK_SET) != 0 || fread(cbuf, 1, e->csize, f) != e->csize) err = "The download was too short.";
	fclose(f);
	if (!err) {
		if (e->method == 0) { if (e->csize != e->usize) err = "Corrupt file in the package."; else memcpy(out, cbuf, e->usize); }
		else if (zx_inflate(cbuf, e->csize, out, e->usize)) err = "Corrupt file in the package.";
	}
	if (!err && zx_crc32(0, out, e->usize) != e->crc) err = "A downloaded file failed its checksum. Try again.";
	if (!err) {
		char path[900], part[920];
		comp_path(c, e->name, path, sizeof path);
		mkparent(path);
		snprintf(part, sizeof part, "%s.gitroms-part", path);
		FILE *w = fopen(part, "wb");
		if (!w) err = "Could not write to the ports folder.";
		else {
			if (e->usize && fwrite(out, 1, e->usize, w) != e->usize) err = "Could not write (card full?).";
			if (fclose(w) != 0 && !err) err = "Could not write (card full?).";
			if (!err) {
				chmod(part, (e->mode ? e->mode : 0644) | 0600);
				if (rename(part, path) != 0) err = "Could not replace a file.";
			}
			if (err) remove(part);
		}
	}
	free(cbuf);
	free(out);
	return err;
}

static void up_extract_step(Comp *c) {
	uint32_t t0 = SDL_GetTicks();
	snprintf(up.step, sizeof up.step, "Installing files");
	while (up.ex_i < c->dir.n) {
		if (c->need[up.ex_i]) {
			const ZxEnt *e = &c->dir.e[up.ex_i];
			const char *err = up_extract_entry(c, e);
			if (err) { logf_("extract %s: %s", e->name, err); up_fail(c, err); return; }
			up.p_done += e->usize;
		}
		up.ex_i++;
		if (SDL_GetTicks() - t0 > 30) return;
	}
	char m[120];
	snprintf(m, sizeof m, "Updated %d files", c->nneed);
	up.updated[up.ci] = 1;
	if (c->self) up.restart_needed = 1;
	comp_record(c);
	comp_runtime_free(c);
	up_finish_comp(c, CS_OK, m);
}

static void up_do_cancel(void) {
	job_cancel(&up.job);
	if (up.ci >= 0 && up.ci < ncomp) {
		Comp *c = &comp[up.ci];
		char part[920], path[900];
		if (c->kind == 1) { comp_path(c, "", path, sizeof path); snprintf(part, sizeof part, "%s.gitroms-part", path); remove(part); }
		c->status = CS_ERROR;
		scpy(c->msg, sizeof c->msg, "Cancelled. Press Select to check again.");
		comp_runtime_free(c);
	}
	up_cleanup_tmp();
	up.phase = UPH_NONE;
	up.cancel = 0;
	if (screen == SC_UPDATING) screen = SC_UPDATES;
}

static void up_step(uint32_t now) {
	(void)now;
	if (up.phase == UPH_NONE) return;
	if (up.cancel) { up_do_cancel(); return; }
	Comp *c = &comp[up.ci];
	char p[480];
	switch (up.phase) {
	case UPH_FILEDL: {
		char path[900], part[920];
		comp_path(c, "", path, sizeof path);
		snprintf(part, sizeof part, "%s.gitroms-part", path);
		up.p_done = file_size(part);
		if (!job_poll(&up.job)) return;
		if (up.job.rc != 0) { remove(part); up_fail_job(c); return; }
		if (c->total > 0 && file_size(part) != c->total) { remove(part); up_fail(c, "The download was incomplete."); return; }
		chmod(part, 0644);
		if (rename(part, path) != 0) { remove(part); up_fail(c, "Could not replace the file."); return; }
		up.updated[up.ci] = 1;
		comp_record(c);
		up_finish_comp(c, CS_OK, "Updated");
		return;
	}
	case UPH_SIZE: {
		if (!job_poll(&up.job)) return;
		if (up.job.rc != 0 && up.job.rc != 63) { up_fail_job(c); return; }
		int ro;
		up_tmp(p, sizeof p, "up.hdr");
		long long tot = hdr_total(p, &ro);
		if (up.job.rc == 63) ro = 0;                 /* the server ignored the range request */
		if (tot <= 0) { up_fail(c, "Could not read the file size."); return; }
		c->total = tot;
		c->rangeok = ro;
		char v[200];
		c->stamp[0] = 0;
		if (hdr_find(p, "last-modified:", v, sizeof v)) scpy(c->stamp, sizeof c->stamp, v);
		else if (hdr_find(p, "etag:", v, sizeof v)) snprintf(c->stamp, sizeof c->stamp, "etag %s", v);
		if (!strncmp(c->src, "commit:", 7)) {
			char repo[120], path[120], url[400], out[480], err[480];
			if (sscanf(c->src + 7, "%119[^:]:%119s", repo, path) == 2) {
				snprintf(url, sizeof url, "%s/repos/%s/commits?path=%s&per_page=1", g_api, repo, path);
				up_tmp(out, sizeof out, "up.stamp");
				up_tmp(err, sizeof err, "up.err");
				remove(out);
				if (job_start_ex(&up.job, url, out, err, 30, 1, NULL, NULL, 65536)) { up.phase = UPH_STAMP; return; }
			}
		}
		up_after_stamp(c);
		return;
	}
	case UPH_STAMP: {
		if (!job_poll(&up.job)) return;
		if (up.job.rc == 0) {                       /* on any failure keep the ETag stamp */
			up_tmp(p, sizeof p, "up.stamp");
			size_t n;
			char *body = read_file(p, 65536, &n);
			J *root = body ? json_parse(body, n) : NULL;
			J *first = (root && root->t == 4) ? root->kids : NULL;
			J *cm = jget(first, "commit"), *cr = jget(cm, "committer");
			const char *d = jstr(cr, "date");
			if (d) snprintf(c->stamp, sizeof c->stamp, "%s", d);
			j_free(root);
			free(body);
		}
		up_after_stamp(c);
		return;
	}
	case UPH_TAIL:
		if (!job_poll(&up.job)) return;
		if (up.job.rc != 0) { up_fail_job(c); return; }
		up_tail_ready(c);
		return;
	case UPH_CD: {
		if (!job_poll(&up.job)) return;
		if (up.job.rc != 0) { up_fail_job(c); return; }
		up_tmp(p, sizeof p, "up.cd");
		uint8_t *cd = read_range(p, 0, up.cd_size);
		if (!cd) { up_fail(c, "The download was incomplete."); return; }
		up_cd_ready(c, cd);
		free(cd);
		return;
	}
	case UPH_CMP: up_cmp_step(c); return;
	case UPH_FETCH: {
		char rn[24];
		snprintf(rn, sizeof rn, "up.r%d", up.ri);
		up_tmp(p, sizeof p, rn);
		up.p_done = up.p_base + file_size(p);
		if (!job_poll(&up.job)) return;
		if (up.job.rc != 0) { up_fail_job(c); return; }
		long long want = up.reg[up.ri].e - up.reg[up.ri].s + 1;
		if (file_size(p) != want) { up_fail(c, "The download was incomplete."); return; }
		up.p_base += want;
		up.ri++;
		if (up.ri < up.nreg) {
			snprintf(rn, sizeof rn, "up.r%d", up.ri);
			if (!up_job(c, rn, up.reg[up.ri].s, up.reg[up.ri].e, NULL, 0, 3600)) { up_fail(c, "Could not start curl."); return; }
			return;
		}
		up.ex_i = 0;
		up.p_done = 0;
		up.p_total = c->need_u;
		up.phase = UPH_EXTRACT;
		return;
	}
	case UPH_EXTRACT: up_extract_step(c); return;
	}
}

static void up_start_check(const int *only) {
	if (up.phase != UPH_NONE) return;
	up.mode = 0;
	for (int i = 0; i < ncomp; i++) up.flag[i] = only ? only[i] : 1;
	up.ci = -1;
	up.cancel = 0;
	up_advance();
}

static int up_available(void) {
	int n = 0;
	for (int i = 0; i < ncomp; i++) if (comp[i].status == CS_UPDATE || comp[i].status == CS_NEW) n++;
	return n;
}

static void up_start_update(void) {
	if (up.phase != UPH_NONE || !up_available()) return;
	up.mode = 1;
	memset(up.updated, 0, sizeof up.updated);
	up.ci = -1;
	up.cancel = 0;
	screen = SC_UPDATING;
	up_advance();
}

static void updates_open(void) {
	if (!ncomp) comps_load();
	comp_sel = 0;
	screen = SC_UPDATES;
	if (up.phase == UPH_NONE) up_start_check(NULL);
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
		else if (a == A_INFO) { prev_screen = SC_LIST; if (!si_n) si_start(); screen = SC_SYSINFO; }
		else if (a == A_OK && nitems) {
			Item *it = &items[cur.sel];
			Frame f;
			memset(&f, 0, sizeof f);
			f.kind = K_DIR;
			if (it->kind == K_UPD) {
				up_from = SC_LIST;
				updates_open();
			} else if (it->kind == K_REPO) {
				scpy(f.repo, sizeof f.repo, it->path);
				scpy(f.branch, sizeof f.branch, it->branch);
				begin_load(&f, M_PUSH, 0, 0);
			} else if (it->kind == K_REL) {
				f.kind = FK_REL;
				scpy(f.repo, sizeof f.repo, cur.repo);
				scpy(f.branch, sizeof f.branch, cur.branch);
				scpy(f.path, sizeof f.path, "@releases");
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
				cf.conflict_sel = (cf.has_inst && cf.upd) ? 0 : 1; /* never replace by default unless an update is waiting */
				screen = SC_CONFLICT;
			} else begin_download(cf.name, 0);
		}
		break;
	}
	case SC_CONFLICT:
		if (a == A_UP) cf.conflict_sel = (cf.conflict_sel + 2) % 3;
		else if (a == A_DOWN) cf.conflict_sel = (cf.conflict_sel + 1) % 3;
		else if (a == A_BACK) screen = SC_LIST;
		else if (a == A_OK) {
			if (cf.conflict_sel == 0) begin_download(cf.name, 1);
			else if (cf.conflict_sel == 1) begin_download(cf.copy_name, 0);
			else screen = SC_LIST;
		}
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
		else if (a == A_INFO) { up_from = SC_ERROR; updates_open(); }   /* updates do not need the GitHub API */
		else if (a == A_START || a == A_SELECT) load_root();
		break;
	case SC_HISTORY:
		if (a == A_UP && hist_scroll > 0) hist_scroll--;
		else if (a == A_DOWN && hist_scroll < nhist - 10) hist_scroll++;
		else if (a == A_BACK || a == A_START) screen = prev_screen;
		break;
	case SC_UPDATES:
		if (a == A_UP && comp_sel > 0) comp_sel--;
		else if (a == A_DOWN && comp_sel < ncomp - 1) comp_sel++;
		else if (a == A_OK) {
			if (up.phase == UPH_NONE && up_available()) up_start_update();
			else if (up.phase == UPH_NONE && up.restart_needed) g_run = 0;
		} else if (a == A_SELECT) up_start_check(NULL);
		else if (a == A_BACK) { if (up.phase != UPH_NONE) up.cancel = 1; screen = up_from; }
		break;
	case SC_UPDATING:
		if (a == A_BACK) up.cancel = 1;
		break;
	case SC_SYSINFO: {
		int rows = 12, mx = si_n > rows ? si_n - rows : 0;
		if (a == A_UP && si_top > 0) si_top--;
		else if (a == A_DOWN && si_top < mx) si_top++;
		else if (a == A_PGUP || a == A_LEFT) si_top = si_top > 10 ? si_top - 10 : 0;
		else if (a == A_PGDN || a == A_RIGHT) si_top = si_top + 10 < mx ? si_top + 10 : mx;
		else if (a == A_SELECT && !si_running) si_start();
		else if (a == A_BACK || a == A_INFO) { si_stop(); screen = prev_screen; }
		break;
	}
	case SC_EXIT:
		if (a == A_OK) g_run = 0;
		else if (a == A_BACK) screen = have_cur ? SC_LIST : SC_ERROR;
		break;
	}
}

/* Input model: one "held" state per action, shared by every source.
 * The pad reaches us twice (gptokeyb as a key, and SDL as a controller button),
 * and the OS also auto-repeats held keys. Handling each event as a press made
 * one tap jump several rows. Now: a press that arrives while the other source
 * already holds the action is merged, OS auto-repeat events are ignored, and
 * holding up/down repeats on our own steady timer. */
#define SRC_KEY 1
#define SRC_PAD 2
#define REPEAT_DELAY_MS 450
#define REPEAT_RATE_MS 90
#define BOUNCE_MS 150   /* a second fresh press of one action inside this is the same tap */
/* verified: SDL's own key/button state confirmed the press when it arrived, so a later
 * "released" can be checked against that state. A hold that cannot be verified never
 * auto-repeats: a lost release must not turn one tap into a run of rows. */
static struct { int src, scan, verified; uint32_t next_rep, last_fire; } hold[16];
static void *g_js[8];
static int g_njs, g_test_inject;
static int g_trace = 500;     /* the first input events of a run are written to gitroms.log */
#define TRACE(...) do { if (g_trace > 0) { g_trace--; logf_(__VA_ARGS__); } } while (0)
static const char *ANAME[] = {"none", "up", "down", "left", "right", "ok", "back", "start", "select", "pgup", "pgdn", "info"};

static void js_remember(void *j) {
	if (!j) return;
	for (int i = 0; i < g_njs; i++) if (g_js[i] == j) return;
	if (g_njs < 8) g_js[g_njs++] = j;
}

static int pad_down_now(int a);
static int key_down_now(int scan) {
	const uint8_t *ks = SDL_GetKeyboardState(NULL);
	return ks && scan > 0 && scan < 512 && ks[scan];
}
/* src: which source; scan: the key's scancode (keys only). */
static void input_press(int a, int src, uint32_t now, int scan) {
	if (a <= A_NONE || a > A_INFO) return;
	if (hold[a].src) {                                       /* the other source already holds it: same tap */
		hold[a].src |= src;
		if (src == SRC_KEY) hold[a].scan = scan;
		TRACE("in %s +%s (merged)", ANAME[a], src == SRC_KEY ? "key" : "pad");
		return;
	}
	hold[a].src = src;
	hold[a].scan = scan;
	hold[a].verified = g_test_inject || (src == SRC_KEY ? key_down_now(scan) : pad_down_now(a));
	hold[a].next_rep = now + REPEAT_DELAY_MS;
	if (hold[a].last_fire && now - hold[a].last_fire < BOUNCE_MS) {
		TRACE("in %s %s ignored (bounce %ums)", ANAME[a], src == SRC_KEY ? "key" : "pad", (unsigned)(now - hold[a].last_fire));
		return;
	}
	hold[a].last_fire = now;
	TRACE("in %s %s fire (verified %d)", ANAME[a], src == SRC_KEY ? "key" : "pad", hold[a].verified);
	act(a);
}

static void input_release(int a, int src) {
	if (a <= A_NONE || a > A_INFO) return;
	hold[a].src &= ~src;
	TRACE("in %s %s released", ANAME[a], src == SRC_KEY ? "key" : "pad");
}

static void input_tick(uint32_t now) {
	/* A release that never arrived is caught here: SDL's own state says the control is up. */
	if (!g_test_inject)
		for (int a = A_UP; a <= A_INFO; a++) {
			if (!hold[a].src || !hold[a].verified) continue;
			if ((hold[a].src & SRC_KEY) && !key_down_now(hold[a].scan)) { hold[a].src &= ~SRC_KEY; TRACE("in %s key release was missing", ANAME[a]); }
			if ((hold[a].src & SRC_PAD) && !pad_down_now(a)) { hold[a].src &= ~SRC_PAD; TRACE("in %s pad release was missing", ANAME[a]); }
		}
	static const int rep[] = {A_UP, A_DOWN};              /* paging and buttons never auto-repeat */
	for (int i = 0; i < 2; i++) {
		int a = rep[i];
		if (hold[a].src && hold[a].verified && now >= hold[a].next_rep) {
			hold[a].last_fire = now;
			hold[a].next_rep = now + REPEAT_RATE_MS;
			TRACE("in %s repeat", ANAME[a]);
			act(a);
		}
	}
}

static int key_action(int sc) {
	switch (sc) {
	case 82: return A_UP; case 81: return A_DOWN; case 80: return A_LEFT; case 79: return A_RIGHT;
	case 29: case 44: return A_OK;           /* z, space */
	case 27: case 41: return A_BACK;         /* x, escape */
	case 40: case 88: return A_START;        /* return, keypad enter */
	case 43: return A_SELECT;                /* tab */
	case 20: return A_PGUP; case 8: return A_PGDN; /* q, e */
	case 6: return A_INFO;                   /* c (pad X via gptokeyb) */
	}
	return A_NONE;
}
/* Raw joystick buttons. The defaults are the R36S layout recorded in
 * BibooSGBA's sgba/controls-arkos.json (d-pad = buttons 8-11, A = 1, B = 0).
 * Override any of them in gitroms.conf, e.g. "pad_a=0". The SDL "controller"
 * layer is not used: its mapping can disagree with the device's own. */
static const char *ROLE_NAME[] = {"up", "down", "left", "right", "a", "b", "x", "y", "l1", "r1", "select", "start"};
static const int ROLE_ACT[] = {A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_INFO, A_SELECT, A_PGUP, A_PGDN, A_SELECT, A_START};
static int role_btn[12] = {8, 9, 10, 11, 1, 0, 2, 3, 4, 5, 12, 13};
static int btn_act[32];
static int g_keys_only;   /* gptokeyb is delivering the pad as keys: ignore raw joystick events */

static void build_btn_map(void) {
	memset(btn_act, 0, sizeof btn_act);
	for (int r = 0; r < 12; r++)
		if (role_btn[r] >= 0 && role_btn[r] < 32) btn_act[role_btn[r]] = ROLE_ACT[r];
}

/* Is any pad control mapped to action `a` physically down right now? */
static int pad_down_now(int a) {
	for (int j = 0; j < g_njs; j++) {
		for (int r = 0; r < 12; r++)
			if (ROLE_ACT[r] == a && role_btn[r] >= 0 && role_btn[r] < 32 && SDL_JoystickGetButton(g_js[j], role_btn[r])) return 1;
		uint8_t h = SDL_JoystickGetHat(g_js[j], 0);
		if ((a == A_UP && (h & 1)) || (a == A_RIGHT && (h & 2)) || (a == A_DOWN && (h & 4)) || (a == A_LEFT && (h & 8))) return 1;
	}
	return 0;
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

/* Like text_fit, but keeps the end of the name (the part that tells builds apart). */
static void text_fit_mid(int x, int y, const char *s, uint32_t c, int sc, int maxw) {
	int cap = maxw / (8 * sc), n = (int)strlen(s);
	if (n <= cap || cap < 16) { text_fit(x, y, s, c, sc, maxw); return; }
	if (cap > 190) cap = 190;
	char buf[200];
	int tail = 10, head = cap - 3 - tail;
	memcpy(buf, s, (size_t)head);
	memcpy(buf + head, "...", 3);
	memcpy(buf + head + 3, s + n - tail, (size_t)tail + 1);
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
		snprintf(right, sizeof right, "%d repos", nitems > 0 && items[0].kind == K_UPD ? nitems - 1 : nitems);
	} else if (cur.kind == FK_REL) {
		const char *r = strchr(cur.repo, '/');
		snprintf(title, sizeof title, "%s releases", r ? r + 1 : cur.repo);
		scpy(right, sizeof right, nitems && items[0].desc[0] ? items[0].desc : "latest");
	} else {
		const char *r = strchr(cur.repo, '/');
		snprintf(title, sizeof title, "%s%s%s", r ? r + 1 : cur.repo, cur.path[0] ? "/" : "", cur.path);
		scpy(right, sizeof right, cur.branch);
	}
	header(title, right);
	if (!nitems) text(32, 90, cur.kind == K_REPO ? "No public repositories found." : cur.kind == FK_REL ? "No ROM or zip files in the latest release." : "No ROM files or folders here.", C_MUTED, 2);
	for (int i = 0; i < ROWS; i++) {
		int idx = cur.scroll + i;
		if (idx >= nitems) break;
		Item *it = &items[idx];
		int y = 56 + i * ROWH, sel = idx == cur.sel;
		if (sel) fill(8, y, VW - 24, 28, C_SIGNAL);
		uint32_t fg = sel ? C_INK : C_FG;
		char nm[220];
		if (it->kind == K_FILE) {
			text_fit_mid(16, y + 6, it->name, fg, 2, VW - 236);
			char h[32];
			human(h, sizeof h, it->size);
			int hw = (int)strlen(h) * 16;
			text(VW - 24 - hw, y + 6, h, sel ? C_INK : C_MUTED, 2);
			Inst *e = inst_find(cur.repo, it->path);
			if (e) {
				int upd = it->token[0] && strcmp(e->token, it->token) != 0;
				text(VW - 24 - hw - 56, y + 6, upd ? "UPD" : "OK", upd ? (sel ? C_INK : C_SIGNAL) : (sel ? C_INK : C_GOOD), 2);
			}
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
	footer("A open B back X info START log SEL ref");
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
		text(32, y, "Status", C_MUTED, 2);
		if (cf.has_inst) text(160, y, cf.upd ? "Update available" : "Up to date", cf.upd ? C_SIGNAL : C_GOOD, 2);
		else text(160, y, "Not tracked yet", C_MUTED, 2);
		y += 28;
		text(32, y, "Save to", C_MUTED, 2);
		text_fit(160, y, g_romroot, C_FG, 2, VW - 186);
		y += 40;
		text(32, y + 6, "System", C_MUTED, 2);
		fill(160, y, 300, 30, C_RAISED);
		snprintf(buf, sizeof buf, "<  %s  >", SYSTEMS[cf.sys_idx]);
		text_fit(176, y + 7, buf, C_SIGNAL, 2, 270);
		footer(cf.has_inst && cf.upd ? "A update  B cancel  L/R folder" : "A save  B cancel  L/R folder");
		break;
	}
	case SC_CONFLICT: {
		header("File already exists", NULL);
		int y = text_wrap(32, 70, cf.name, C_FG, 2, VW - 64, 2);
		text(32, y + 6, "Nothing is replaced unless chosen.", C_MUTED, 2);
		const char *opt[3];
		char o2[340];
		opt[0] = cf.has_inst ? (cf.upd ? "Update: replace with new version" : "Re-download: replace file") : "Replace the existing file";
		snprintf(o2, sizeof o2, "Save as %s", cf.copy_name);
		opt[1] = o2;
		opt[2] = "Keep existing file (cancel)";
		y += 50;
		for (int i = 0; i < 3; i++) {
			if (i == cf.conflict_sel) fill(24, y, VW - 48, 34, C_SIGNAL);
			text_fit(36, y + 9, opt[i], i == cf.conflict_sel ? C_INK : C_FG, 2, VW - 72);
			y += 42;
		}
		footer("A choose  B cancel  UP/DN move");
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
		footer(err_fatal ? "A quit" : have_cur ? "A/B back  START retry  X updates" : "A retry  B quit  X updates");
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
	case SC_UPDATES: {
		header("Updates", NULL);
		for (int i = 0; i < ncomp; i++) {
			Comp *c = &comp[i];
			int y = 56 + i * 58;
			if (i == comp_sel) fill(8, y, VW - 16, 54, C_RAISED);
			int dw = c->pretty[0] ? (int)strlen(c->pretty) * 16 + 12 : 0;   /* date, right aligned, scale 2 */
			text_fit(16, y + 6, c->name, C_FG, 2, VW - 40 - dw);
			if (dw) text_fit(VW - 24 - (dw - 12), y + 6, c->pretty, C_MUTED, 2, dw - 12);
			uint32_t col = c->status == CS_OK ? C_GOOD : (c->status == CS_UPDATE || c->status == CS_NEW) ? C_SIGNAL : c->status == CS_ERROR ? C_BAD : C_MUTED;
			const char *m = c->status == CS_CHECKING ? "Checking..." : c->status == CS_IDLE ? "Waiting..." : c->status == CS_UPDATING ? "Updating..." : c->msg;
			text_fit(16, y + 30, m, col, 2, VW - 40);
		}
		if (up.restart_needed) text_fit(16, 410, "GitROMs was updated. Quit and reopen it.", C_SIGNAL, 2, VW - 32);
		if (up.phase != UPH_NONE) footer("Checking...  B back");
		else if (up_available()) footer("A update  SEL check again  B back");
		else if (up.restart_needed) footer("A quit  SEL check again  B back");
		else footer("SEL check again  B back");
		break;
	}
	case SC_UPDATING: {
		header("Updating", NULL);
		Comp *c = &comp[up.ci < 0 ? 0 : up.ci];
		text_fit(32, 80, c->name, C_FG, 2, VW - 64);
		text_fit(32, 112, up.step, C_MUTED, 2, VW - 64);
		progress(32, 150, VW - 64, up.p_total > 0 ? (double)up.p_done / (double)up.p_total : -1, now);
		char a[32], b[32];
		human(a, sizeof a, up.p_done);
		human(b, sizeof b, up.p_total);
		snprintf(buf, sizeof buf, "%s of %s", a, b);
		if (up.phase == UPH_FETCH || up.phase == UPH_EXTRACT || up.phase == UPH_FILEDL) text(32, 184, buf, C_MUTED, 2);
		footer("B cancel");
		break;
	}
	case SC_SYSINFO: {
		int rows = 12;
		if (si_running) {
			header("System info", NULL);
			text(32, 150, "Collecting information...", C_FG, 2);
			progress(120, 210, VW - 240, -1, now);
			footer("B back");
			break;
		}
		snprintf(buf, sizeof buf, "System info %d/%d", si_n ? si_top + 1 : 0, si_n);
		header(buf, NULL);
		for (int i = 0; i < rows && si_top + i < si_n; i++) text(12, 46 + i * 32, si_lines[si_top + i], C_FG, 2);
		footer("UP/DN scroll L/R page SEL redo B back");
		break;
	}
	case SC_EXIT:
		header("Quit GitROMs?", NULL);
		text(32, 100, "Press A to quit, B to stay.", C_FG, 2);
		footer("A quit  B stay");
		break;
	}
}

/* ----------------------------------------------------------- test driver */
static char *tsteps[256];
static int ntsteps, tstep, test_mode;
static uint32_t twait_until;
static char *tshot;

static int act_from_name(const char *n) {
	static const char *names[] = {"", "up", "down", "left", "right", "confirm", "cancel", "start", "select", "pgup", "pgdn", "info"};
	for (int i = 1; i <= A_INFO; i++) if (!strcmp(n, names[i])) return i;
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
	g_test_inject = 1;      /* injected events cannot be checked against SDL state */
	static char copy[16384];
	scpy(copy, sizeof copy, spec);
	for (char *s = strtok(copy, ";"); s && ntsteps < 256; s = strtok(NULL, ";")) tsteps[ntsteps++] = s;
	test_mode = 1;
}

static void test_step(uint32_t now) {
	if (!test_mode || tshot) return;
	if (now < twait_until) return;
	if (tstep >= ntsteps) { g_run = 0; return; }
	char *st = tsteps[tstep];
	if (!strcmp(st, "idle")) {
		if (!task_active() && up.phase == UPH_NONE) { tstep++; twait_until = now + 200; }
		return;
	}
	tstep++;
	if (!strncmp(st, "wait=", 5)) twait_until = now + (uint32_t)(atof(st + 5) * 1000);
	else if (!strncmp(st, "act=", 4)) { act(act_from_name(st + 4)); twait_until = now + 100; }
	else if (!strncmp(st, "shot=", 5)) tshot = st + 5;
	else if (!strncmp(st, "kd=", 3)) input_press(act_from_name(st + 3), SRC_KEY, now, 0);
	else if (!strncmp(st, "ku=", 3)) input_release(act_from_name(st + 3), SRC_KEY);
	else if (!strncmp(st, "pd=", 3)) input_press(act_from_name(st + 3), SRC_PAD, now, 0);
	else if (!strncmp(st, "pu=", 3)) input_release(act_from_name(st + 3), SRC_PAD);
	else if (!strcmp(st, "logsel")) logf_("sel=%d", have_cur ? cur.sel : -1);
	else if (!strcmp(st, "logscr")) logf_("scr=%d sp=%d", screen, sp);
	else if (!strcmp(st, "logupd")) {
		for (int i = 0; i < ncomp; i++) logf_("upd[%d] %s st=%d nneed=%d date=[%s] msg=%s", i, comp[i].name, comp[i].status, comp[i].nneed, comp[i].pretty, comp[i].msg);
	}
	else if (!strncmp(st, "jb=", 3) || !strncmp(st, "ju=", 3)) { /* raw joystick button down / up */
		SDL_Event e;
		memset(&e, 0, sizeof e);
		e.type = st[1] == 'b' ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
		((uint8_t *)&e)[12] = (uint8_t)atoi(st + 3);
		((uint8_t *)&e)[13] = st[1] == 'b';
		SDL_PushEvent(&e);
	} else if (!strncmp(st, "hat=", 4)) {
		SDL_Event e;
		memset(&e, 0, sizeof e);
		e.type = SDL_JOYHATMOTION;
		((uint8_t *)&e)[13] = (uint8_t)atoi(st + 4);
		SDL_PushEvent(&e);
	} else if (!strncmp(st, "key=", 4) || !strncmp(st, "keyu=", 5)) { /* keyboard scancode down / up */
		SDL_Event e;
		int32_t sc = atoi(strchr(st, '=') + 1);
		memset(&e, 0, sizeof e);
		e.type = st[3] == '=' ? SDL_KEYDOWN : SDL_KEYUP;
		memcpy((uint8_t *)&e + 16, &sc, 4);
		SDL_PushEvent(&e);
	}
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
		else if (!strncmp(line, "pad_", 4) && v[0])
			for (int r = 0; r < 12; r++) if (!strcmp(line + 4, ROLE_NAME[r])) role_btn[r] = atoi(v);
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
	if ((e = getenv("GITROMS_PORTS")) && *e) scpy(g_ports, sizeof g_ports, e);
	else {
		scpy(g_ports, sizeof g_ports, g_home);                 /* <ports>/gitroms/data -> <ports> */
		for (int up_dirs = 0; up_dirs < 2; up_dirs++) { char *sl = strrchr(g_ports, '/'); if (sl) *sl = 0; }
	}
	snprintf(g_instpath, sizeof g_instpath, "%s/installed.txt", g_home);
	snprintf(g_statepath, sizeof g_statepath, "%s/updates.txt", g_home);
	inst_load();

	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK) != 0) {
		logf_("SDL_Init (with joystick) failed: %s", SDL_GetError());
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
	g_keys_only = (e = getenv("GITROMS_INPUT")) && !strcmp(e, "keys");
	build_btn_map();
	logf_("input: %s; buttons up%d down%d left%d right%d a%d b%d x%d y%d l1_%d r1_%d select%d start%d", g_keys_only ? "keys from gptokeyb (pad ignored)" : "raw joystick + keys",
	      role_btn[0], role_btn[1], role_btn[2], role_btn[3], role_btn[4], role_btn[5], role_btn[6], role_btn[7], role_btn[8], role_btn[9], role_btn[10], role_btn[11]);
	if (!g_keys_only)
		for (int i = 0, n = SDL_NumJoysticks(); i < n; i++) js_remember(SDL_JoystickOpen(i));

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
			else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
				int32_t scan;
				memcpy(&scan, (uint8_t *)&ev + 16, 4);
				int a = key_action((int)scan);
				if (ev.type == SDL_KEYUP) input_release(a, SRC_KEY);
				else if (!((uint8_t *)&ev)[13]) input_press(a, SRC_KEY, t0, (int)scan);   /* ignore OS auto-repeat */
			} else if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
				int b = ((uint8_t *)&ev)[12], a = (b >= 0 && b < 32) ? btn_act[b] : A_NONE;
				if (g_keys_only) continue;
				if (ev.type == SDL_JOYBUTTONDOWN) input_press(a, SRC_PAD, t0, 0);
				else input_release(a, SRC_PAD);
			} else if (ev.type == SDL_JOYHATMOTION) {
				static int hat_prev;
				int v = ((uint8_t *)&ev)[13];
				static const int bit[4] = {1, 2, 4, 8}, hact[4] = {A_UP, A_RIGHT, A_DOWN, A_LEFT};
				if (g_keys_only) continue;
				for (int i = 0; i < 4; i++) {
					if ((v & bit[i]) && !(hat_prev & bit[i])) input_press(hact[i], SRC_PAD, t0, 0);
					if (!(v & bit[i]) && (hat_prev & bit[i])) input_release(hact[i], SRC_PAD);
				}
				hat_prev = v;
			} else if (ev.type == SDL_JOYDEVICEADDED) {
				int32_t which;
				memcpy(&which, (uint8_t *)&ev + 8, 4);
				if (!g_keys_only) js_remember(SDL_JoystickOpen(which));
			}
		}
		input_tick(t0);
		up_step(t0);
		update_task(t0);
		si_poll();
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

	si_stop();
	if (up.phase != UPH_NONE) up_do_cancel();
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
