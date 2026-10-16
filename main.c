/* Saved Places GeoJSON -> KMZ. Build: cc -std=c11 -O2 -Wall -Wextra -Wpedantic
 * main.c -o takeout2kmz
 * Uses only the ISO C11 standard library; ZIP method 0 (stored), no zlib.
 */
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CHAR_BIT != 8
#error "The JSON and ZIP encodings require 8-bit bytes."
#endif

typedef struct { char *p; size_t n, cap; } Buffer;

static void die(const char *message)
{
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

static void append(Buffer *b, const void *p, size_t n)
{
    size_t need, cap;
    char *q;
    if (n > SIZE_MAX - b->n - 1) die("data too large");
    need = b->n + n + 1;
    if (need > b->cap) {
        cap = b->cap ? b->cap : 256;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        q = realloc(b->p, cap);
        if (!q) die("out of memory");
        b->p = q;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, p, n);
    b->n += n;
    b->p[b->n] = '\0';
}

static void put(Buffer *b, const char *s) { append(b, s, strlen(s)); }
static void byte(Buffer *b, unsigned char c) { append(b, &c, 1); }

enum { OBJECT, ARRAY, STRING, NUMBER, BOOLEAN, NIL };
typedef struct {
    int kind;
    size_t start, len, next, count;
    Buffer text;
} Token;
typedef struct {
    const unsigned char *s;
    size_t n, pos, used, cap;
    Token *t;
} Json;

static void json_error(const Json *j, const char *message)
{
    fprintf(stderr, "Error: JSON at byte %zu: %s\n", j->pos + 1, message);
    exit(EXIT_FAILURE);
}

static int digit(unsigned char c) { return c >= '0' && c <= '9'; }
static void whitespace(Json *j)
{
    while (j->pos < j->n && (j->s[j->pos] == ' ' || j->s[j->pos] == '\n' ||
           j->s[j->pos] == '\r' || j->s[j->pos] == '\t')) ++j->pos;
}

static size_t new_token(Json *j, int kind)
{
    size_t i = j->used, cap;
    Token *t;
    if (i == j->cap) {
        if (j->cap > SIZE_MAX / 2 / sizeof(*t)) die("too many JSON tokens");
        cap = j->cap ? j->cap * 2 : 256;
        t = realloc(j->t, cap * sizeof(*t));
        if (!t) die("out of memory");
        j->t = t;
        j->cap = cap;
    }
    memset(&j->t[i], 0, sizeof(j->t[i]));
    j->t[i].kind = kind;
    j->t[i].start = j->pos;
    ++j->used;
    return i;
}

/* Decode exactly one valid UTF-8 scalar, rejecting overlong encodings. */
static unsigned long utf8(Json *j)
{
    unsigned long cp, min;
    unsigned char c;
    unsigned int extra, k;
    c = j->s[j->pos++];
    if (c < 0x80) return c;
    if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; min = 0x10000; }
    else { json_error(j, "invalid UTF-8"); return 0; }
    for (k = 0; k < extra; ++k) {
        if (j->pos == j->n || (j->s[j->pos] & 0xc0) != 0x80)
            json_error(j, "invalid UTF-8 continuation");
        cp = (cp << 6) | (j->s[j->pos++] & 63);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        json_error(j, "invalid Unicode scalar");
    return cp;
}

static void encode(Buffer *b, unsigned long cp)
{
    if (cp < 0x80) byte(b, (unsigned char)cp);
    else if (cp < 0x800) {
        byte(b, (unsigned char)(0xc0 | (cp >> 6)));
        byte(b, (unsigned char)(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
        byte(b, (unsigned char)(0xe0 | (cp >> 12)));
        byte(b, (unsigned char)(0x80 | ((cp >> 6) & 63)));
        byte(b, (unsigned char)(0x80 | (cp & 63)));
    } else {
        byte(b, (unsigned char)(0xf0 | (cp >> 18)));
        byte(b, (unsigned char)(0x80 | ((cp >> 12) & 63)));
        byte(b, (unsigned char)(0x80 | ((cp >> 6) & 63)));
        byte(b, (unsigned char)(0x80 | (cp & 63)));
    }
}

static unsigned long hex4(Json *j)
{
    unsigned int k, v;
    unsigned long cp = 0;
    unsigned char c;
    for (k = 0; k < 4; ++k) {
        if (j->pos == j->n) json_error(j, "incomplete Unicode escape");
        c = j->s[j->pos++];
        if (digit(c)) v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else { json_error(j, "invalid Unicode escape"); return 0; }
        cp = cp * 16 + v;
    }
    return cp;
}

static size_t string(Json *j)
{
    size_t i = new_token(j, STRING);
    unsigned long cp, low;
    unsigned char c;
    Buffer b = {0};
    ++j->pos; /* opening quote */
    while (j->pos < j->n && j->s[j->pos] != '"') {
        if (j->s[j->pos] < 0x20) json_error(j, "unescaped control in string");
        if (j->s[j->pos] != '\\') { encode(&b, utf8(j)); continue; }
        if (++j->pos == j->n) json_error(j, "incomplete escape");
        c = j->s[j->pos++];
        switch (c) {
        case '"': case '\\': case '/': byte(&b, c); break;
        case 'b': byte(&b, '\b'); break;
        case 'f': byte(&b, '\f'); break;
        case 'n': byte(&b, '\n'); break;
        case 'r': byte(&b, '\r'); break;
        case 't': byte(&b, '\t'); break;
        case 'u':
            cp = hex4(j);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (j->n - j->pos < 2 || j->s[j->pos] != '\\' || j->s[j->pos+1] != 'u')
                    json_error(j, "missing low surrogate");
                j->pos += 2;
                low = hex4(j);
                if (low < 0xdc00 || low > 0xdfff) json_error(j, "invalid low surrogate");
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) json_error(j, "unpaired surrogate");
            encode(&b, cp);
            break;
        default: json_error(j, "unknown escape");
        }
    }
    if (j->pos == j->n) json_error(j, "unterminated string");
    ++j->pos;
    j->t[i].text = b;
    j->t[i].next = j->used;
    return i;
}

/* Also used for the string-valued latitude/longitude fields in Takeout. */
static int number_end(const unsigned char *s, size_t n, size_t *end)
{
    size_t p = 0;
    if (p < n && s[p] == '-') ++p;
    if (p == n) return 0;
    if (s[p] == '0') ++p;
    else {
        if (s[p] < '1' || s[p] > '9') return 0;
        while (p < n && digit(s[p])) ++p;
    }
    if (p < n && s[p] == '.') {
        ++p;
        if (p == n || !digit(s[p])) return 0;
        while (p < n && digit(s[p])) ++p;
    }
    if (p < n && (s[p] == 'e' || s[p] == 'E')) {
        ++p;
        if (p < n && (s[p] == '+' || s[p] == '-')) ++p;
        if (p == n || !digit(s[p])) return 0;
        while (p < n && digit(s[p])) ++p;
    }
    *end = p;
    return 1;
}

static size_t value(Json *j, unsigned int depth)
{
    size_t i, len;
    unsigned char c, close;
    const char *literal;
    if (depth > 128) json_error(j, "nesting exceeds 128 levels");
    whitespace(j);
    if (j->pos == j->n) json_error(j, "expected a value");
    c = j->s[j->pos];
    if (c == '"') return string(j);
    if (c == '{' || c == '[') {
        i = new_token(j, c == '{' ? OBJECT : ARRAY);
        close = c == '{' ? '}' : ']';
        ++j->pos;
        whitespace(j);
        if (j->pos < j->n && j->s[j->pos] == close) ++j->pos;
        else for (;;) {
            if (c == '{') {
                if (j->pos == j->n || j->s[j->pos] != '"') json_error(j, "expected object key");
                (void)string(j);
                whitespace(j);
                if (j->pos == j->n || j->s[j->pos++] != ':') json_error(j, "expected colon");
            }
            (void)value(j, depth + 1);
            ++j->t[i].count;
            whitespace(j);
            if (j->pos == j->n) json_error(j, "unterminated container");
            if (j->s[j->pos] == close) { ++j->pos; break; }
            if (j->s[j->pos++] != ',') json_error(j, "expected comma");
            whitespace(j);
        }
    } else if (c == '-' || digit(c)) {
        i = new_token(j, NUMBER);
        if (!number_end(j->s + j->pos, j->n - j->pos, &len)) json_error(j, "invalid number");
        j->t[i].len = len;
        j->pos += len;
    } else {
        if (c == 't') literal = "true";
        else if (c == 'f') literal = "false";
        else if (c == 'n') literal = "null";
        else { json_error(j, "unexpected character"); return 0; }
        len = strlen(literal);
        if (len > j->n - j->pos || memcmp(j->s + j->pos, literal, len)) json_error(j, "invalid literal");
        i = new_token(j, c == 'n' ? NIL : BOOLEAN);
        j->pos += len;
    }
    j->t[i].next = j->used;
    return i;
}

static int equals(const Token *t, const char *s)
{
    return t && t->kind == STRING && t->text.n == strlen(s) &&
           (!t->text.n || !memcmp(t->text.p, s, t->text.n));
}

/* Tokens are a flat preorder tree; next skips a complete subtree. */
static const Token *field(const Json *j, const Token *obj, const char *name)
{
    size_t k;
    const Token *found = NULL;
    if (!obj || obj->kind != OBJECT) return NULL;
    for (k = (size_t)(obj - j->t) + 1; k < obj->next; k = j->t[k+1].next) {
        if (equals(&j->t[k], name)) {
            if (found) die("duplicate relevant JSON key");
            found = &j->t[k+1];
        }
    }
    return found;
}

static const Token *text_field(const Json *j, const Token *obj, const char *name)
{
    const Token *t = field(j, obj, name);
    return t && t->kind == STRING && t->text.n ? t : NULL;
}

static int coordinate(const Json *j, const Token *t, int allow_string, double *v)
{
    const char *s;
    size_t n, end;
    char *tail;
    Buffer b = {0};
    if (!t) return 0;
    if (t->kind == NUMBER) { s = (const char *)j->s + t->start; n = t->len; }
    else if (allow_string && t->kind == STRING) { s = t->text.p; n = t->text.n; }
    else return 0;
    if (!number_end((const unsigned char *)s, n, &end) || end != n) return 0;
    append(&b, s, n);
    *v = strtod(b.p, &tail);
    /* Underflow (a value rounding to zero) is safe for geographic coordinates. */
    end = (size_t)(tail - b.p);
    free(b.p);
    return end == n && isfinite(*v);
}

static int in_range(double lon, double lat)
{
    return lon >= -180 && lon <= 180 && lat >= -90 && lat <= 90;
}

static int position(const Json *j, const Token *f, double *lon, double *lat)
{
    const Token *g = field(j, f, "geometry"), *a, *p;
    const Token *location = field(j, field(j, f, "properties"), "Location");
    size_t k;
    if (g && g->kind != NIL) {
        if (!equals(field(j, g, "type"), "Point")) return 0;
        a = field(j, g, "coordinates");
        if (a && a->kind == ARRAY && (a->count == 2 || a->count == 3)) {
            k = (size_t)(a - j->t) + 1;
            if (coordinate(j, &j->t[k], 0, lon) &&
                coordinate(j, &j->t[j->t[k].next], 0, lat) && in_range(*lon, *lat)) return 1;
        }
    }
    p = field(j, location, "Geo Coordinates");
    return coordinate(j, field(j, p, "Longitude"), 1, lon) &&
           coordinate(j, field(j, p, "Latitude"), 1, lat) && in_range(*lon, *lat);
}

/* XML 1.0 cannot represent certain JSON controls. Replace them with U+FFFD.
 * Escape twice for plain user text embedded in a KML HTML description. */
static void xml(Buffer *b, const char *s, size_t n, int html)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '&') put(b, html ? "&amp;amp;" : "&amp;");
        else if (c == '<') put(b, html ? "&amp;lt;" : "&lt;");
        else if (c == '>') put(b, html ? "&amp;gt;" : "&gt;");
        else if (c == '"') put(b, html ? "&amp;quot;" : "&quot;");
        else if (c == '\'') put(b, html ? "&amp;#39;" : "&apos;");
        else if ((c < 32 && c != '\t' && c != '\n' && c != '\r') ||
                 (c == 0xef && n - i >= 3 && (unsigned char)s[i+1] == 0xbf &&
                  ((unsigned char)s[i+2] == 0xbe || (unsigned char)s[i+2] == 0xbf))) {
            put(b, "\xef\xbf\xbd");
            if (c == 0xef) i += 2;
        } else if (c == '\r') put(b, "&#13;");
        else byte(b, c);
    }
}

