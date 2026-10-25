/* SPDX-License-Identifier: GPL-3.0-only */
/* Home's six later landscapes (the owner's, 2026-10-25): 6 FOREST (old trees at dusk, the light through them in
 * shafts, mist on the floor), 7 LIVING (a couch before a CRT, a VHS deck, two old speakers, the blinds), 8 VILLA (a
 * pool on a white terrace over the Mediterranean, cypresses, a stone pine), 9 CALIFORNIA (a street of palms running to
 * the sun), 10 HARBOUR (boats in fog, a lamp on the pier), 11 LIGHTHOUSE (on a cliff at night, its beam turning).
 * Each is three pictures: the band (Home's card, SC_W x SC_H) and, in full screen, what is above it and below it
 * (DMK_W x LF_H); all drawn a pixel a byte in the light's scratch (sc_fg), 0xFF where the sky shows. Built only when
 * the scene changes: compiled for size (DMK_SMALL), the sound let in every 2048 pixels as the first six are */

static int cv_w = SC_W, cv_h = SC_H;                    /* the picture being drawn: the band, or above or below it */
#ifdef __GNUC__
__attribute__((noinline))
#endif
static void cv_put(int x, int y, int c)
{
    if (++fg_n >= 2048u) { fg_n = 0; ui_yield(); }
    if (x >= 0 && x < cv_w && y >= 0 && y < cv_h) sc_fg[y * cv_w + x] = (uint8_t)c;
}
static int cv_get(int x, int y) { return x >= 0 && x < cv_w && y >= 0 && y < cv_h ? sc_fg[y * cv_w + x] : 0xFF; }
DMK_SMALL static void cv_rect(int x, int y, int w, int h, int c) { int i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) cv_put(x + i, y + j, c); }
DMK_SMALL static void cv_disc(int cx, int cy, int r, int c)
{
    int i, j;
    for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) if (i * i + j * j <= r * r + r / 2) cv_put(cx + i, cy + j, c);
}
DMK_SMALL static void cv_line(int x0, int y0, int x1, int y1, int c)   /* a line, a pixel a step */
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        int e2 = 2 * e;                                 /* (taken once: both steps read the same) */
        cv_put(x0, y0, c);
        if (x0 == x1 && y0 == y1) return;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
DMK_SMALL static void cv_dith(int x, int y, int w, int h, int n, int c)   /* a box, n sixteenths of its pixels in c */
{
    int i, j;
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) if (bay(i, j) < n) cv_put(i, j, c);
}
DMK_SMALL static void cv_wire(int x0, int y0, int x1, int y1, int sag, int c)   /* a wire between two poles, sagging */
{
    int x;
    for (x = x0; x <= x1; x++) {
        int t = x - x0, w = x1 - x0 ? x1 - x0 : 1;
        cv_put(x, y0 + (y1 - y0) * t / w + sag * 4 * t * (w - t) / (w * w), c);
    }
}
/* a palm, in the synthwave picture's way (the owner's reference): a slim trunk leaning a little, and a crown of seven
 * fronds fanned up and out like a star, each a solid blade, thickest at its middle, pointed, its tip curving a little
 * down, its edge stepped */
DMK_SMALL static void cv_palm(int x, int base, int h, int lean, int c)
{
    static const int8_t FR[7][3] = {{-12, -11, 8}, {-16, -4, 8}, {-13, 6, 5}, {-3, -16, 6}, {11, -12, 8}, {16, -3, 8},
                                    {13, 7, 5}};        /* its way out (16 a pixel), its length in eighths */
    int t, k, s, j, xt = x + lean, yt = base - h, L = 6 + h / 7;
    for (t = 0; t <= h; t++) {                          /* the trunk */
        int px = x + lean * t / h, w = t < h / 5 ? 3 : 2;
        cv_rect(px - w / 2, base - t, w, 1, c);
    }
    for (k = 0; k < 7; k++) {
        int len = L * FR[k][2] / 8, mw = 1 + len / 5, dx = FR[k][0], dy = FR[k][1];
        for (s = 0; s <= len; s++) {
            int cx = xt * 16 + dx * s, cy = yt * 16 + dy * s + s * s * 16 / (len * 3);   /* (Q4) the tip bending down */
            int hw = mw * 4 * s * (len - s) / (len * len) + ((s / 2) & 1);   /* the blade's half width, its steps */
            for (j = -hw; j <= hw; j++) cv_put((cx - dy * j / 2 + 8) >> 4, (cy + dx * j / 2 + 8) >> 4, c);
            cv_put((cx + 8) >> 4, (cy + 8) >> 4, c);
        }
    }
    cv_disc(xt, yt, 2, c);
}
/* a broadleaf, old: its trunk, a fork, a crown of round masses (holes left where the sky shows through) */
DMK_SMALL static void cv_oak(int x, int base, int h, int r, int c, uint32_t seed)
{
    int k, w = 3 + r / 5;
    cv_rect(x - w / 2, base - h + r, w, h - r, c);
    cv_line(x, base - h + r + 6, x - r / 2, base - h + r / 2, c);
    cv_line(x, base - h + r + 4, x + r / 2, base - h + r / 3, c);
    for (k = 0; k < 9; k++) {
        uint32_t q = hsh(seed + (uint32_t)k * 77u);
        cv_disc(x + (int)(q % (uint32_t)(r * 2 + 1)) - r, base - h + (int)((q >> 8) % (uint32_t)(r + 1)), r / 3 + (int)((q >> 16) % (uint32_t)(r / 3 + 1)), c);
    }
}
DMK_SMALL static void cv_cypress(int x, int base, int h, int c)   /* a cypress: a dark flame, slim */
{
    int y, mw = h / 18 + 2;
    for (y = 0; y < h; y++) {
        int half = mw * y * (2 * h - y) / (h * h) + (hsh((uint32_t)(x * 7 + y)) & 1u);
        cv_rect(x - half, base - h + y, half * 2 + 1, 1, c);
    }
}
DMK_SMALL static void cv_boat(int x, int wl, int w, int c, int lt)   /* a fishing boat: its hull, its cabin, a mast */
{
    int j;
    for (j = 0; j < 6; j++) cv_rect(x + j / 2, wl - 6 + j, w - j, 1, c);
    cv_rect(x + w / 4, wl - 6, w / 2, 1, lt == c ? c : C_LINE);   /* its rail, pale in the fog */
    cv_rect(x + w / 2, wl - 12, w / 3, 6, c);
    cv_rect(x + w / 2 + 2, wl - 10, 2, 2, C_A_LABEL);
    cv_rect(x + w / 3, wl - 28, 1, 22, c);
    cv_line(x + w / 3, wl - 28, x + 1, wl - 7, c);
    cv_line(x + w / 3, wl - 28, x + w - 2, wl - 7, c);
}
/* a plant's leaves in the light, not a black mass: each a dark shadow, the leaf, its lit side, a split or two */
DMK_SMALL static void cv_leaves(int x0, int y0, int w, int h, int n, uint32_t seed)
{
    int k;
    for (k = 0; k < n; k++) {
        uint32_t q = hsh(seed + (uint32_t)k * 7u);
        int cx = x0 + (int)(q % (uint32_t)w), cy = y0 + (int)((q >> 8) % (uint32_t)h), r = 5 + (int)((q >> 16) % 4u);
        cv_disc(cx + 1, cy + 1, r, C_HOLO0);
        cv_disc(cx, cy, r, C_HOLO1);
        cv_disc(cx - 1, cy - 1, r - 2, C_HOLO2);
        cv_disc(cx - 2, cy - 2, r / 3, C_HOLO3);
        cv_line(cx, cy, cx + r - 1, cy - (int)((q >> 24) % 3u), C_HOLO0);   /* a split */
    }
}
/* fog over what is drawn: from a row's density at y0 to none at y1 (or the other way), in the fog's two greys */
DMK_SMALL static void cv_fog(int y0, int y1, int n0, int n1)
{
    int x, y;
    for (y = y0; y < y1; y++) {
        int n = n0 + (n1 - n0) * (y - y0) / (y1 - y0 > 0 ? y1 - y0 : 1);
        for (x = 0; x < cv_w; x++) if (bay(x + y / 3, y) < n) cv_put(x, y, bay(x, y) < 8 ? C_A_LABEL : C_LINE);
    }
}

