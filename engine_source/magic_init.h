#ifndef MAGIC_INIT_H
#define MAGIC_INIT_H

#include "bitscan.h"
#include "magic_numbers.h"

#define BISHOP_TABLE_SIZE 512
#define ROOK_TABLE_SIZE   4096

static Bitboard BishopMask[64];
static Bitboard RookMask[64];
static int      BishopShift[64];
static int      RookShift[64];
static Bitboard BishopAttacks[64][BISHOP_TABLE_SIZE];
static Bitboard RookAttacks[64][ROOK_TABLE_SIZE];

static int g_magic_initialized = 0;

static Bitboard mi_rook_mask(int sq) {
    Bitboard m = 0;
    int rk = sq / 8, fl = sq % 8, r, f;
    for (r = rk + 1; r <= 6; r++) m |= (Bitboard)1 << (fl + r * 8);
    for (r = rk - 1; r >= 1; r--) m |= (Bitboard)1 << (fl + r * 8);
    for (f = fl + 1; f <= 6; f++) m |= (Bitboard)1 << (f + rk * 8);
    for (f = fl - 1; f >= 1; f--) m |= (Bitboard)1 << (f + rk * 8);
    return m;
}
static Bitboard mi_bishop_mask(int sq) {
    Bitboard m = 0;
    int rk = sq / 8, fl = sq % 8, r, f;
    for (r = rk + 1, f = fl + 1; r <= 6 && f <= 6; r++, f++) m |= (Bitboard)1 << (f + r * 8);
    for (r = rk + 1, f = fl - 1; r <= 6 && f >= 1; r++, f--) m |= (Bitboard)1 << (f + r * 8);
    for (r = rk - 1, f = fl + 1; r >= 1 && f <= 6; r--, f++) m |= (Bitboard)1 << (f + r * 8);
    for (r = rk - 1, f = fl - 1; r >= 1 && f >= 1; r--, f--) m |= (Bitboard)1 << (f + r * 8);
    return m;
}

static Bitboard mi_rook_attacks_slow(int sq, Bitboard occ) {
    Bitboard a = 0;
    int rk = sq / 8, fl = sq % 8, r, f;
    for (r = rk + 1; r <= 7; r++) { a |= (Bitboard)1 << (fl + r * 8); if (occ & ((Bitboard)1 << (fl + r * 8))) break; }
    for (r = rk - 1; r >= 0; r--) { a |= (Bitboard)1 << (fl + r * 8); if (occ & ((Bitboard)1 << (fl + r * 8))) break; }
    for (f = fl + 1; f <= 7; f++) { a |= (Bitboard)1 << (f + rk * 8); if (occ & ((Bitboard)1 << (f + rk * 8))) break; }
    for (f = fl - 1; f >= 0; f--) { a |= (Bitboard)1 << (f + rk * 8); if (occ & ((Bitboard)1 << (f + rk * 8))) break; }
    return a;
}
static Bitboard mi_bishop_attacks_slow(int sq, Bitboard occ) {
    Bitboard a = 0;
    int rk = sq / 8, fl = sq % 8, r, f;
    for (r = rk + 1, f = fl + 1; r <= 7 && f <= 7; r++, f++) { a |= (Bitboard)1 << (f + r * 8); if (occ & ((Bitboard)1 << (f + r * 8))) break; }
    for (r = rk + 1, f = fl - 1; r <= 7 && f >= 0; r++, f--) { a |= (Bitboard)1 << (f + r * 8); if (occ & ((Bitboard)1 << (f + r * 8))) break; }
    for (r = rk - 1, f = fl + 1; r >= 0 && f <= 7; r--, f++) { a |= (Bitboard)1 << (f + r * 8); if (occ & ((Bitboard)1 << (f + r * 8))) break; }
    for (r = rk - 1, f = fl - 1; r >= 0 && f >= 0; r--, f--) { a |= (Bitboard)1 << (f + r * 8); if (occ & ((Bitboard)1 << (f + r * 8))) break; }
    return a;
}

static int mi_enumerate_occupancies(Bitboard mask, Bitboard *out) {
    int n = 0;
    Bitboard occ = 0;
    do {
        out[n++] = occ;
        occ = (occ - mask) & mask;
    } while (occ);
    return n;
}

static void init_magic_bitboards(void) {
    if (g_magic_initialized) return;

    Bitboard occs[ROOK_TABLE_SIZE];

    for (int sq = 0; sq < 64; sq++) {

        BishopMask[sq]  = mi_bishop_mask(sq);
        BishopShift[sq] = 64 - popcount(BishopMask[sq]);
        int nb = mi_enumerate_occupancies(BishopMask[sq], occs);
        for (int i = 0; i < nb; i++) {
            Bitboard attack = mi_bishop_attacks_slow(sq, occs[i]);
            unsigned idx = (unsigned)((occs[i] * BishopMagic[sq]) >> BishopShift[sq]);
            BishopAttacks[sq][idx] = attack;
        }

        RookMask[sq]  = mi_rook_mask(sq);
        RookShift[sq] = 64 - popcount(RookMask[sq]);
        int nr = mi_enumerate_occupancies(RookMask[sq], occs);
        for (int i = 0; i < nr; i++) {
            Bitboard attack = mi_rook_attacks_slow(sq, occs[i]);
            unsigned idx = (unsigned)((occs[i] * RookMagic[sq]) >> RookShift[sq]);
            RookAttacks[sq][idx] = attack;
        }
    }

    g_magic_initialized = 1;
}

static inline Bitboard bishop_attacks(int sq, Bitboard occ) {
    unsigned idx = (unsigned)(((occ & BishopMask[sq]) * BishopMagic[sq]) >> BishopShift[sq]);
    return BishopAttacks[sq][idx];
}
static inline Bitboard rook_attacks(int sq, Bitboard occ) {
    unsigned idx = (unsigned)(((occ & RookMask[sq]) * RookMagic[sq]) >> RookShift[sq]);
    return RookAttacks[sq][idx];
}
static inline Bitboard queen_attacks(int sq, Bitboard occ) {
    return bishop_attacks(sq, occ) | rook_attacks(sq, occ);
}

#endif