typedef struct { const char *name; const Token *value; } Metadata;

static void placemark(Buffer *b, const Json *j, const Token *f, size_t index, double lon, double lat)
{
    const Token *p = field(j, f, "properties"), *l = field(j, p, "Location");
    const Token *name = text_field(j, p, "Title");
    Metadata m[] = {
        {"Address", text_field(j, l, "Address")},
        {"Business Name", text_field(j, l, "Business Name")},
        {"Country Code", text_field(j, l, "Country Code")},
        {"Google Maps URL", text_field(j, p, "Google Maps URL")},
        {"Note", text_field(j, p, "Note")},
        {"Comment", text_field(j, p, "Comment")},
        {"Description", text_field(j, p, "Description")},
        {"Published", text_field(j, p, "Published")},
        {"Updated", text_field(j, p, "Updated")}
    };
    size_t k;
    char number[128];
    if (!name) name = text_field(j, l, "Business Name");
    if (!name) name = text_field(j, l, "Address");
    put(b, "  <Placemark>\n    <name>");
    if (name) xml(b, name->text.p, name->text.n, 0);
    else {
        snprintf(number, sizeof(number), "Saved place %zu", index);
        put(b, number);
    }
    put(b, "</name>\n    <description>");
    for (k = 0; k < sizeof(m)/sizeof(m[0]); ++k) if (m[k].value) {
        put(b, m[k].name);
        put(b, ": ");
        xml(b, m[k].value->text.p, m[k].value->text.n, 1);
        put(b, "&lt;br/&gt;");
    }
    put(b, "</description>\n    <ExtendedData>\n");
    for (k = 0; k < sizeof(m)/sizeof(m[0]); ++k) if (m[k].value) {
        put(b, "      <Data name=\"");
        put(b, m[k].name);
        put(b, "\"><value>");
        xml(b, m[k].value->text.p, m[k].value->text.n, 0);
        put(b, "</value></Data>\n");
    }
    put(b, "    </ExtendedData>\n    <Point><coordinates>");
    /* C starts in the "C" locale; do not call setlocale(). */
    snprintf(number, sizeof(number), "%.17g,%.17g,0", lon, lat);
    put(b, number);
    put(b, "</coordinates></Point>\n  </Placemark>\n");
}