/* the shapes in dk lit along their tops where the sky (or a lighter thing) is behind them: backlight, n sixteenths */
DMK_SMALL static void cv_rim(int dk, int c, int n)
{
    int x, y;
    for (y = cv_h - 1; y > 0; y--) for (x = 0; x < cv_w; x++) {
        int up = sc_fg[(y - 1) * cv_w + x];
        if (sc_fg[y * cv_w + x] == dk && up != dk && up != c && bay(x, y) < n) sc_fg[y * cv_w + x] = (uint8_t)c;
    }
}
DMK_SMALL static void cv_veil(int y0, int y1, int n0, int n1, int c)   /* a mist of c, n0 sixteenths at y0 to n1 */
{
    int x, y;
    for (y = y0; y < y1; y++) {
        int n = n0 + (n1 - n0) * (y - y0) / (y1 - y0 > 0 ? y1 - y0 : 1);
        for (x = 0; x < cv_w; x++) if (bay(x + y / 2, y) < n) cv_put(x, y, c);
    }
}
DMK_SMALL static void cv_fern(int fx, int fy, int n, int dk, int tip)   /* a fern: fronds fanned up and out */
{
    int j;
    for (j = 0; j <= n; j++) {
        int tx = fx - n + j * 2, ty = fy - n + (j - n / 2) * (j - n / 2) * 2 / (n + 1);
        cv_line(fx, fy, tx, ty, dk);
        cv_put(tx, ty, tip);
    }
}
/* the forest's rays: three, slanting down from the upper right; how strong one is at (x, y), 0 .. 6 */
static int ray_at(int x, int y)
{
    int k, best = 0;
    for (k = 0; k < 3; k++) {
        int cx = 120 + k * 46 - y * 3 / 5, half = 4 + y / 16, d = x - cx < 0 ? cx - x : x - cx, v;
        if (d < half) { v = (half - d) * 6 / half - y / 30; if (v > best) best = v; }
    }
    return best;
}

/* CALIFORNIA's nearest palms, one each side, on the sidewalks: so tall their crowns are over the band; drawn whole on
 * the band (which shows their trunks, from the ground up) and above it (their crowns), in each one's place, so they meet */
static void ca_near(int dk, int above)
{
    int ox = above ? LF_X0 : 0, oy = above ? LF_H : 0;
    cv_palm(12 + ox, 106 + oy, 146, 6, dk);             /* (on the sidewalks, past the curbs: the owner, not on the road) */
    cv_palm(SC_W - 12 + ox, 106 + oy, 152, -7, dk);
}

