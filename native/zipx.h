/* zipx.h: CRC32, DEFLATE (RFC 1951) and zip central-directory parsing.
 * Header-only, no dependencies, so the handheld needs no unzip or zlib.
 * Inflate is a compact canonical-Huffman decoder in the style of zlib's puff. */
#ifndef ZIPX_H
#define ZIPX_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------- CRC32 */
static uint32_t zx_crc_table[256];
static int zx_crc_ready;

static void zx_crc_init(void) {
	if (zx_crc_ready) return;
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		zx_crc_table[i] = c;
	}
	zx_crc_ready = 1;
}

static uint32_t zx_crc32(uint32_t crc, const uint8_t *p, size_t n) {
	zx_crc_init();
	crc = ~crc;
	while (n--) crc = zx_crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
	return ~crc;
}

/* CRC32 of a whole file; returns 0 and sets *ok=0 if it cannot be read. */
static uint32_t zx_crc32_file(const char *path, int *ok) {
	FILE *f = fopen(path, "rb");
	uint8_t buf[65536];
	uint32_t crc = 0;
	size_t n;
	if (ok) *ok = 0;
	if (!f) return 0;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) crc = zx_crc32(crc, buf, n);
	if (ferror(f)) { fclose(f); return 0; }
	fclose(f);
	if (ok) *ok = 1;
	return crc;
}

/* --------------------------------------------------------------- inflate */
#define ZX_MAXBITS 15
typedef struct { uint16_t count[ZX_MAXBITS + 1]; uint16_t symbol[288]; } ZxHuff;
typedef struct {
	const uint8_t *in; size_t inlen, inpos;
	uint32_t bitbuf; int bitcnt;
	uint8_t *out; size_t outlen, outpos;
	int err;
} ZxInf;

static uint32_t zx_bits(ZxInf *s, int need) {
	uint32_t val = s->bitbuf;
	while (s->bitcnt < need) {
		if (s->inpos >= s->inlen) { s->err = 1; return 0; }
		val |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = val >> need;
	s->bitcnt -= need;
	return val & ((1u << need) - 1);
}

static int zx_decode(ZxInf *s, const ZxHuff *h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= ZX_MAXBITS; len++) {
		code |= (int)zx_bits(s, 1);
		if (s->err) return -1;
		int count = h->count[len];
		if (code - count < first) return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static int zx_construct(ZxHuff *h, const uint16_t *length, int n) {
	uint16_t offs[ZX_MAXBITS + 1];
	int left = 1;
	memset(h->count, 0, sizeof h->count);
	for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
	if (h->count[0] == n) return 0;
	for (int len = 1; len <= ZX_MAXBITS; len++) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0) return left;
	}
	offs[1] = 0;
	for (int len = 1; len < ZX_MAXBITS; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
	for (int sym = 0; sym < n; sym++)
		if (length[sym]) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
	return left;
}

static int zx_codes(ZxInf *s, const ZxHuff *lencode, const ZxHuff *distcode) {
	static const uint16_t lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static const uint16_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	static const uint16_t dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
	static const uint16_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
	int symbol;
	do {
		symbol = zx_decode(s, lencode);
		if (symbol < 0) return -1;
		if (symbol < 256) {
			if (s->outpos >= s->outlen) return -2;
			s->out[s->outpos++] = (uint8_t)symbol;
		} else if (symbol > 256) {
			symbol -= 257;
			if (symbol >= 29) return -3;
			int len = lens[symbol] + (int)zx_bits(s, lext[symbol]);
			symbol = zx_decode(s, distcode);
			if (symbol < 0 || symbol >= 30) return -4;
			size_t dist = dists[symbol] + zx_bits(s, dext[symbol]);
			if (s->err) return -1;
			if (dist > s->outpos) return -5;
			if (s->outpos + (size_t)len > s->outlen) return -2;
			for (int i = 0; i < len; i++, s->outpos++) s->out[s->outpos] = s->out[s->outpos - dist];
		}
	} while (symbol != 256);
	return 0;
}

static int zx_stored(ZxInf *s) {
	s->bitbuf = 0;
	s->bitcnt = 0;
	if (s->inpos + 4 > s->inlen) return -1;
	unsigned len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
	unsigned nlen = s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8);
	s->inpos += 4;
	if (len != (~nlen & 0xFFFF)) return -6;
	if (s->inpos + len > s->inlen) return -1;
	if (s->outpos + len > s->outlen) return -2;
	memcpy(s->out + s->outpos, s->in + s->inpos, len);
	s->inpos += len;
	s->outpos += len;
	return 0;
}

static int zx_fixed(ZxInf *s) {
	static ZxHuff lencode, distcode;
	static int built;
	if (!built) {
		uint16_t lengths[288];
		int sym;
		for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
		for (; sym < 256; sym++) lengths[sym] = 9;
		for (; sym < 280; sym++) lengths[sym] = 7;
		for (; sym < 288; sym++) lengths[sym] = 8;
		zx_construct(&lencode, lengths, 288);
		for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
		zx_construct(&distcode, lengths, 30);
		built = 1;
	}
	return zx_codes(s, &lencode, &distcode);
}

