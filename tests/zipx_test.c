/* Verifies zipx.h against real zip files: parses the central directory, inflates
 * every member and checks the CRC. Prints one line per failure and a summary. */
#include "../native/zipx.h"
#include <sys/stat.h>
int main(int argc, char **argv) {
	if (argc < 2) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror("open"); return 2; }
	fseek(f, 0, SEEK_END);
	long total = ftell(f);
	uint8_t *all = malloc((size_t)total);
	fseek(f, 0, SEEK_SET);
	if (fread(all, 1, (size_t)total, f) != (size_t)total) return 2;
	fclose(f);
	uint32_t cdoff, cdsize, count;
	if (zx_find_eocd(all, (size_t)total, &cdoff, &cdsize, &count)) { puts("FAIL eocd"); return 1; }
	ZxDir d;
	if (zx_parse_cd(all + cdoff, cdsize, count, &d)) { puts("FAIL central directory"); return 1; }
	int bad = 0, dirs = 0, safe = 0;
	size_t bytes = 0;
	for (int i = 0; i < d.n; i++) {
		ZxEnt *e = &d.e[i];
		size_t nl = strlen(e->name);
		if (nl && e->name[nl - 1] == '/') { dirs++; continue; }
		if (zx_safe_name(e->name)) safe++;
		const uint8_t *lh = all + e->off;
		if (zx_u32(lh) != 0x04034b50u) { printf("FAIL local header %s\n", e->name); bad++; continue; }
		size_t data = e->off + 30 + zx_u16(lh + 26) + zx_u16(lh + 28);
		uint8_t *out = malloc(e->usize ? e->usize : 1);
		int r = e->method == 0 ? (memcpy(out, all + data, e->usize), 0) : zx_inflate(all + data, e->csize, out, e->usize);
		if (r || zx_crc32(0, out, e->usize) != e->crc) { printf("FAIL %s (rc=%d)\n", e->name, r); bad++; }
		bytes += e->usize;
		free(out);
	}
	printf("%s: %d entries (%d dirs), %d safe names, %zu bytes inflated, %d failures\n", argv[1], d.n, dirs, safe, bytes, bad);
	return bad ? 1 : 0;
}