static uint32_t crc32(const unsigned char *p, size_t n)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t k;
    unsigned int bit;
    for (k = 0; k < n; ++k) {
        crc ^= p[k];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return crc ^ UINT32_C(0xffffffff);
}

static void le16(Buffer *b, uint32_t v)
{
    byte(b, (unsigned char)v);
    byte(b, (unsigned char)(v >> 8));
}
static void le32(Buffer *b, uint32_t v)
{
    le16(b, v & 0xffff);
    le16(b, v >> 16);
}

/* A single ZIP entry, doc.kml, using stored data and a central directory.
 * Fields are serialized explicitly, never by writing native C structs. */
static int write_kmz(FILE *out, const Buffer *kml)
{
    Buffer h = {0};
    uint32_t size = (uint32_t)kml->n;
    uint32_t crc = crc32((const unsigned char *)kml->p, kml->n);
    int ok;
    le32(&h, UINT32_C(0x04034b50));
    le16(&h, 20); le16(&h, 0); le16(&h, 0); /* ZIP 2.0, flags, stored */
    le16(&h, 0); le16(&h, 33); /* 1980-01-01, midnight */
    le32(&h, crc); le32(&h, size); le32(&h, size);
    le16(&h, 7); le16(&h, 0); put(&h, "doc.kml");
    ok = fwrite(h.p, 1, h.n, out) == h.n && fwrite(kml->p, 1, kml->n, out) == kml->n;
    h.n = 0;
    le32(&h, UINT32_C(0x02014b50));
    le16(&h, 20); le16(&h, 20); le16(&h, 0); le16(&h, 0);
    le16(&h, 0); le16(&h, 33);
    le32(&h, crc); le32(&h, size); le32(&h, size);
    le16(&h, 7); le16(&h, 0); le16(&h, 0); le16(&h, 0); le16(&h, 0);
    le32(&h, 0); le32(&h, 0); put(&h, "doc.kml");
    le32(&h, UINT32_C(0x06054b50));
    le16(&h, 0); le16(&h, 0); le16(&h, 1); le16(&h, 1);
    le32(&h, 53); le32(&h, size + 37); le16(&h, 0);
    if (ok) ok = fwrite(h.p, 1, h.n, out) == h.n;
    free(h.p);
    return ok;
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    Buffer input = {0}, kml = {0};
    unsigned char block[8192];
    Json j = {0};
    const Token *features, *f;
    size_t n, i, k, total = 0, imported = 0, skipped = 0;
    double lon, lat;
    int ok;
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        printf("Usage: %s \"Saved Places.json\" places.kmz\n", argv[0]);
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "Usage: %s \"Saved Places.json\" places.kmz\n", argv[0]);
        return 1;
    }
    in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    while ((n = fread(block, 1, sizeof(block), in)) != 0) append(&input, block, n);
    ok = !ferror(in);
    if (fclose(in) != 0) ok = 0;
    if (!ok) die("could not read input");
    j.s = (const unsigned char *)input.p;
    j.n = input.n;
    if (j.n >= 3 && !memcmp(j.s, "\xef\xbb\xbf", 3)) j.pos = 3;
    (void)value(&j, 0);
    whitespace(&j);
    if (j.pos != j.n) json_error(&j, "trailing data");
    if (!equals(field(&j, &j.t[0], "type"), "FeatureCollection")) die("expected a GeoJSON FeatureCollection");
    features = field(&j, &j.t[0], "features");
    if (!features || features->kind != ARRAY) die("expected a features array");
    put(&kml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
              "<Document>\n  <name>Saved Places</name>\n");
    for (k = (size_t)(features - j.t) + 1; k < features->next; k = j.t[k].next) {
        f = &j.t[k];
        ++total;
        if (!equals(field(&j, f, "type"), "Feature") || !position(&j, f, &lon, &lat)) {
            fprintf(stderr, "Warning: feature %zu skipped (not a Feature with usable point coordinates).\n", total);
            ++skipped;
            continue;
        }
        placemark(&kml, &j, f, total, lon, lat);
        ++imported;
    }
    if (total && !imported) die("no usable point coordinates; no output created");
    put(&kml, "</Document>\n</kml>\n");
    if ((uintmax_t)kml.n > (uintmax_t)UINT32_MAX - 112) die("KMZ exceeds the classic ZIP size limit");
    /* ISO C11 exclusive creation protects existing files, including the input. */
    out = fopen(argv[2], "wbx");
    if (!out) { perror(argv[2]); return 1; }
    ok = write_kmz(out, &kml);
    if (fclose(out) != 0) ok = 0;
    if (!ok) {
        if (remove(argv[2]) != 0) fprintf(stderr, "Warning: could not remove incomplete output: %s\n", argv[2]);
        die("could not write complete KMZ");
    }
    fprintf(stderr, "Wrote %s: %zu imported, %zu skipped.\n", argv[2], imported, skipped);
    for (i = 0; i < j.used; ++i) free(j.t[i].text.p);
    free(j.t);
    free(input.p);
    free(kml.p);
    return skipped ? 2 : 0;
}