/* ---- the bands */
DMK_SMALL static void land_band(int sc, int dk, int lt)
{
    int x, y, k;
    cv_w = SC_W; cv_h = SC_H;
    if (sc == 6) {                                      /* FOREST */
        for (x = 0; x < SC_W; x++) {                    /* far: a treeline of round crowns, violet in the dusk */
            int top = 70 - tri(x + 7, 31, 8) - tri(x + 3, 13, 4) - tri(x, 7, 2);
            for (y = top; y < 94; y++) cv_put(x, y, y < top + 2 ? C_HOLO3 : bay(x, y) < 5 ? C_HOLO1 : C_HOLO2);
        }
        for (k = 0; k < 6; k++) {                       /* nearer trees: a trunk each, a crown into the canopy */
            int tx = 26 + k * 34 + (int)(hsh((uint32_t)k + 5u) % 10u), w = 3 + (int)(hsh((uint32_t)k + 7u) % 3u), j;
            cv_rect(tx, 20, w, 76, dk);
            for (j = 0; j < 6; j++) {
                uint32_t q = hsh((uint32_t)(k * 13 + j) + 101u);
                cv_disc(tx + w / 2 + (int)(q % 26u) - 13, 14 + (int)((q >> 8) % 22u), 6 + (int)((q >> 16) % 6u), dk);
            }
        }
        for (k = 0; k < 26; k++) {                      /* the canopy over all */
            uint32_t q = hsh((uint32_t)k * 2654435761u + 3u);
            cv_disc((int)(q % (uint32_t)SC_W), (int)((q >> 8) % 8u), 8 + (int)((q >> 16) % 7u), dk);
        }
        cv_rim(dk, C_HOLO3, 9);                         /* lit from behind */
        for (y = 8; y < 96; y++) for (x = 0; x < SC_W; x++) {   /* the rays through, on the air and the leaves */
            int v = ray_at(x, y), c = cv_get(x, y);
            if (!x && !(y & 7)) ui_yield();             /* (a row's worth of rays is long: the sound let in) */
            if (v > 0 && bay(x, y) < (c == dk ? v / 3 : v)) cv_put(x, y, y < 46 ? C_HOLO6 : C_HOLO5);
        }
        cv_veil(72, 96, 0, 8, C_HOLO4);                 /* mist on the floor, the dusk in it */
        for (y = 0; y < SC_H; y++) {                    /* the near trees, framing it, dark, their inner edges lit */
            int l = 9 + (y > 90 ? (y - 90) / 3 : 0) + tri(y, 37, 1), r = SC_W - 11 - (y > 94 ? (y - 94) / 3 : 0);
            for (x = 0; x < l; x++) cv_put(x, y, x == l - 1 && bay(x, y) < 8 ? C_HOLO2 : dk);
            for (x = r; x < SC_W; x++) cv_put(x, y, x == r && bay(x, y) < 8 ? C_HOLO2 : dk);
        }
        cv_line(8, 30, 60, 6, dk); cv_line(8, 31, 60, 7, dk); cv_line(8, 32, 40, 18, dk);
        cv_line(SC_W - 11, 26, 168, 4, dk); cv_line(SC_W - 11, 27, 168, 5, dk);
        for (y = 94; y < SC_H; y++) for (x = 9; x < SC_W - 11; x++) {   /* the floor, a path into it */
            int pc = 114 + tri(y * 2, 40, 6) - 3, half = 2 + (y - 94) * (y - 94) / 14, d = x - pc < 0 ? pc - x : x - pc;
            cv_put(x, y, d < half ? (bay(x, y) < 12 - (y - 94) / 3 ? C_HOLO3 : C_HOLO1) : d < half + 2 ? C_HOLO1 : bay(x, y) < 2 ? C_HOLO0 : dk);
        }
        for (k = 0; k < 14; k++) {                      /* ferns along it */
            uint32_t q = hsh((uint32_t)k * 40503u + 11u);
            int side = k & 1, d = (int)(q % 24u), fy = 100 + d, fx = side ? 140 + d * 2 + (int)((q >> 8) % 30u) : 88 - d * 2 - (int)((q >> 8) % 30u);
            cv_fern(fx, fy, 5 + d / 4, C_HOLO0, C_HOLO2);
        }
    } else if (sc == 7) {                               /* LIVING: the TV's light on the wall, the blinds, the set */
        for (y = 0; y < SC_H; y++) for (x = 0; x < SC_W; x++) {
            int dx = x - 96, dy = (y - 56) * 3 / 2, d = dx * dx + dy * dy;
            cv_put(x, y, d < 1800 ? (bay(x, y) * 120 < 1800 - d ? C_HOLO1 : C_HOLO0) : d < 5000 ? (bay(x, y) * 320 < 5000 - d ? C_HOLO0 : dk) : dk);
        }
        for (y = 10; y < 64; y++) for (x = 172; x < 216; x++) {   /* the window: the blinds, the city between their slats */
            int slat = (y - 10) % 4 < 2;
            if (slat) cv_put(x, y, bay(x, y) < 6 ? C_A_INK2 : C_LABEL);
            else cv_put(x, y, hsh((uint32_t)(x * 31 + y * 7)) % 23u == 0 ? C_HOLO6 : y > 44 ? C_HOLO2 : C_HOLO1);
        }
        cv_rect(170, 8, 48, 2, C_LABEL); cv_rect(170, 64, 48, 2, C_LABEL);   /* its frame, the cord */
        cv_rect(214, 10, 1, 36, C_LABEL);
        for (k = 0; k < 7; k++) { cv_line(161 + k, 97, 157 + k * 3, 65 + (k & 1) * 6, C_HOLO0); cv_line(162 + k, 97, 158 + k * 3, 67 + (k & 1) * 6, dk); }   /* a snake plant by it */
        cv_rect(158, 96, 14, 4, C_LABEL);
        for (k = 0; k < 2; k++) {                       /* the speakers: dark cabinets, a woofer, a tweeter, lit toward the TV */
            int sx = k ? 140 : 22, edge = k ? sx : sx + 25;
            cv_rect(sx, 38, 26, 62, dk);
            cv_rect(edge, 38, 1, 62, C_HOLO1);
            cv_rect(sx, 38, 26, 1, C_LABEL);
            cv_disc(sx + 13, 50, 4, C_LABEL); cv_disc(sx + 13, 50, 2, dk);
            cv_disc(sx + 13, 78, 10, C_LABEL); cv_disc(sx + 13, 78, 8, dk); cv_disc(sx + 13, 78, 3, C_LABEL);
            cv_rect(sx + 3, 93, 20, 1, C_A_INK2);
        }
        cv_rect(60, 28, 70, 56, C_LABEL);               /* the TV: its box, its screen's bezel, its knobs */
        cv_rect(60, 28, 70, 1, C_LINE); cv_rect(129, 28, 1, 56, C_A_INK2);
        cv_rect(64, 32, 50, 44, C_A_INK2);
        cv_rect(66, 34, 46, 40, dk);
        cv_disc(122, 40, 3, C_A_INK2); cv_disc(122, 50, 3, C_A_INK2); cv_rect(118, 60, 8, 12, C_A_INK2);
        for (k = 0; k < 4; k++) cv_rect(119, 62 + k * 3, 6, 1, C_LABEL);
        cv_line(84, 27, 72, 10, C_LABEL); cv_line(92, 27, 104, 12, C_LABEL);   /* its rabbit ears */
        cv_rect(48, 84, 94, 16, dk);                    /* the cabinet, the deck in it, tapes */
        cv_rect(48, 84, 94, 1, C_LABEL);
        cv_rect(54, 87, 56, 9, C_LABEL); cv_rect(58, 89, 26, 2, dk); cv_rect(88, 89, 18, 4, dk);
        for (k = 0; k < 6; k++) { cv_rect(114 + k * 4, 86 - (k & 1), 3, 12 + (k & 1), k % 3 ? C_A_INK2 : C_HOLO2); cv_rect(114 + k * 4, 89, 3, 2, lt); }
        for (y = 96; y < SC_H; y++) for (x = 0; x < SC_W; x++) {   /* the couch's back, its cushions, lit by the screen */
            int cu = (x % 72) - 36, top = 100 + cu * cu / 260 - (x < 12 || x > 214 ? 5 : 0);
            if (y >= top) cv_put(x, y, y == top ? C_HOLO1 : y < top + 3 && bay(x, y) < 10 - (y - top) * 3 ? C_HOLO0 : (x % 72 == 0 && y > top + 4) ? C_A_INK2 : dk);
        }
        for (y = 104; y < SC_H; y++) for (x = 150; x < 190; x++) if (((x + y) / 3) % 3 == 0 && bay(x, y) < 10) cv_put(x, y, C_HOLO3);   /* a throw over it */
        {                                               /* the cat on it, from behind, watching too: lit at its left */
            static const uint16_t CAT[16] = {528, 816, 1008, 2040, 2040, 1008, 480, 1008, 2040, 4092, 4092, 4093, 4093, 2041, 1010, 2042};
            for (y = 0; y < 16; y++) for (x = 0; x < 12; x++)
                if (CAT[y] >> (11 - x) & 1u) {
                    int left = !x || !(CAT[y] >> (12 - x) & 1u), top = !y || !(CAT[y - 1] >> (11 - x) & 1u);
                    cv_put(184 + x, 85 + y, left || top ? (left ? C_HOLO1 : C_HOLO0) : dk);
                }
        }
    } else if (sc == 8) {                               /* VILLA: a pool house in the Californian way (the owner's
                                                           picture): a flat white roof on pink walls and dark glass, a
                                                           pink deck, the pool before it in perspective (its ripples
                                                           move: land_anim), the sea and its hills to the right */
        for (x = 150; x < SC_W; x++) {                  /* the hills, the sea */
            int top = 46 - tri(x, 41, 5) - tri(x + 7, 13, 2);
            for (y = top; y < 52; y++) cv_put(x, y, bay(x, y) < 6 ? C_HOLO3 : C_HOLO2);
            for (y = 52; y < 58; y++) cv_put(x, y, y == 52 ? C_HOLO5 : bay(x, y) < 6 ? C_HOLO1 : C_HOLO0);
        }
        cv_palm(190, 58, 34, 2, dk);                    /* a palm by the sea */
        cv_rect(40, 25, 56, 33, C_HOLO5);               /* the house: its left wing, glass between pink */
        cv_rect(44, 29, 48, 29, dk);
        for (k = 0; k < 3; k++) cv_rect(56 + k * 12, 29, 1, 29, C_HOLO2);
        for (y = 29; y < 58; y++) for (x = 44; x < 92; x++) if (((x + y) % 23) < 3 && bay(x, y) < 8) cv_put(x, y, C_HOLO1);   /* the sky in it */
        cv_rect(96, 25, 14, 33, C_HOLO6); cv_rect(109, 25, 1, 33, C_HOLO5);   /* a pillar, pale */
        cv_rect(110, 25, 48, 33, C_HOLO4);              /* the right wing: a deeper wall, a doorway of dark glass */
        cv_rect(116, 30, 36, 28, dk);
        for (k = 0; k < 2; k++) cv_rect(128 + k * 12, 30, 1, 28, C_HOLO2);
        cv_rect(26, 20, 138, 4, lt); cv_rect(26, 24, 138, 1, C_LABEL);   /* the roof, a white slab over all */
        for (y = 58; y < 67; y++) for (x = 0; x < SC_W; x++) cv_put(x, y, y >= 65 ? C_HOLO4 : bay(x, y) < 9 ? C_HOLO6 : lt);   /* the deck */
        cv_line(60, 62, 72, 56, lt); cv_line(60, 63, 72, 57, lt); cv_rect(61, 63, 1, 2, C_LABEL); cv_rect(71, 58, 1, 6, C_LABEL);   /* a lounger */
        for (y = 14; y < 66; y++) {                     /* a stone pine at the left: a tall trunk, bent, lit on its side */
            int tx = 14 + (66 - y) * (66 - y) / 220;
            cv_rect(tx, y, 3, 1, dk); cv_put(tx + 2, y, C_HOLO2);
        }
        cv_line(26, 20, 36, 12, dk); cv_line(25, 20, 12, 13, dk);   /* its limbs */
        for (x = 0; x < 66; x++) {                      /* its crown: flat, wide, one mass; lit on top, shaded under */
            int top = 4 + tri(x * 3, 23, 2) + (x < 6 ? 6 - x : 0) + (x > 56 ? (x - 56) : 0), bot = 21 + tri(x * 2 + 5, 17, 2) - (x > 58 ? x - 58 : 0);
            for (y = top; y < bot; y++)
                cv_put(x, y, y < top + 2 ? (bay(x, y) < 8 ? C_HOLO3 : C_HOLO2) : y > bot - 3 ? (bay(x, y) < 6 ? C_HOLO0 : dk) : bay(x, y) < 7 ? C_HOLO2 : C_HOLO1);
        }
        for (y = 67; y < SC_H; y++) for (x = 0; x < SC_W; x++) {   /* the pool: lighter far, deeper near; the house in it */
            int m = 66 - (y - 67) - 1, c = y < 82 ? cv_get(x + tri(y * 3, 5, 1), m) : 0xFF;
            if (c == dk) c = C_HOLO0;                   /* (its glass, deep in the water) */
            cv_put(x, y, c != 0xFF && c != C_HOLO4 && bay(x, y) < 4 ? c : bay(x, y) < (y - 67) / 4 ? C_HOLO0 : C_HOLO1);
        }
        for (y = 76; y < SC_H; y++) {                   /* a coping slanting across it to the bottom, the water both sides */
            int x0 = (y - 76) * 132 / 42;
            cv_rect(x0 - 3, y, 6, 1, lt); cv_put(x0 + 3, y, C_LABEL); cv_put(x0 + 4, y, C_HOLO0); cv_put(x0 - 4, y, C_HOLO7);
        }
        for (k = 0; k < 60; k++) {                      /* the diving board, out over the water from the corner */
            int bx = SC_W - 1 - k, by = 114 - k * 2 / 5;
            cv_rect(bx, by, 1, 6, k < 4 ? C_LABEL : bay(bx, by) < 12 ? lt : C_HOLO7);
            cv_put(bx - 2, by + 8, C_HOLO0);            /* its shadow on the water */
        }
    } else if (sc == 9) {                               /* CALIFORNIA: the street to the sun, palms along it */
        int vx = 114, vy = 66;
        for (x = 0; x < SC_W; x++) { int top = 66 - tri(x + 30, 90, 7) - tri(x, 31, 2); for (y = top; y < 68; y++) cv_put(x, y, bay(x, y) < 8 ? C_HOLO2 : C_HOLO1); }   /* hills */
        for (k = 0; k < 6; k++) {                       /* low shops and a motel along each side, their windows lit */
            int side = k & 1, d = 3 + k * 3, h = 6 + k * 2, w = 10 + k * 4, bx = side ? vx + d * 3 + 4 : vx - d * 3 - 4 - w, i;
            cv_rect(bx, vy - h + d / 2, w, h, dk);
            for (i = 2; i < w - 2; i += 4) cv_rect(bx + i, vy - h + d / 2 + 2, 2, 2, hsh((uint32_t)(k * 9 + i)) & 1u ? C_HOLO5 : C_HOLO3);
        }
        for (y = vy; y < SC_H; y++) {                   /* the street, its curbs, its line, the sidewalks */
            int d = y - vy, half = d * 2 + 2, cur = d * 3 + 4;
            for (x = 0; x < SC_W; x++) {
                int ax = x - vx < 0 ? vx - x : x - vx, c;
                if (ax < half) c = ax < 1 + d / 16 && (d * d / 6) % 8 < 4 ? lt : bay(x, y) < 2 ? C_HOLO0 : dk;
                else if (ax < half + 1 + d / 10) c = C_LABEL;
                else if (ax < cur) c = bay(x, y) < 3 ? C_HOLO1 : dk;
                else c = y < vy + 3 ? C_HOLO1 : dk;
                cv_put(x, y, c);
            }
        }
        cv_rect(22, 30, 20, 10, C_HOLO4); cv_rect(24, 32, 16, 6, dk); cv_rect(26, 34, 12, 2, C_HOLO6); cv_rect(31, 40, 2, 30, dk);   /* the motel's sign */
        for (k = 0; k < 3; k++) {                       /* the palms, nearer and taller, both sides */
            int d = 8 + k * k * 7, h = 16 + k * k * 9, lean = k & 1 ? 4 : -3;
            cv_palm(vx - d * 3 / 2 - 8, vy + d / 2 + 2, h, lean, dk);
            cv_palm(vx + d * 3 / 2 + 8, vy + d / 2 + 2, h, -lean, dk);
        }
        ca_near(dk, 0);                                 /* the nearest two: their trunks (their crowns above, in full screen) */
        for (k = 0; k < 3; k++) {                       /* the streetlamps up the right side, their arms over the street */
            static const uint8_t LD[3] = {10, 24, 44};
            int d = LD[k], bx = vx + d * 5 / 2, by = vy + d, ht = d * 6 / 5 + 12;
            cv_rect(bx, by - ht, 1 + d / 22, ht, dk);
            cv_line(bx, by - ht, bx - 4 - d / 8, by - ht + 1, dk);
        }
        cv_rim(dk, C_HOLO3, 7);
        cv_wire(0, 22, 92, 50, 6, dk); cv_wire(136, 50, SC_W - 1, 18, 6, dk);   /* the wires */
        cv_rect(92, 46, 1, 22, dk); cv_rect(136, 46, 1, 22, dk);
    } else if (sc == 10) {                              /* HARBOUR: a far quay, cranes, boats, the pier, its lamp */
        for (k = 0; k < 3; k++) {                       /* the cranes and sheds, ghosts in the fog */
            int cx = 118 + k * 36, h = 30 + k * 6;
            cv_rect(cx, 66 - h, 3, h, C_HOLO1); cv_rect(cx - 14, 66 - h, 30, 2, C_HOLO1); cv_line(cx + 1, 66 - h, cx + 14, 66 - h + 8, C_HOLO1);
        }
        cv_rect(96, 56, 132, 12, C_HOLO1);
        for (k = 0; k < 12; k++) cv_rect(100 + k * 10, 59, 3, 2, C_HOLO5);
        for (x = 0; x < SC_W; x++) cv_put(x, 68, C_HOLO2);
        cv_veil(26, 70, 2, 7, C_HOLO2);                 /* the fog over the far side, thicker low */
        for (k = 0; k < 3; k++) {                       /* the boats, dark, their cabins lit */
            static const uint8_t BX[3] = {28, 86, 146}, BW[3] = {40, 30, 44};
            cv_boat(BX[k], 82 - (k & 1) * 2, BW[k], dk, lt);
            cv_rect(BX[k] + BW[k] / 2 + 2, 82 - (k & 1) * 2 - 10, 2, 2, C_HOLO5);
        }
        for (y = 84; y < SC_H; y++) for (x = 0; x < SC_W; x++) {   /* the water: the boats and the fog in it */
            int sy = 84 - (y - 84) - 1, c = cv_get(x + (y % 4 == 0 ? 1 : 0), sy);
            cv_put(x, y, bay(x, y) < 6 ? (c == 0xFF || c == C_HOLO2 ? C_HOLO1 : c) : bay(x, y) < 8 ? C_HOLO0 : dk);
        }
        for (k = 0; k < 6; k++) cv_rect(198 + k * 5, 70 + (k & 1), 2, 48, dk);   /* the pier: its posts, its deck, a lamp */
        cv_rect(194, 72, 34, 3, dk);
        cv_rect(212, 32, 2, 40, dk); cv_rect(208, 30, 10, 3, dk);
        for (y = 86; y < SC_H; y++) if (bay(213, y) < 6) cv_rect(211 + (y & 1), y, 3, 1, C_HOLO6);   /* the lamp in the water */
    } else {                                            /* LIGHTHOUSE */
        for (y = 80; y < SC_H; y++) for (x = 0; x < SC_W; x++)   /* the sea, dark, the horizon lit */
            cv_put(x, y, y < 81 ? C_HOLO1 : bay(x, y) < (y < 86 ? 4 : 2) ? C_HOLO0 : dk);
        for (x = 96; x < SC_W; x++) {                   /* the cliff, its face moonlit, grass on it */
            int top = x < 142 ? 88 - (x - 96) * 30 / 46 + tri(x * 3, 11, 3) : 58 + tri(x, 19, 2) - (x > 200 ? (x - 200) / 6 : 0);
            for (y = top; y < SC_H; y++)
                cv_put(x, y, x < 146 && bay(x, y) < 4 - (y - top) / 12 ? C_HOLO0 : (y - top) % 9 == 4 && x < 150 && bay(x, y) < 6 ? C_HOLO1 : dk);
            if (!(x % 3) && x > 140) cv_rect(x, top - 2, 1, 2, dk);
        }
        for (k = 0; k < 8; k++) { uint32_t q = hsh((uint32_t)k + 40u); cv_disc(66 + k * 7, 104 + (int)(q % 10u), 3 + (int)((q >> 8) % 4u), dk); }   /* rocks */
        cv_rect(96, 92, 14, 2, dk); cv_rect(118, 76, 10, 2, dk);   /* ledges */
        cv_rim(dk, C_HOLO1, 10);
        for (y = 14; y < 60; y++) {                     /* the tower, its bands, tapering */
            int half = 4 + (y - 14) / 11;
            for (x = -half; x <= half; x++) cv_put(178 + x, y, (y / 7) & 1 ? (x < half - 1 ? C_HOLO4 : C_HOLO3) : x < half - 1 ? lt : C_LABEL);
        }
        cv_rect(172, 13, 13, 2, dk);                    /* its gallery, its lantern, its cap */
        cv_rect(174, 6, 9, 7, dk); cv_rect(175, 7, 7, 5, C_HOLO6);
        cv_rect(173, 4, 11, 2, dk); cv_rect(178, 1, 1, 3, dk);
        cv_rect(196, 48, 24, 11, lt); cv_rect(196, 48, 24, 1, C_LABEL); cv_rect(216, 49, 4, 10, C_LABEL);   /* the keeper's house */
        for (k = 0; k < 6; k++) cv_rect(194 + k, 48 - k, 28 - k * 2, 1, dk);
    }
}

