/* Crash and unclean-exit reports for GitROMs itself.
 *
 * A fatal signal writes <ports>/sgba/reports/gitroms-crash-<epoch>.txt (the same folder SGBA uses, so [Send reports] posts it):
 * version, source hash, signal, fault address, what the app was doing, a backtrace as offsets into the program (symbolize them
 * with an unstripped build of the same source: addr2line -f -e gitroms.unstripped <offset>) and the last log lines.
 * A marker file (data/.running) is written at start and removed on a clean exit or after a crash report. If it is still there at
 * the next start the last run was killed, lost power or hung, and gitroms-unclean-<time>.txt is written from it and the old log.
 * Only plain write() and a few libc calls are used after the signal, the usual compromise for a crash handler. */
#include <ucontext.h>

#define DG_LINES 40
#define DG_W 200
#ifndef GITROMS_SRC
#define GITROMS_SRC "unknown"
#endif

static char dg_ring[DG_LINES][DG_W];
static volatile unsigned dg_n;
static char dg_dir[440], dg_marker[460], dg_state[96];
static volatile int dg_screen = -1, dg_phase = -1, dg_comp = -1;     /* what the app was doing, refreshed by the main loop */
static char dg_altstack[65536];
static time_t dg_t0;
#define DG_STR2(x) #x
#define DG_STR(x) DG_STR2(x)
#ifdef GITROMS_SRCHASH
#undef GITROMS_SRC
#define GITROMS_SRC DG_STR(GITROMS_SRCHASH)
#endif

static void dg_ring_add(const char *line) {
	unsigned i = dg_n++ % DG_LINES;
	size_t l = strlen(line);
	if (l >= DG_W) l = DG_W - 1;
	memcpy(dg_ring[i], line, l);
	dg_ring[i][l] = 0;
}

static void dg_put(int fd, const char *s) { size_t l = strlen(s); while (l) { ssize_t w = write(fd, s, l); if (w <= 0) return; s += w; l -= (size_t)w; } }
static void dg_num(int fd, long long v) {
	char b[24]; int i = 23; b[i] = 0;
	int neg = v < 0; unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
	do { b[--i] = (char)('0' + u % 10); u /= 10; } while (u && i > 1);
	if (neg) b[--i] = '-';
	dg_put(fd, b + i);
}
static void dg_hex(int fd, unsigned long long v) {
	char b[24]; int i = 23; b[i] = 0;
	do { b[--i] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v && i > 2);
	b[--i] = 'x'; b[--i] = '0';
	dg_put(fd, b + i);
}

static void dg_marker_set(const char *state) {
	if (!dg_marker[0] || !strcmp(state, dg_state)) return;
	snprintf(dg_state, sizeof dg_state, "%s", state);
	FILE *f = fopen(dg_marker, "wb");
	if (!f) return;
	fprintf(f, "version=%s\nsource=%s\nstart=%lld\nstate=%s\n", VERSION, GITROMS_SRC, (long long)dg_t0, dg_state);
	fclose(f);
}

static void dg_on_crash(int sig, siginfo_t *si, void *uc) {
	(void)uc;
	signal(sig, SIG_DFL);
	if (dg_dir[0]) {
		char path[520];
		time_t now = time(NULL);
		snprintf(path, sizeof path, "%s/gitroms-crash-%lld.txt", dg_dir, (long long)now);
		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			dg_put(fd, "GitROMs report: crash\nversion: "); dg_put(fd, VERSION);
			dg_put(fd, "\nsource: "); dg_put(fd, GITROMS_SRC);
			dg_put(fd, "\nsignal: "); dg_num(fd, sig);
			dg_put(fd, sig == SIGSEGV ? " (segmentation fault)" : sig == SIGABRT ? " (abort)" : sig == SIGBUS ? " (bus error)" : sig == SIGILL ? " (illegal instruction)" : sig == SIGFPE ? " (arithmetic)" : "");
			dg_put(fd, "\nfault address: "); dg_hex(fd, (unsigned long long)(uintptr_t)(si ? si->si_addr : 0));
			dg_put(fd, "\ntime: "); dg_num(fd, (long long)now);
			dg_put(fd, "\nscreen: "); dg_num(fd, dg_screen);
			dg_put(fd, "  update phase: "); dg_num(fd, dg_phase);
			dg_put(fd, "  component: "); dg_num(fd, dg_comp);
			dg_put(fd, "  state: "); dg_put(fd, dg_state);
			dg_put(fd, "\n");
			int m = open("/proc/self/maps", O_RDONLY);
			if (m >= 0) {                                         /* the first mapping is the program: subtract its start from each frame */
				char mb[160]; ssize_t r = read(m, mb, sizeof mb - 1);
				if (r > 0) { mb[r] = 0; for (ssize_t k = 0; k < r; k++) if (mb[k] == '\n') { mb[k] = 0; break; } dg_put(fd, "program mapping: "); dg_put(fd, mb); dg_put(fd, "\n"); }
				close(m);
			}
			unsigned long long pc = 0, lr = 0, fp = 0;                  /* registers at the fault: where it stopped and who called */
#if defined(__aarch64__)
			if (uc) { ucontext_t *u = uc; pc = u->uc_mcontext.pc; lr = u->uc_mcontext.regs[30]; fp = u->uc_mcontext.regs[29]; }
#elif defined(__arm__)
			if (uc) { ucontext_t *u = uc; pc = u->uc_mcontext.arm_pc; lr = u->uc_mcontext.arm_lr; }
#elif defined(__x86_64__)
			if (uc) { ucontext_t *u = uc; pc = (unsigned long long)u->uc_mcontext.gregs[REG_RIP]; fp = (unsigned long long)u->uc_mcontext.gregs[REG_RBP]; }
#endif
			dg_put(fd, "pc: "); dg_hex(fd, pc);
			dg_put(fd, "\nlr: "); dg_hex(fd, lr);
			dg_put(fd, "\ncallers (frame pointer chain, addresses):\n");
			for (int i = 0; i < 16 && fp && !(fp & 7) && fp > 4096; i++) {   /* [fp] = previous fp, [fp+8] = return address */
				unsigned long long *fr = (unsigned long long *)(uintptr_t)fp;
				unsigned long long next = fr[0], ret = fr[1];
				dg_put(fd, "  "); dg_hex(fd, ret); dg_put(fd, "\n");
				if (next <= fp || next - fp > 1048576) break;
				fp = next;
			}
			dg_put(fd, "\n--- last log lines ---\n");
			unsigned total = dg_n, first = total > DG_LINES ? total - DG_LINES : 0;
			for (unsigned k = first; k < total; k++) { dg_put(fd, dg_ring[k % DG_LINES]); dg_put(fd, "\n"); }
			close(fd);
		}
	}
	if (dg_marker[0]) unlink(dg_marker);                         /* reported: not also an "unclean exit" */
	raise(sig);
}

static void dg_clean(void) { if (dg_marker[0]) unlink(dg_marker); }

static void dg_install(const char *reports_dir, const char *marker_path) {
	dg_t0 = time(NULL);
	snprintf(dg_dir, sizeof dg_dir, "%s", reports_dir);
	snprintf(dg_marker, sizeof dg_marker, "%s", marker_path);
	stack_t ss; ss.ss_sp = dg_altstack; ss.ss_size = sizeof dg_altstack; ss.ss_flags = 0;
	sigaltstack(&ss, NULL);                                      /* a stack overflow can still be reported */
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = dg_on_crash;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	static const int sigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE };
	for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
}