static int zx_dynamic(ZxInf *s) {
	static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	uint16_t lengths[320];
	ZxHuff lencode, distcode;
	int nlen = (int)zx_bits(s, 5) + 257, ndist = (int)zx_bits(s, 5) + 1, ncode = (int)zx_bits(s, 4) + 4, index;
	if (s->err || nlen > 286 || ndist > 30) return -1;
	for (index = 0; index < ncode; index++) lengths[order[index]] = (uint16_t)zx_bits(s, 3);
	for (; index < 19; index++) lengths[order[index]] = 0;
	if (zx_construct(&lencode, lengths, 19) != 0) return -7;
	index = 0;
	while (index < nlen + ndist) {
		int symbol = zx_decode(s, &lencode);
		if (symbol < 0) return -1;
		if (symbol < 16) lengths[index++] = (uint16_t)symbol;
		else {
			int len = 0;
			if (symbol == 16) {
				if (index == 0) return -5;
				len = lengths[index - 1];
				symbol = 3 + (int)zx_bits(s, 2);
			} else if (symbol == 17) symbol = 3 + (int)zx_bits(s, 3);
			else symbol = 11 + (int)zx_bits(s, 7);
			if (s->err || index + symbol > nlen + ndist) return -6;
			while (symbol--) lengths[index++] = (uint16_t)len;
		}
	}
	if (lengths[256] == 0) return -9;
	int err = zx_construct(&lencode, lengths, nlen);
	if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -7;
	err = zx_construct(&distcode, lengths + nlen, ndist);
	if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -8;
	return zx_codes(s, &lencode, &distcode);
}

/* Inflate raw DEFLATE data; out must hold exactly outlen bytes. 0 = success. */
static int zx_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen) {
	ZxInf s;
	int last, err;
	memset(&s, 0, sizeof s);
	s.in = in; s.inlen = inlen; s.out = out; s.outlen = outlen;
	do {
		last = (int)zx_bits(&s, 1);
		int type = (int)zx_bits(&s, 2);
		if (s.err) return -1;
		err = type == 0 ? zx_stored(&s) : type == 1 ? zx_fixed(&s) : type == 2 ? zx_dynamic(&s) : -10;
		if (err) return err;
	} while (!last);
	return s.outpos == outlen ? 0 : -11;
}

/* ------------------------------------------------------------------- zip */
typedef struct {
	char *name;
	uint32_t crc, csize, usize, off;
	uint16_t method;
	uint32_t mode;   /* unix permission bits, 0 if unknown */
} ZxEnt;
typedef struct { ZxEnt *e; int n; } ZxDir;

static uint32_t zx_u16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t zx_u32(const uint8_t *p) { return zx_u16(p) | (zx_u16(p + 2) << 16); }

/* Find the end-of-central-directory record in a buffer that ends the file. 0 = ok. */
static int zx_find_eocd(const uint8_t *buf, size_t n, uint32_t *cd_off, uint32_t *cd_size, uint32_t *count) {
	if (n < 22) return -1;
	for (size_t i = n - 22 + 1; i-- > 0;) {
		if (zx_u32(buf + i) == 0x06054b50u) {
			*count = zx_u16(buf + i + 10);
			*cd_size = zx_u32(buf + i + 12);
			*cd_off = zx_u32(buf + i + 16);
			if (*count == 0xFFFF || *cd_size == 0xFFFFFFFFu || *cd_off == 0xFFFFFFFFu) return -2; /* zip64: unsupported */
			return 0;
		}
		if (n - i > 22 + 65535) break;
	}
	return -1;
}

static void zx_free(ZxDir *d) {
	for (int i = 0; i < d->n; i++) free(d->e[i].name);
	free(d->e);
	d->e = NULL;
	d->n = 0;
}

/* Parse a central directory of `count` entries. 0 = ok. */
static int zx_parse_cd(const uint8_t *cd, size_t n, uint32_t count, ZxDir *d) {
	size_t pos = 0;
	d->e = calloc(count ? count : 1, sizeof *d->e);
	d->n = 0;
	if (!d->e) return -1;
	for (uint32_t i = 0; i < count; i++) {
		if (pos + 46 > n || zx_u32(cd + pos) != 0x02014b50u) { zx_free(d); return -1; }
		const uint8_t *p = cd + pos;
		size_t nlen = zx_u16(p + 28), elen = zx_u16(p + 30), clen = zx_u16(p + 32);
		if (pos + 46 + nlen + elen + clen > n) { zx_free(d); return -1; }
		ZxEnt *e = &d->e[d->n];
		e->method = (uint16_t)zx_u16(p + 10);
		e->crc = zx_u32(p + 16);
		e->csize = zx_u32(p + 20);
		e->usize = zx_u32(p + 24);
		e->off = zx_u32(p + 42);
		if ((zx_u16(p + 4) >> 8) == 3) e->mode = (zx_u32(p + 38) >> 16) & 07777; /* made on unix */
		e->name = malloc(nlen + 1);
		if (!e->name) { zx_free(d); return -1; }
		memcpy(e->name, p + 46, nlen);
		e->name[nlen] = 0;
		d->n++;
		pos += 46 + nlen + elen + clen;
	}
	return 0;
}

/* Entry names that are safe to join onto a destination folder. */
static int zx_safe_name(const char *s) {
	size_t n = strlen(s);
	if (!n || n > 280 || s[0] == '/' || strchr(s, '\\')) return 0;
	for (const char *p = s; *p; p++) if ((unsigned char)*p < 32) return 0;
	const char *seg = s;
	for (const char *p = s;; p++) {
		if (*p == '/' || *p == 0) {
			size_t l = (size_t)(p - seg);
			if (l == 2 && seg[0] == '.' && seg[1] == '.') return 0;
			if (!*p) break;
			seg = p + 1;
		}
	}
	return 1;
}
#endif