/* ---- above and below, in full screen */
DMK_SMALL static void land_top(int sc, int dk, int lt)
{
    int x, y, k;
    (void)lt;
    cv_w = DMK_W; cv_h = LF_H;
    if (sc != 7) for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) cv_put(x, y, sky_full(sc, x, y, dk));
    if (sc == 6) {                                      /* the canopy: masses, branches, the sky's glow in its holes */
        for (k = 0; k < 64; k++) {
            uint32_t q = hsh((uint32_t)k * 2654435761u + 9u);
            cv_disc((int)(q % (uint32_t)DMK_W), (int)((q >> 8) % (uint32_t)LF_H), 6 + (int)((q >> 16) % 9u), dk);
        }
        for (k = 0; k < 2; k++) { cv_line(k ? DMK_W - 1 : 0, 34, k ? 170 : 70, 60, dk); cv_line(k ? DMK_W - 1 : 0, 35, k ? 170 : 70, 61, dk); }
        cv_rim(dk, C_HOLO3, 6);
    } else if (sc == 7) {                               /* the wall going up, a poster, a shelf of records */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) cv_put(x, y, y < 4 ? dk : y == 4 ? C_LABEL : bay(x, y) < 1 ? C_HOLO0 : dk);
        cv_rect(78, 16, 40, 38, C_LABEL);               /* the poster: a sun over a grid */
        for (y = 18; y < 52; y++) for (x = 80; x < 116; x++) {
            int dx = x - 98, dy = y - 34;
            cv_put(x, y, y > 36 ? ((y - 36) % 4 == 0 || (x - 98) * 4 % ((y - 34) * 2) == 0 ? C_HOLO5 : C_HOLO0)
                   : dx * dx + dy * dy < 80 ? ((y & 3) ? C_HOLO6 : C_HOLO4) : C_HOLO2);
        }
        cv_rect(146, 46, 68, 2, C_LABEL);               /* the shelf: records on their spines, a plant */
        for (k = 0; k < 14; k++) cv_rect(150 + k * 3, 33 + (k * 5 % 3), 2, 13 - (k * 5 % 3), k % 5 == 2 ? C_HOLO3 : k % 3 ? C_A_INK2 : C_LABEL);
        cv_rect(196, 40, 10, 6, C_HOLO3); for (k = 0; k < 5; k++) cv_line(201, 40, 195 + k * 3, 30 + (k & 1) * 4, C_HOLO0);
    } else if (sc == 9) {                               /* the nearest palms' crowns, the wires going on */
        ca_near(dk, 1);
        cv_wire(0, 50, DMK_W - 1, 40, 10, dk);
        cv_rim(dk, C_HOLO3, 6);
    } else if (sc == 10) {                              /* a crane's arm into the fog, gulls */
        cv_rect(150, 40, 90, 3, C_HOLO1); cv_line(200, 40, 239, 20, C_HOLO1); cv_rect(170, 43, 1, 18, C_HOLO1);
        for (k = 0; k < 3; k++) { int gx = 40 + k * 22, gy = 28 + (k & 1) * 6; cv_line(gx - 3, gy, gx, gy + 2, dk); cv_line(gx, gy + 2, gx + 3, gy, dk); }
        cv_veil(20, LF_H, 0, 3, C_HOLO1);
    }
}
DMK_SMALL static void land_bottom(int sc, int dk, int lt)
{
    int x, y, k;
    cv_w = DMK_W; cv_h = LF_H;
    if (sc == 6) {                                      /* the floor nearer: the path, roots, ferns, mushrooms */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int pc = 120 + tri(y * 2, 40, 6) - 3, half = 50 + y * 3 / 2, d = x - pc < 0 ? pc - x : x - pc;
            cv_put(x, y, d < half ? (bay(x, y) < 4 ? C_HOLO3 : C_HOLO1) : d < half + 2 ? C_HOLO1 : bay(x, y) < 2 ? C_HOLO0 : dk);
        }
        for (k = 0; k < 2; k++) { cv_line(k ? DMK_W - 1 : 0, 4, k ? 190 : 50, 40, dk); cv_line(k ? DMK_W - 1 : 0, 5, k ? 190 : 50, 41, dk); }
        for (k = 0; k < 18; k++) {
            uint32_t q = hsh((uint32_t)k * 2654435761u + 21u);
            int side = k & 1, d = (int)(q % 40u), fx = side ? 200 - (int)((q >> 8) % 40u) : 40 + (int)((q >> 8) % 40u);
            cv_fern(fx, 20 + d, 8 + d / 5, C_HOLO0, C_HOLO2);
        }
        for (k = 0; k < 4; k++) { int mx = 30 + k * 60, my = 44 + (k & 1) * 8; cv_rect(mx, my, 2, 5, C_LABEL); cv_rect(mx - 3, my - 2, 8, 2, C_HOLO4); cv_put(mx - 1, my - 2, lt); }
    } else if (sc == 7) {                               /* the couch's back down to the floor, the rug, a tape, the remote */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++)
            cv_put(x, y, y < 24 ? (bay(x, y) < 1 ? C_HOLO0 : dk) : y < 26 ? C_A_INK2 : (y - 26) % 7 == 0 ? C_HOLO0 : dk);
        for (y = 34; y < LF_H; y++) for (x = 30; x < 210; x++) {   /* the rug, round, its rings */
            int dx = (x - 120) * 2 / 9, dy = y - 48, d = dx * dx + dy * dy;
            if (d < 300) cv_put(x, y, d < 90 ? (bay(x, y) < 6 ? C_HOLO2 : C_HOLO0) : d < 200 ? C_HOLO0 : (bay(x, y) < 8 ? C_HOLO1 : C_HOLO0));
        }
        cv_rect(150, 44, 18, 10, dk); cv_rect(152, 46, 14, 4, lt); cv_rect(153, 47, 8, 1, C_HOLO3);   /* a tape's box */
        cv_rect(78, 46, 16, 6, C_LABEL); cv_rect(80, 47, 2, 2, C_HOLO4); cv_rect(84, 47, 6, 1, dk);   /* the remote */
    } else if (sc == 8) {                               /* the pool nearer: deeper, its coping across it and the board going on */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int x0 = LF_X0 + (y + SC_H - 76) * 132 / 42;
            cv_put(x, y, x >= x0 - 3 && x < x0 + 3 ? lt : x == x0 + 3 ? C_LABEL : bay(x, y) < 12 ? C_HOLO0 : C_HOLO1);
        }
        for (k = 0; k < 40; k++) { int bx = DMK_W - 1 - k, by = 4 - k * 2 / 5; cv_rect(bx, by < 0 ? 0 : by, 1, 6 + (by < 0 ? by : 0), lt); }
    } else if (sc == 9) {                               /* the street nearer: the curbs, the line */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int d = y + SC_H - 66, ax = x - 120 < 0 ? 120 - x : x - 120, half = d * 2 + 2;
            cv_put(x, y, ax < half ? (bay(x, y) < 2 ? C_HOLO0 : dk) : ax < half + 1 + d / 10 ? C_LABEL : bay(x, y) < 3 ? C_HOLO1 : dk);
        }
    } else if (sc == 10) {                              /* the quay: its stones wet, a bollard, its rope, a lantern */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int gx = (x + (y / 9 & 1) * 12) % 24, gy = y % 9;
            cv_put(x, y, y < 14 ? (bay(x, y) < 3 ? C_HOLO0 : dk) : !gx || !gy ? dk : gy == 1 && bay(x, y) < 10 ? C_HOLO1 : bay(x, y) < 4 ? C_HOLO0 : dk);
        }
        cv_rect(60, 8, 14, 22, dk); cv_rect(57, 6, 20, 4, dk); cv_rect(57, 6, 20, 1, C_HOLO1); cv_rect(60, 10, 1, 20, C_HOLO1);
        cv_line(74, 14, 130, 2, C_HOLO2); cv_line(74, 15, 130, 3, C_HOLO2);
        cv_rect(170, 24, 30, 18, dk); cv_rect(170, 24, 30, 1, C_HOLO1); cv_rect(170, 32, 30, 1, C_A_INK2);   /* a crate */
        cv_rect(214, 26, 6, 9, C_HOLO6); cv_rect(213, 24, 8, 2, dk); cv_rect(213, 35, 8, 2, dk);   /* a lantern on it */
        for (y = -10; y <= 10; y++) for (x = -10; x <= 10; x++) if (x * x + y * y < 100 && bay(x + 217, y + 30) < 3) cv_put(217 + x, 30 + y, C_HOLO5);
    } else {                                            /* the sea nearer, waves, rocks, the cliff's foot */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++)
            cv_put(x, y, (y + tri(x, 29, 2)) % 9 == 0 && bay(x, y) < 10 ? C_HOLO1 : bay(x, y) < 2 ? C_HOLO0 : dk);
        for (y = 0; y < LF_H; y++) for (x = 130; x < DMK_W; x++) if (x > 160 - y / 2) cv_put(x, y, bay(x, y) < 1 ? C_HOLO0 : dk);
        for (k = 0; k < 7; k++) { uint32_t q = hsh((uint32_t)k + 70u); cv_disc(40 + k * 22 + (int)(q % 10u), 30 + (int)((q >> 8) % 26u), 5 + (int)((q >> 16) % 8u), dk); }
        cv_rim(dk, C_HOLO1, 10);
    }
}

/* ---- what moves on them, each 140 ms frame, over the still parts */
static int lt_flash(uint32_t tick) { return (int)(tick % 24u); }
DMK_SMALL static void land_anim(int sc, uint32_t tick, int dk, int lt)
{
    int i, x, y;
    (void)dk;
    if (sc == 6)                                        /* motes in the rays, drifting down */
        for (i = 0; i < 26; i++) {
            uint32_t h = hsh((uint32_t)i * 2654435761u + 5u);
            int k = (int)(h % 3u), my = 12 + (int)(((h >> 8) % 80u + tick / 2u) % 80u), half = 4 + my / 16;
            int mx = 120 + k * 46 - my * 3 / 5 + (int)((h >> 16) % (uint32_t)half) - half / 2;
            if (mx > 9 && mx < SC_W - 11 && (hsh(h + tick / 3u) & 1u)) px_put(SX + mx, SY + my, i & 1 ? C_HOLO7 : C_HOLO6);
        }
    if (sc == 7) {                                      /* the TV: a tape playing, scanlines, the tracking rolling */
        int band = (int)(tick * 3u % 52u);
        for (y = 34; y < 74; y++) for (x = 66; x < 112; x++) {
            int dx = x - 92, dy = y - 56, hill = 60 - tri(x, 23, 5) - tri(x + 5, 11, 2);
            int c = bay(x, y) < (y - 34) * 16 / 26 ? C_HOLO4 : C_HOLO2;   /* the tape: a sun going down behind hills */
            if (dx * dx + dy * dy < 70) c = dy & 1 ? C_HOLO6 : C_HOLO7;
            if (y >= hill) c = bay(x, y) < 3 ? C_HOLO1 : C_HOLO0;
            if ((y & 1) && bay(x, y) < 5) c = C_HOLO0;
            if (y - 30 > band - 3 && y - 30 < band && (hsh((uint32_t)(x + tick * 7u)) & 3u)) c = C_LINE;
            px_put(SX + x, SY + y, c);
        }
        fill(SX + 68, SY + 36, 2, 5, lt); fill(SX + 70, SY + 37, 1, 3, lt); fill(SX + 71, SY + 38, 1, 1, lt);   /* PLAY */
        if ((tick / 4u) & 1u) for (i = 0; i < 4; i++) fill(SX + 89 + i * 4 - (i > 1 ? 0 : 1), SY + 90, 3, 2, C_HOLO1);   /* 12:00 */
        for (i = 0; i < 2; i++) {                       /* the woofers moving with the sound */
            int sx = i ? 153 : 35, r = 3 + (syn_peak[i ? 1 : 0] > 3000 ? (int)(tick & 1u) : 0);
            round_line(SX + sx - r, SY + 78 - r, r * 2 + 1, r * 2 + 1, r, 1, C_LABEL);
        }
    }
    if (sc == 8) {                                      /* the pool's ripples, Hockney's: a few short lines of light a row,
                                                           the rows apart, each line in its own place (none crossing),
                                                           drifting; wider nearer; only on the water */
        for (y = 72; y < SC_H; y += 6) {
            int n = 3 + (y - 72) / 18, cell = SC_W / n, j;
            for (i = 0; i < n; i++) {
                uint32_t h = hsh((uint32_t)(y * 131 + i));
                int len = 4 + (int)(h >> 28) + (y - 72) / 8, rx = i * cell + (int)((h % (uint32_t)(cell - len > 1 ? cell - len : 1) + tick / 4u) % (uint32_t)cell);
                for (j = 0; j < len; j++) {
                    int px = rx + j, py = y + (j > len / 4 && j < len * 3 / 4) * ((int)((tick / 6u + h) & 1u) ? -1 : 0);
                    int c = px < SC_W && py < SC_H ? sc_fg[py * SC_W + px] : 0xFF;
                    if (c == C_HOLO0 || c == C_HOLO1) px_put(SX + px, SY + py, j == 0 || j == len - 1 ? C_HOLO6 : C_HOLO7);
                }
            }
        }
        for (i = 0; i < 6; i++) { uint32_t h = hsh((uint32_t)i * 131u + tick); px_put(SX + 160 + (int)(h % 60u), SY + 53 + (int)((h >> 8) % 4u), C_HOLO6); }
    }
    if (sc == 9) {                                      /* the sun's bands moving down, a car going away */
        for (y = 0; y < 8; y++) { int by = 50 + (int)((tick / 2u + (uint32_t)y * 4u) % 16u); if (by < 66) fill(SX + 96, SY + by, 36, 1, C_HOLO4); }
        i = (int)(tick % 40u);
        x = 114 + 10 - i / 4; y = 66 + 40 - i;
        if (y > 68) { fill(SX + x - 2 - (40 - i) / 8, SY + y, 2, 1, C_HOLO5); fill(SX + x + 2 + (40 - i) / 8, SY + y, 2, 1, C_HOLO5); }
    }
    if (sc == 10) {                                     /* the fog drifting, the lamp's halo, a buoy blinking */
        for (y = 40; y < 96; y++) for (x = 0; x < SC_W; x += 1)
            if (bay(x + (int)(tick / 2u), y) == 0 && ((y / 6 + (int)tick / 8) & 3) == 0) px_put(SX + x, SY + y, C_HOLO6);
        for (y = -7; y <= 7; y++) for (x = -7; x <= 7; x++) if (x * x + y * y < 50 && bay(x + 213, y + 34) < 4 + (int)(tick & 1u)) px_put(SX + 213 + x, SY + 34 + y, C_HOLO6);
        fill(SX + 211, SY + 33, 4, 2, C_HOLO7);
        if (tick % 10u < 3u) fill(SX + 128, SY + 76, 2, 2, C_HOLO5);
    }
    if (sc == 11) {                                     /* the beam: swept left across the sky, a flash as it faces us */
        int p = lt_flash(tick);
        if (p < 12) {
            int len = 60 + p * 10, slope = p - 6;
            for (x = 0; x < len && 172 - x > 0; x++) {
                int half = 1 + x / 9, cy = 9 + x * (24 + slope) / 172;
                for (y = -half; y <= half; y++)
                    if (bay(172 - x, cy + y) < 7 - x * 6 / len - (y * y > half * half / 2)) px_put(SX + 174 - x, SY + cy + y, x < 24 ? C_HOLO7 : C_HOLO6);
            }
        }
        fill(SX + 173, SY + 7, 7, 5, p >= 11 && p < 14 ? C_HOLO7 : C_HOLO6);
        for (i = 0; i < 30; i++) {                      /* the moon's road on the sea, glittering */
            uint32_t h = hsh((uint32_t)i * 977u + tick);
            int gy = 82 + (int)(h % 36u), w = 3 + (gy - 80) / 3;
            px_put(SX + 81 - w / 2 + (int)((h >> 8) % (uint32_t)w), SY + gy, h & 0x400u ? lt : C_HOLO6);
        }
        for (i = 0; i < 24; i++) {                      /* the surf on the rocks */
            uint32_t h = hsh((uint32_t)i * 2654435761u + tick);
            px_put(SX + 66 + (int)(h % 70u), SY + 104 + (int)((h >> 9) % 12u), h & 0x200u ? lt : C_HOLO1);
        }
    }
}
/* a synth's light in them: a mushroom's glow, the deck's LEDs, a pool light, a streetlamp, a mast's light, a window */
DMK_SMALL static void land_light(int sc, int i, int on, uint32_t tick)
{
    int x, y;
    if (sc == 6) {                                      /* mushrooms by the path, glowing */
        static const uint8_t GX[3] = {72, 152, 188}, GY[3] = {108, 104, 112};
        x = GX[i]; y = GY[i];
        fill(SX + x, SY + y + 2, 1, 3, C_LABEL);
        fill(SX + x - 2, SY + y, 5, 2, on ? C_HOLO7 : C_HOLO3);
        if (on) { px_put(SX + x - 3, SY + y + 1, C_HOLO5); px_put(SX + x + 3, SY + y + 1, C_HOLO5); px_put(SX + x, SY + y - 1, C_HOLO5); }
    } else if (sc == 7) fill(SX + 60 + i * 4, SY + 93, 2, 1, on ? C_HOLO6 : C_A_INK2);   /* the deck's LEDs */
    else if (sc == 8) { x = 84 + i * 40; fill(SX + x, SY + 70, 6, 2, on ? C_HOLO7 : C_HOLO2); if (on) { fill(SX + x - 2, SY + 72, 10, 1, C_HOLO5); fill(SX + x + 1, SY + 73, 4, 1, C_HOLO6); } }   /* pool lights */
    else if (sc == 9) {                                 /* the streetlamps' heads */
        static const uint8_t LD[3] = {10, 24, 44};
        int d = LD[i], hx = 114 + d * 5 / 2 - 5 - d / 8, hy = 66 + d - d * 6 / 5 - 12 + 1, w = 2 + d / 16;
        fill(SX + hx, SY + hy, w, 2, on ? C_HOLO7 : C_LABEL);
        if (on) for (y = 0; y < 5; y++) px_put(SX + hx + (int)(hsh((uint32_t)(i * 9 + y) + tick) % (uint32_t)(w + y)) - y / 2, SY + hy + 3 + y * 2, C_HOLO5);
    } else if (sc == 10) { static const uint8_t MX[3] = {41, 96, 160}, MY[3] = {53, 51, 53}; fill(SX + MX[i] - 1, SY + MY[i] - 1, 3, 2, on ? C_HOLO7 : C_HOLO4); }   /* mast lights */
    else fill(SX + 198 + i * 6, SY + 51, 3, 3, on ? C_HOLO7 : C_HOLO2);   /* the keeper's windows */
}

/* full screen: what moves above and below the band */
DMK_SMALL static void land_full_anim(int sc, uint32_t t, int dk, int lt)
{
    int k, y;
    (void)dk;
    if (sc == 9)                                        /* the street's line coming at us */
        for (y = 0; y < LF_H; y++) {
            int d = y + SC_H - 66;
            if (((d * d / 6) + (int)(t / 140u) * 3) % 8 < 4) fill(120 - d / 32, LF_Y0 + SC_H + y, 1 + d / 16, 1, lt);
        }
    if (sc == 11)                                       /* foam on the near rocks */
        for (k = 0; k < 40; k++) {
            uint32_t h = hsh((uint32_t)k * 2654435761u + t / 140u);
            px_put(30 + (int)(h % 170u), LF_Y0 + SC_H + 22 + (int)((h >> 9) % 34u), h & 0x200u ? lt : C_HOLO1);
        }
    if (sc == 8)                                        /* the ripples nearer: longer, apart, on the water only */
        for (y = 3; y < LF_H; y += 8) {
            int i, j, n = 4, cell = DMK_W / n;
            for (i = 0; i < n; i++) {
                uint32_t h = hsh((uint32_t)(y * 977 + i));
                int len = 10 + (int)(h >> 28) + y / 6, rx = i * cell + (int)((h % (uint32_t)(cell - len) + t / 560u) % (uint32_t)cell);
                int x0 = LF_X0 + (y + SC_H - 76) * 132 / 42;
                for (j = 0; j < len; j++) {
                    int px = rx + j, py = LF_Y0 + SC_H + y - (j > len / 4 && j < len * 3 / 4 && ((t / 840u + h) & 1u));
                    if ((px > x0 + 5 || px < x0 - 5) && px >= 0 && px < DMK_W && py < DMK_H) px_put(px, py, j == 0 || j == len - 1 ? C_HOLO6 : C_HOLO7);
                }
            }
        }
}
