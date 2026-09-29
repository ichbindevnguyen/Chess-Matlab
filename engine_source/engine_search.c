#ifndef STANDALONE_PERFT
#include "mex.h"
#else
#define mexErrMsgTxt(msg) do { fprintf(stderr, "ERROR: %s\n", msg); exit(1); } while(0)
#endif
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "bitscan.h"
#include "magic_numbers.h"
#include "magic_init.h"

#define more_than_one(b) (((b) & ((b) - 1)) != 0)
#define MAX_MOVES   256
#define MAX_PLY     128
#define INF         32001
#define MATE_VAL    32000
#define MATE_TH     (MATE_VAL - MAX_PLY)
#define VALUE_NONE  32002
#define WHITE       0
#define BLACK       1

#define NO_PIECE    0
#define PAWN        1
#define KNIGHT      2
#define BISHOP      3
#define ROOK        4
#define QUEEN       5
#define KING        6

#define MOVE_TO(m)      ((m) & 0x3F)
#define MOVE_FROM(m)    (((m) >> 6) & 0x3F)
#define MOVE_TYPE(m)    (((m) >> 12) & 0x3)
#define MOVE_PROMO(m)   (((m) >> 14) & 0x3)
#define MT_NORMAL    0
#define MT_PROMOTION 1
#define MT_EP        2
#define MT_CASTLING  3
#define MAKE_MOVE(from, to, type, promo) \
    ((uint16_t)((to) | ((from) << 6) | ((type) << 12) | ((promo) << 14)))
#define MOVE_NONE 0
#define MOVE_NULL 65

static const int PieceValue[7] = {0, 208, 781, 825, 1276, 2538, 0};

#define CR_WK  1
#define CR_WQ  2
#define CR_BK  4
#define CR_BQ  8

#define FILE_A  0x0101010101010101ULL
#define FILE_B  (FILE_A << 1)
#define FILE_C  (FILE_A << 2)
#define FILE_D  (FILE_A << 3)
#define FILE_E  (FILE_A << 4)
#define FILE_F  (FILE_A << 5)
#define FILE_G  (FILE_A << 6)
#define FILE_H  (FILE_A << 7)
#define RANK_1  0xFFULL
#define RANK_2  (RANK_1 << 8)
#define RANK_3  (RANK_1 << 16)
#define RANK_4  (RANK_1 << 24)
#define RANK_5  (RANK_1 << 32)
#define RANK_6  (RANK_1 << 40)
#define RANK_7  (RANK_1 << 48)
#define RANK_8  (RANK_1 << 56)

#define SQ_RANK(sq) ((sq) >> 3)
#define SQ_FILE(sq) ((sq) & 7)
#define SQ_MAKE(r,f) (((r) << 3) | (f))
#define SQ_FLIP(sq) ((sq) ^ 56)

typedef struct {
    Bitboard byType[7];
    Bitboard byColor[2];
    int8_t   board[64];
    int8_t   pieceColor[64];
    int      side;
    int      castling;
    int      ep;
    int      rule50;
    int      ply;
    uint64_t key;
    uint64_t pawnKey;
    int      npm[2];
    Bitboard checkers;
    int      kingSquare[2];
} Position;

typedef struct {
    uint64_t key;
    uint64_t pawnKey;
    int      castling;
    int      ep;
    int      rule50;
    int8_t   captured;
    int8_t   capturedColor;
    Bitboard checkers;
    int      npm[2];
} UndoInfo;

typedef struct {
    int      ply;
    int      staticEval;
    uint16_t currentMove;
    uint16_t killers[2];
    int      moveCount;
    int8_t   movedPiece;
    int8_t   movedTo;
    uint64_t key;
} SStack;

#define TT_BOUND_NONE   0
#define TT_BOUND_UPPER  1
#define TT_BOUND_LOWER  2
#define TT_BOUND_EXACT  3

typedef struct {
    uint16_t key16;
    int16_t  value;
    int16_t  eval;
    uint16_t move;
    int8_t   depth;
    uint8_t  genBound;
} TTEntry;

typedef struct {
    TTEntry entry[3];
    char padding[2];
} TTCluster;

static TTCluster *tt_table = NULL;
static uint32_t   tt_clusterCount = 0;
static uint8_t    tt_generation = 0;

static void tt_resize(int mb) {
    if (tt_table) { free(tt_table); tt_table = NULL; }
    size_t bytes = (size_t)mb * 1024 * 1024;
    tt_clusterCount = (uint32_t)(bytes / sizeof(TTCluster));
    if (tt_clusterCount < 1024) tt_clusterCount = 1024;
    tt_table = (TTCluster*)calloc(tt_clusterCount, sizeof(TTCluster));
    if (!tt_table) mexErrMsgTxt("Cannot allocate TT");
}

static void tt_clear(void) {
    if (tt_table) memset(tt_table, 0, (size_t)tt_clusterCount * sizeof(TTCluster));
}

static void tt_new_search(void) {
    tt_generation += 4;
}

static inline TTCluster* tt_cluster(uint64_t key) {

    uint32_t idx = (uint32_t)(((unsigned __int128)key * (unsigned __int128)tt_clusterCount) >> 64);
    return &tt_table[idx];
}

static int tt_probe(uint64_t key, int depth, int ply, int *tt_move, int *tt_value,
                    int *tt_eval, int *tt_depth, int *tt_bound) {
    TTCluster *cl = tt_cluster(key);
    uint16_t k16 = (uint16_t)key;
    *tt_move = MOVE_NONE;
    *tt_value = VALUE_NONE;
    *tt_eval = VALUE_NONE;
    *tt_depth = -9;
    *tt_bound = TT_BOUND_NONE;

    for (int i = 0; i < 3; i++) {
        if (cl->entry[i].key16 == k16 && cl->entry[i].depth != 0) {
            *tt_move  = cl->entry[i].move;
            int val = cl->entry[i].value;
            if (val != VALUE_NONE) {
                if (val > MATE_TH) val -= ply;
                else if (val < -MATE_TH) val += ply;
            }
            *tt_value = val;
            *tt_eval  = cl->entry[i].eval;
            *tt_depth = cl->entry[i].depth;
            *tt_bound = cl->entry[i].genBound & 3;

            cl->entry[i].genBound = (uint8_t)(tt_generation | (*tt_bound));
            return 1;
        }
    }
    return 0;
}

static void tt_store(uint64_t key, int depth, int ply, int value, int eval,
                     uint16_t move, int bound) {
    TTCluster *cl = tt_cluster(key);
    uint16_t k16 = (uint16_t)key;

    int tt_val = value;
    if (tt_val > MATE_TH) tt_val += ply;
    else if (tt_val < -MATE_TH) tt_val -= ply;

    int replace_idx = 0;
    int worst_score = 999999;
    for (int i = 0; i < 3; i++) {
        if (cl->entry[i].key16 == k16) {

            if (bound == TT_BOUND_EXACT || depth >= cl->entry[i].depth - 2) {
                replace_idx = i;
                break;
            }

            if (move == MOVE_NONE) move = cl->entry[i].move;
            replace_idx = i;
            break;
        }

        int age = (int)(tt_generation >> 2) - (int)(cl->entry[i].genBound >> 2);
        if (age < 0) age += 64;
        int score = (int)cl->entry[i].depth - 4 * age;
        if (score < worst_score) {
            worst_score = score;
            replace_idx = i;
        }
    }

    TTEntry *e = &cl->entry[replace_idx];
    e->key16 = k16;
    e->value = (int16_t)tt_val;
    e->eval  = (int16_t)eval;
    e->move  = move;
    e->depth = (int8_t)depth;
    e->genBound = (uint8_t)(tt_generation | bound);
}

static Bitboard KnightAttacks[64];
static Bitboard KingAttacks[64];
static Bitboard PawnAttacks[2][64];
static Bitboard BetweenBB[64][64];
static Bitboard LineBB[64][64];
static Bitboard FileBB[8];
static Bitboard RankBB[8];
static Bitboard AdjacentFiles[8];

static uint64_t zobPiece[2][7][64];
static uint64_t zobSide;
static uint64_t zobCastle[16];
static uint64_t zobEp[8];
static int g_tables_init = 0;

static const int pst_mg[7][64] = {
{0},

{  0,  0,  0,  0,  0,  0,  0,  0,
 -35, -1,-20,-23,-15, 24, 38,-22,
 -26, -4, -4,-10,  3,  3, 33,-12,
 -27, -2, -5, 12, 17,  6, 10,-25,
 -14, 13,  6, 21, 23, 12, 17,-23,
  -6,  7, 26, 31, 65, 56, 25,-20,
  98,134, 61, 95, 68,126, 34,-11,
   0,  0,  0,  0,  0,  0,  0,  0},

{ -105, -21,-58,-33,-17,-28, -19,-23,
  -29, -53,-12, -3, -1, 18,-14, -19,
  -23, -9, 12, 10, 19, 17, 25, -16,
  -13,  4, 16, 13, 28, 19, 21,  -8,
   -9, 17, 19, 53, 37, 69, 18,  22,
  -47, 60, 37, 65, 84,129, 73,  44,
  -73,-41, 72, 36, 23, 62,  7, -17,
 -167,-89,-34,-49, 61,-97,-15,-107},

{ -33, -3,-14,-21,-13,-12,-39,-21,
    4, 15, 16,  0,  7, 21, 33,  1,
    0, 15, 15, 15, 14, 27, 18, 10,
   -6, 13, 13, 26, 34, 12, 10,  4,
   -4,  5, 19, 50, 37, 37,  7, -2,
  -16, 37, 43, 40, 35, 50, 37, -2,
  -26, 16,-18,-13, 30, 59, 18,-47,
  -29,  4,-82,-37,-25,-42,  7, -8},

{ -19,-13,  1, 17, 16,  7,-37,-26,
  -44,-16,-20, -9, -1, 11, -6,-71,
  -45,-25,-16,-17,  3,  0, -5,-33,
  -36,-26,-12, -1,  9, -7,  6,-23,
  -24,-11,  7, 26, 24, 35, -8,-20,
   -5, 19, 26, 36, 17, 45, 61, 16,
   27, 32, 58, 62, 80, 67, 26, 44,
   32, 42, 32, 51, 63,  9, 31, 43},

{  -1,-18, -9, 10,-15,-25,-31,-50,
  -35, -8, 11,  2,  8, 15, -3,  1,
  -14,  2,-11, -2, -5,  2, 14,  5,
   -9,-26, -9,-10, -2, -4,  3, -3,
  -27,-27,-16,-16, -1, 17, -2,  1,
  -13,-17,  7,  8, 29, 56, 47, 57,
  -24,-39, -5,  1,-16, 57, 28, 54,
  -28,  0, 29, 12, 59, 44, 43, 45},

{ -15, 36, 12,-54,  8,-28, 24, 14,
    1,  7, -8,-64,-43,-16,  9,  8,
  -14,-14,-22,-46,-44,-30,-15,-27,
  -49, -1,-27,-39,-46,-44,-33,-51,
  -17,-20,-12,-27,-30,-25,-14,-36,
   -9, 24,  2,-16,-20,  6, 22,-22,
   29, -1,-20, -7, -8, -4,-38,-29,
  -65, 23, 16,-15,-56,-34,  2, 13}
};

static const int pst_eg[7][64] = {
{0},

{  0,  0,  0,  0,  0,  0,  0,  0,
  13,  8,  8, 10, 13,  0,  2, -7,
   4,  7, -6,  1,  0, -5, -1, -8,
  13,  9,-3, -7, -7, -8,  3, -1,
  32, 24, 13,  5, -2,  4, 17, 17,
  94,100, 85, 67, 56, 53, 82, 84,
 178,173,158,134,147,132,165,187,
   0,  0,  0,  0,  0,  0,  0,  0},

{ -29,-51,-23,-15,-22,-18,-50,-64,
  -42,-20,-10, -5, -2,-20,-23,-44,
  -23, -3, -1, 15, 10, -3,-20,-22,
  -18, -6, 16, 25, 16, 17,  4,-18,
  -17,  3, 22, 22, 22, 11,  8,-18,
  -24,-20, 10,  9, -1, -9,-19,-41,
  -25, -8,-25, -2, -9,-25,-24,-52,
  -58,-38,-13,-28,-31,-27,-63,-99},

{ -23, -9,-23, -5, -9,-16, -5,-17,
  -14,-18, -7, -1,  4, -9,-15,-27,
  -12, -3,  8, 10, 13,  3, -7,-15,
   -6,  3, 13, 19,  7, 10, -3, -9,
   -3,  9, 12,  9, 14, 10,  3,  2,
    2, -8,  0, -1, -2,  6,  0,  4,
   -8, -4,  7,-12, -3,-13, -4,-14,
  -14,-21,-11, -8, -7, -9,-17,-24},

{  -9,  2,  3, -1, -5,-13,  4,-20,
   -6, -6,  0,  2, -9, -9,-11, -3,
   -4,  0, -5, -1, -7,-12, -8,-16,
    3,  5,  8,  4, -5, -6, -8,-11,
    4,  3, 13,  1,  2,  1, -1,  2,
    7,  7,  7,  5,  4, -3, -5, -3,
   11, 13, 13, 11, -3,  3,  8,  3,
   13, 10, 18, 15, 12, 12,  8,  5},

{ -33,-28,-22,-43, -5,-32,-20,-41,
  -22,-23,-30,-16,-16,-23,-36,-32,
  -16,-27, 15,  6,  9, 17, 10,  5,
  -18, 28, 19, 47, 31, 34, 39, 23,
    3, 22, 24, 45, 57, 40, 57, 36,
  -20,  6,  9, 49, 47, 35, 19,  9,
  -17, 20, 32, 41, 58, 25, 30,  0,
   -9, 22, 22, 27, 27, 19, 10, 20},

{ -53,-34,-21,-11,-28,-14,-24,-43,
  -27,-11,  4, 13, 14,  4, -5,-17,
  -19, -3, 11, 21, 23, 16,  7, -9,
  -18, -4, 21, 24, 27, 23,  9,-11,
   -8, 22, 24, 27, 26, 33, 26,  3,
   10, 17, 23, 15, 20, 45, 44, 13,
  -12, 17, 14, 17, 17, 38, 23, 11,
  -74,-35,-18,-18,-11, 15,  4,-17}
};

static int g_aggression = 100, g_king_attack = 100;
static int g_sacrifice = 100, g_positional = 100, g_risk = 100;

static void init_tables(void) {
    if (g_tables_init) return;

    for (int f = 0; f < 8; f++) FileBB[f] = FILE_A << f;
    for (int r = 0; r < 8; r++) RankBB[r] = RANK_1 << (r * 8);
    for (int f = 0; f < 8; f++) {
        AdjacentFiles[f] = 0;
        if (f > 0) AdjacentFiles[f] |= FileBB[f-1];
        if (f < 7) AdjacentFiles[f] |= FileBB[f+1];
    }

    static const int kn_dr[8] = {-2,-2,-1,-1, 1, 1, 2, 2};
    static const int kn_df[8] = {-1, 1,-2, 2,-2, 2,-1, 1};
    for (int sq = 0; sq < 64; sq++) {
        KnightAttacks[sq] = 0;
        int r = SQ_RANK(sq), f = SQ_FILE(sq);
        for (int i = 0; i < 8; i++) {
            int nr = r + kn_dr[i], nf = f + kn_df[i];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                KnightAttacks[sq] |= (Bitboard)1 << SQ_MAKE(nr, nf);
        }
    }

    static const int kg_dr[8] = {-1,-1,-1, 0, 0, 1, 1, 1};
    static const int kg_df[8] = {-1, 0, 1,-1, 1,-1, 0, 1};
    for (int sq = 0; sq < 64; sq++) {
        KingAttacks[sq] = 0;
        int r = SQ_RANK(sq), f = SQ_FILE(sq);
        for (int i = 0; i < 8; i++) {
            int nr = r + kg_dr[i], nf = f + kg_df[i];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                KingAttacks[sq] |= (Bitboard)1 << SQ_MAKE(nr, nf);
        }
    }

    for (int sq = 0; sq < 64; sq++) {
        PawnAttacks[WHITE][sq] = 0;
        PawnAttacks[BLACK][sq] = 0;
        int r = SQ_RANK(sq), f = SQ_FILE(sq);

        if (r < 7) {
            if (f > 0) PawnAttacks[WHITE][sq] |= (Bitboard)1 << SQ_MAKE(r+1, f-1);
            if (f < 7) PawnAttacks[WHITE][sq] |= (Bitboard)1 << SQ_MAKE(r+1, f+1);
        }

        if (r > 0) {
            if (f > 0) PawnAttacks[BLACK][sq] |= (Bitboard)1 << SQ_MAKE(r-1, f-1);
            if (f < 7) PawnAttacks[BLACK][sq] |= (Bitboard)1 << SQ_MAKE(r-1, f+1);
        }
    }

    memset(BetweenBB, 0, sizeof(BetweenBB));
    memset(LineBB, 0, sizeof(LineBB));
    init_magic_bitboards();

    for (int s1 = 0; s1 < 64; s1++) {
        for (int s2 = 0; s2 < 64; s2++) {
            if (s1 == s2) continue;

            Bitboard bb_s2 = (Bitboard)1 << s2;
            Bitboard rattk = rook_attacks(s1, 0);
            Bitboard battk = bishop_attacks(s1, 0);
            if (rattk & bb_s2) {

                LineBB[s1][s2] = (rook_attacks(s1, 0) & rook_attacks(s2, 0)) |
                                 ((Bitboard)1 << s1) | ((Bitboard)1 << s2);
                BetweenBB[s1][s2] = rook_attacks(s1, (Bitboard)1 << s2) &
                                    rook_attacks(s2, (Bitboard)1 << s1);
            } else if (battk & bb_s2) {

                LineBB[s1][s2] = (bishop_attacks(s1, 0) & bishop_attacks(s2, 0)) |
                                 ((Bitboard)1 << s1) | ((Bitboard)1 << s2);
                BetweenBB[s1][s2] = bishop_attacks(s1, (Bitboard)1 << s2) &
                                    bishop_attacks(s2, (Bitboard)1 << s1);
            }
        }
    }

    uint64_t s = 0x123456789ABCDEFULL;
    #define ZOB_NEXT (s = s*6364136223846793005ULL + 1442695040888963407ULL, s)
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 7; p++)
            for (int sq = 0; sq < 64; sq++)
                zobPiece[c][p][sq] = ZOB_NEXT;
    zobSide = ZOB_NEXT;
    for (int i = 0; i < 16; i++) zobCastle[i] = ZOB_NEXT;
    for (int f = 0; f < 8; f++) zobEp[f] = ZOB_NEXT;
    #undef ZOB_NEXT

    g_tables_init = 1;
}

static inline Bitboard pieces_of(const Position *p, int color) {
    return p->byColor[color];
}
static inline Bitboard pieces_type(const Position *p, int pt) {
    return p->byType[pt];
}
static inline Bitboard pieces_ct(const Position *p, int color, int pt) {
    return p->byColor[color] & p->byType[pt];
}
static inline Bitboard all_pieces(const Position *p) {
    return p->byType[0];
}

static uint64_t compute_hash(const Position *p) {
    uint64_t h = 0;
    for (int sq = 0; sq < 64; sq++) {
        if (p->board[sq]) {
            h ^= zobPiece[(int)p->pieceColor[sq]][(int)p->board[sq]][sq];
        }
    }
    if (p->side == BLACK) h ^= zobSide;
    h ^= zobCastle[p->castling & 15];
    if (p->ep >= 0) h ^= zobEp[SQ_FILE(p->ep)];
    return h;
}

static uint64_t compute_pawn_hash(const Position *p) {
    uint64_t h = 0;
    Bitboard pawns = p->byType[PAWN];
    while (pawns) {
        int sq = pop_lsb(&pawns);
        h ^= zobPiece[(int)p->pieceColor[sq]][PAWN][sq];
    }
    return h;
}

static void pos_put_piece(Position *p, int sq, int color, int pt) {
    Bitboard bb = (Bitboard)1 << sq;
    p->board[sq] = (int8_t)pt;
    p->pieceColor[sq] = (int8_t)color;
    p->byType[0] |= bb;
    p->byType[pt] |= bb;
    p->byColor[color] |= bb;
    if (pt == KING) p->kingSquare[color] = sq;
    if (pt != KING && pt != PAWN) p->npm[color] += PieceValue[pt];
}

static void pos_remove_piece(Position *p, int sq) {
    Bitboard bb = (Bitboard)1 << sq;
    int pt = p->board[sq];
    int color = p->pieceColor[sq];
    p->board[sq] = 0;
    p->pieceColor[sq] = -1;
    p->byType[0] &= ~bb;
    p->byType[pt] &= ~bb;
    p->byColor[color] &= ~bb;
    if (pt != KING && pt != PAWN) p->npm[color] -= PieceValue[pt];
}

static void pos_move_piece(Position *p, int from, int to) {
    Bitboard fromTo = ((Bitboard)1 << from) | ((Bitboard)1 << to);
    int pt = p->board[from];
    int color = p->pieceColor[from];
    p->board[to] = (int8_t)pt;
    p->board[from] = 0;
    p->pieceColor[to] = (int8_t)color;
    p->pieceColor[from] = -1;
    p->byType[0] ^= fromTo;
    p->byType[pt] ^= fromTo;
    p->byColor[color] ^= fromTo;
    if (pt == KING) p->kingSquare[color] = to;
}

static void pos_from_matlab(Position *p, double *bd, int mside,
                            double *cd, double *ed) {
    memset(p, 0, sizeof(Position));
    memset(p->pieceColor, -1, 64);
    p->ep = -1;
    p->npm[0] = p->npm[1] = 0;

    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            int sq = r * 8 + f;
            double val = bd[f * 8 + r];
            int iv = (int)val;
            if (iv == 0) continue;
            int color = (iv > 0) ? WHITE : BLACK;
            int pt = (iv > 0) ? iv : -iv;
            if (pt < 1 || pt > 6) continue;
            pos_put_piece(p, sq, color, pt);
        }
    }

    p->side = (mside > 0) ? WHITE : BLACK;

    p->castling = 0;
    if ((int)cd[0]) p->castling |= CR_WK;
    if ((int)cd[1]) p->castling |= CR_WQ;
    if ((int)cd[2]) p->castling |= CR_BK;
    if ((int)cd[3]) p->castling |= CR_BQ;

    int epr = (int)ed[0], epc = (int)ed[1];
    if (epr > 0 && epc > 0)
        p->ep = (epr - 1) * 8 + (epc - 1);
    else
        p->ep = -1;

    p->key = compute_hash(p);
    p->pawnKey = compute_pawn_hash(p);
    p->rule50 = 0;
    p->ply = 0;
}

static Bitboard attackers_to(const Position *p, int sq, Bitboard occ) {
    return (PawnAttacks[BLACK][sq] & pieces_ct(p, WHITE, PAWN))
         | (PawnAttacks[WHITE][sq] & pieces_ct(p, BLACK, PAWN))
         | (KnightAttacks[sq] & p->byType[KNIGHT])
         | (bishop_attacks(sq, occ) & (p->byType[BISHOP] | p->byType[QUEEN]))
         | (rook_attacks(sq, occ) & (p->byType[ROOK] | p->byType[QUEEN]))
         | (KingAttacks[sq] & p->byType[KING]);
}

static inline int is_attacked(const Position *p, int sq, int by_color) {
    return (attackers_to(p, sq, all_pieces(p)) & p->byColor[by_color]) != 0;
}

static Bitboard compute_checkers(const Position *p) {
    int ksq = p->kingSquare[p->side];
    return attackers_to(p, ksq, all_pieces(p)) & p->byColor[p->side ^ 1];
}

static inline int in_check(const Position *p) {
    return p->checkers != 0;
}

static Bitboard compute_pinned(const Position *p, int color) {
    int ksq = p->kingSquare[color];
    int opp = color ^ 1;
    Bitboard pinned = 0;
    Bitboard occ = all_pieces(p);

    Bitboard rq = (p->byType[ROOK] | p->byType[QUEEN]) & p->byColor[opp] & rook_attacks(ksq, 0);
    while (rq) {
        int sq = pop_lsb(&rq);
        Bitboard between = BetweenBB[ksq][sq];
        Bitboard blockers = between & occ;
        if (popcount(blockers) == 1 && (blockers & p->byColor[color]))
            pinned |= blockers;
    }

    Bitboard bq = (p->byType[BISHOP] | p->byType[QUEEN]) & p->byColor[opp] & bishop_attacks(ksq, 0);
    while (bq) {
        int sq = pop_lsb(&bq);
        Bitboard between = BetweenBB[ksq][sq];
        Bitboard blockers = between & occ;
        if (popcount(blockers) == 1 && (blockers & p->byColor[color]))
            pinned |= blockers;
    }
    return pinned;
}

static inline int gives_check(const Position *p, uint16_t move) {
    int us = p->side, them = us ^ 1;
    int from = MOVE_FROM(move), to = MOVE_TO(move);
    int oppKsq = p->kingSquare[them];
    int pt = p->board[from];
    if (MOVE_TYPE(move) == MT_PROMOTION) pt = MOVE_PROMO(move) + KNIGHT;

    Bitboard occ = (all_pieces(p) ^ ((Bitboard)1 << from)) | ((Bitboard)1 << to);
    if (pt == PAWN) {
        if (PawnAttacks[us][to] & ((Bitboard)1 << oppKsq)) return 1;
    } else if (pt == KNIGHT) {
        if (KnightAttacks[to] & ((Bitboard)1 << oppKsq)) return 1;
    } else if (pt == BISHOP) {
        if (bishop_attacks(to, occ) & ((Bitboard)1 << oppKsq)) return 1;
    } else if (pt == ROOK) {
        if (rook_attacks(to, occ) & ((Bitboard)1 << oppKsq)) return 1;
    } else if (pt == QUEEN) {
        if (queen_attacks(to, occ) & ((Bitboard)1 << oppKsq)) return 1;
    }

    Bitboard sliders = (p->byType[BISHOP] | p->byType[ROOK] | p->byType[QUEEN]) & p->byColor[us];
    if (sliders & rook_attacks(oppKsq, 0)) {
        if (LineBB[oppKsq][from] & rook_attacks(oppKsq, occ) & (p->byType[ROOK] | p->byType[QUEEN]) & p->byColor[us])
            return 1;
    }
    if (sliders & bishop_attacks(oppKsq, 0)) {
        if (LineBB[oppKsq][from] & bishop_attacks(oppKsq, occ) & (p->byType[BISHOP] | p->byType[QUEEN]) & p->byColor[us])
            return 1;
    }

    return 0;
}

static int generate_moves(const Position *p, uint16_t *moves, int capturesOnly) {
    int n = 0;
    int us = p->side, them = us ^ 1;
    Bitboard occ = all_pieces(p);
    Bitboard target = capturesOnly ? p->byColor[them] : ~p->byColor[us];
    Bitboard friendlyOrEmpty = capturesOnly ? p->byColor[them] : ~p->byColor[us];

    Bitboard pawns = pieces_ct(p, us, PAWN);
    Bitboard promo_rank = (us == WHITE) ? RANK_8 : RANK_1;
    Bitboard push_rank = (us == WHITE) ? RANK_3 : RANK_6;
    int push_dir = (us == WHITE) ? 8 : -8;
    Bitboard empty = ~occ;

    if (!capturesOnly) {

        Bitboard single = (us == WHITE) ? ((pawns << 8) & empty)
                                        : ((pawns >> 8) & empty);
        Bitboard promos = single & promo_rank;
        single &= ~promo_rank;
        while (single) {
            int to = pop_lsb(&single);
            moves[n++] = MAKE_MOVE(to - push_dir, to, MT_NORMAL, 0);
        }
        while (promos) {
            int to = pop_lsb(&promos);
            int from = to - push_dir;
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 3);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 2);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 1);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 0);
        }

        Bitboard single_push = (us == WHITE) ? ((pawns << 8) & empty)
                                             : ((pawns >> 8) & empty);
        Bitboard dbl = (us == WHITE) ? ((single_push << 8) & empty & RANK_4)
                                     : ((single_push >> 8) & empty & RANK_5);
        while (dbl) {
            int to = pop_lsb(&dbl);
            moves[n++] = MAKE_MOVE(to - 2*push_dir, to, MT_NORMAL, 0);
        }
    }

    {
        Bitboard captureTargets = p->byColor[them];
        Bitboard left_atk, right_atk;
        if (us == WHITE) {
            left_atk  = ((pawns & ~FILE_A) << 7) & captureTargets;
            right_atk = ((pawns & ~FILE_H) << 9) & captureTargets;
        } else {
            left_atk  = ((pawns & ~FILE_H) >> 7) & captureTargets;
            right_atk = ((pawns & ~FILE_A) >> 9) & captureTargets;
        }

        Bitboard lnp = left_atk & ~promo_rank;
        while (lnp) {
            int to = pop_lsb(&lnp);
            int from = (us == WHITE) ? (to - 7) : (to + 7);
            moves[n++] = MAKE_MOVE(from, to, MT_NORMAL, 0);
        }
        Bitboard rnp = right_atk & ~promo_rank;
        while (rnp) {
            int to = pop_lsb(&rnp);
            int from = (us == WHITE) ? (to - 9) : (to + 9);
            moves[n++] = MAKE_MOVE(from, to, MT_NORMAL, 0);
        }

        Bitboard lp = left_atk & promo_rank;
        while (lp) {
            int to = pop_lsb(&lp);
            int from = (us == WHITE) ? (to - 7) : (to + 7);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 3);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 2);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 1);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 0);
        }
        Bitboard rp = right_atk & promo_rank;
        while (rp) {
            int to = pop_lsb(&rp);
            int from = (us == WHITE) ? (to - 9) : (to + 9);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 3);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 2);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 1);
            moves[n++] = MAKE_MOVE(from, to, MT_PROMOTION, 0);
        }

        if (p->ep >= 0) {
            Bitboard epBB = (Bitboard)1 << p->ep;
            Bitboard epPawns = PawnAttacks[them][p->ep] & pawns;
            while (epPawns) {
                int from = pop_lsb(&epPawns);
                moves[n++] = MAKE_MOVE(from, p->ep, MT_EP, 0);
            }
        }
    }

    {
        Bitboard knights = pieces_ct(p, us, KNIGHT);
        while (knights) {
            int from = pop_lsb(&knights);
            Bitboard atk = KnightAttacks[from] & friendlyOrEmpty;
            while (atk) {
                int to = pop_lsb(&atk);
                moves[n++] = MAKE_MOVE(from, to, MT_NORMAL, 0);
            }
        }
    }

    {
        Bitboard bishops = pieces_ct(p, us, BISHOP);
        while (bishops) {
            int from = pop_lsb(&bishops);
            Bitboard atk = bishop_attacks(from, occ) & friendlyOrEmpty;
            while (atk) {
                int to = pop_lsb(&atk);
                moves[n++] = MAKE_MOVE(from, to, MT_NORMAL, 0);
            }
        }
    }

    {
        Bitboard rooks = pieces_ct(p, us, ROOK);
        while (rooks) {
            int from = pop_lsb(&rooks);
            Bitboard atk = rook_attacks(from, occ) & friendlyOrEmpty;
            while (atk) {
                int to = pop_lsb(&atk);
                moves[n++] = MAKE_MOVE(from, to, MT_NORMAL, 0);
            }
        }
    }

    {
        Bitboard queens = pieces_ct(p, us, QUEEN);
        while (queens) {
            int from = pop_lsb(&queens);
            Bitboard atk = queen_attacks(from, occ) & friendlyOrEmpty;
            while (atk) {
                int to = pop_lsb(&atk);
                moves[n++] = MAKE_MOVE(from, to, MT_NORMAL, 0);
            }
        }
    }

    if (!capturesOnly || 1) {
        int ksq = p->kingSquare[us];
        Bitboard atk = KingAttacks[ksq] & friendlyOrEmpty;
        while (atk) {
            int to = pop_lsb(&atk);
            moves[n++] = MAKE_MOVE(ksq, to, MT_NORMAL, 0);
        }

        if (!capturesOnly && !in_check(p)) {
            if (us == WHITE) {

                if ((p->castling & CR_WK) && p->board[7] == ROOK &&
                    p->pieceColor[7] == WHITE &&
                    !(occ & ((Bitboard)1 << 5)) && !(occ & ((Bitboard)1 << 6))) {
                    if (!is_attacked(p, 5, BLACK) && !is_attacked(p, 6, BLACK))
                        moves[n++] = MAKE_MOVE(4, 6, MT_CASTLING, 0);
                }

                if ((p->castling & CR_WQ) && p->board[0] == ROOK &&
                    p->pieceColor[0] == WHITE &&
                    !(occ & ((Bitboard)1 << 1)) && !(occ & ((Bitboard)1 << 2)) &&
                    !(occ & ((Bitboard)1 << 3))) {
                    if (!is_attacked(p, 3, BLACK) && !is_attacked(p, 2, BLACK))
                        moves[n++] = MAKE_MOVE(4, 2, MT_CASTLING, 0);
                }
            } else {

                if ((p->castling & CR_BK) && p->board[63] == ROOK &&
                    p->pieceColor[63] == BLACK &&
                    !(occ & ((Bitboard)1 << 61)) && !(occ & ((Bitboard)1 << 62))) {
                    if (!is_attacked(p, 61, WHITE) && !is_attacked(p, 62, WHITE))
                        moves[n++] = MAKE_MOVE(60, 62, MT_CASTLING, 0);
                }

                if ((p->castling & CR_BQ) && p->board[56] == ROOK &&
                    p->pieceColor[56] == BLACK &&
                    !(occ & ((Bitboard)1 << 57)) && !(occ & ((Bitboard)1 << 58)) &&
                    !(occ & ((Bitboard)1 << 59))) {
                    if (!is_attacked(p, 59, WHITE) && !is_attacked(p, 58, WHITE))
                        moves[n++] = MAKE_MOVE(60, 58, MT_CASTLING, 0);
                }
            }
        }
    }

    return n;
}

static int is_legal_move(const Position *p, uint16_t move) {
    int us = p->side, them = us ^ 1;
    int from = MOVE_FROM(move), to = MOVE_TO(move);
    int ksq = p->kingSquare[us];
    Bitboard occ = all_pieces(p);

    if (from == ksq) {
        if (MOVE_TYPE(move) == MT_CASTLING) {
            return 1;
        }
        Bitboard occWithoutKing = (occ & ~((Bitboard)1 << from));
        if (p->board[to]) occWithoutKing &= ~((Bitboard)1 << to);
        return !(attackers_to(p, to, occWithoutKing) & p->byColor[them]);
    }

    if (p->checkers) {

        if (more_than_one(p->checkers)) return 0;

        int checkerSq = lsb(p->checkers);
        Bitboard targetMask = p->checkers | BetweenBB[ksq][checkerSq];

        if (MOVE_TYPE(move) == MT_EP) {
            int capsq = (us == WHITE) ? (to - 8) : (to + 8);
            if (!(((Bitboard)1 << capsq) & p->checkers) && !(((Bitboard)1 << to) & targetMask))
                return 0;
            Bitboard newOcc = occ ^ ((Bitboard)1 << from) ^ ((Bitboard)1 << capsq) ^ ((Bitboard)1 << to);
            Bitboard rattk = rook_attacks(ksq, newOcc) & (p->byType[ROOK] | p->byType[QUEEN]) & p->byColor[them];
            Bitboard battk = bishop_attacks(ksq, newOcc) & (p->byType[BISHOP] | p->byType[QUEEN]) & p->byColor[them];
            return !(rattk | battk);
        }

        if (!(((Bitboard)1 << to) & targetMask)) return 0;
    }

    if (MOVE_TYPE(move) == MT_EP) {
        int capsq = (us == WHITE) ? (to - 8) : (to + 8);
        Bitboard newOcc = occ ^ ((Bitboard)1 << from) ^ ((Bitboard)1 << capsq) ^ ((Bitboard)1 << to);
        Bitboard rattk = rook_attacks(ksq, newOcc) & (p->byType[ROOK] | p->byType[QUEEN]) & p->byColor[them];
        Bitboard battk = bishop_attacks(ksq, newOcc) & (p->byType[BISHOP] | p->byType[QUEEN]) & p->byColor[them];
        return !(rattk | battk);
    }

    Bitboard pinned = compute_pinned(p, us);
    if (pinned & ((Bitboard)1 << from)) {
        return (LineBB[ksq][from] & ((Bitboard)1 << to)) != 0;
    }

    return 1;
}

static int generate_legal_moves(const Position *p, uint16_t *moves) {
    uint16_t pseudo[MAX_MOVES];
    int np = generate_moves(p, pseudo, 0);
    int n = 0;
    for (int i = 0; i < np; i++) {
        if (is_legal_move(p, pseudo[i]))
            moves[n++] = pseudo[i];
    }
    return n;
}

static const int CastleMask[64] = {
     ~CR_WQ, ~0, ~0, ~0,  ~(CR_WK|CR_WQ), ~0, ~0,  ~CR_WK,
    ~0,~0,~0,~0,~0,~0,~0,~0,
    ~0,~0,~0,~0,~0,~0,~0,~0,
    ~0,~0,~0,~0,~0,~0,~0,~0,
    ~0,~0,~0,~0,~0,~0,~0,~0,
    ~0,~0,~0,~0,~0,~0,~0,~0,
    ~0,~0,~0,~0,~0,~0,~0,~0,
     ~CR_BQ, ~0, ~0, ~0,  ~(CR_BK|CR_BQ), ~0, ~0,  ~CR_BK
};

static void do_move(Position *p, uint16_t move, UndoInfo *u) {

    u->key = p->key;
    u->pawnKey = p->pawnKey;
    u->castling = p->castling;
    u->ep = p->ep;
    u->rule50 = p->rule50;
    u->checkers = p->checkers;
    u->npm[0] = p->npm[0];
    u->npm[1] = p->npm[1];
    u->captured = 0;
    u->capturedColor = -1;

    int us = p->side, them = us ^ 1;
    int from = MOVE_FROM(move), to = MOVE_TO(move);
    int pt = p->board[from];
    int mtype = MOVE_TYPE(move);

    p->rule50++;

    if (p->ep >= 0) p->key ^= zobEp[SQ_FILE(p->ep)];
    p->ep = -1;

    if (mtype == MT_CASTLING) {

        int rookFrom, rookTo;
        if (to > from) {
            rookFrom = (us == WHITE) ? 7 : 63;
            rookTo   = (us == WHITE) ? 5 : 61;
        } else {
            rookFrom = (us == WHITE) ? 0 : 56;
            rookTo   = (us == WHITE) ? 3 : 59;
        }
        p->key ^= zobPiece[us][KING][from] ^ zobPiece[us][KING][to];
        p->key ^= zobPiece[us][ROOK][rookFrom] ^ zobPiece[us][ROOK][rookTo];
        pos_move_piece(p, from, to);
        pos_move_piece(p, rookFrom, rookTo);
        p->rule50 = 0;
    } else if (mtype == MT_EP) {
        int capsq = (us == WHITE) ? (to - 8) : (to + 8);
        u->captured = PAWN;
        u->capturedColor = them;

        p->key ^= zobPiece[them][PAWN][capsq];
        p->pawnKey ^= zobPiece[them][PAWN][capsq];
        pos_remove_piece(p, capsq);

        p->key ^= zobPiece[us][PAWN][from] ^ zobPiece[us][PAWN][to];
        p->pawnKey ^= zobPiece[us][PAWN][from] ^ zobPiece[us][PAWN][to];
        pos_move_piece(p, from, to);
        p->rule50 = 0;
    } else {

        if (p->board[to]) {
            u->captured = p->board[to];
            u->capturedColor = p->pieceColor[to];
            p->key ^= zobPiece[them][(int)u->captured][to];
            if (u->captured == PAWN) p->pawnKey ^= zobPiece[them][PAWN][to];
            pos_remove_piece(p, to);
            p->rule50 = 0;
        }

        p->key ^= zobPiece[us][pt][from] ^ zobPiece[us][pt][to];
        if (pt == PAWN) {
            p->pawnKey ^= zobPiece[us][PAWN][from] ^ zobPiece[us][PAWN][to];
            p->rule50 = 0;

            if (abs(to - from) == 16) {
                int epSq = (from + to) / 2;

                if (PawnAttacks[us][epSq] & pieces_ct(p, them, PAWN)) {
                    p->ep = epSq;
                    p->key ^= zobEp[SQ_FILE(epSq)];
                }
            }
        }

        if (mtype == MT_PROMOTION) {
            int promoPt = MOVE_PROMO(move) + KNIGHT;
            pos_move_piece(p, from, to);

            Bitboard toBB = (Bitboard)1 << to;
            p->byType[PAWN] &= ~toBB;
            p->byType[promoPt] |= toBB;
            p->board[to] = (int8_t)promoPt;
            p->npm[us] += PieceValue[promoPt];

            p->key ^= zobPiece[us][PAWN][to] ^ zobPiece[us][promoPt][to];
            p->pawnKey ^= zobPiece[us][PAWN][to];
        } else {
            pos_move_piece(p, from, to);
        }
    }

    int newCastling = p->castling & CastleMask[from] & CastleMask[to];
    if (newCastling != p->castling) {
        p->key ^= zobCastle[p->castling] ^ zobCastle[newCastling];
        p->castling = newCastling;
    }

    p->side ^= 1;
    p->key ^= zobSide;
    p->ply++;

    p->checkers = compute_checkers(p);
}

static void undo_move(Position *p, uint16_t move, const UndoInfo *u) {
    p->side ^= 1;
    p->ply--;
    int us = p->side, them = us ^ 1;
    int from = MOVE_FROM(move), to = MOVE_TO(move);
    int mtype = MOVE_TYPE(move);

    if (mtype == MT_CASTLING) {
        int rookFrom, rookTo;
        if (to > from) {
            rookFrom = (us == WHITE) ? 7 : 63;
            rookTo   = (us == WHITE) ? 5 : 61;
        } else {
            rookFrom = (us == WHITE) ? 0 : 56;
            rookTo   = (us == WHITE) ? 3 : 59;
        }
        pos_move_piece(p, to, from);
        pos_move_piece(p, rookTo, rookFrom);
    } else if (mtype == MT_EP) {
        int capsq = (us == WHITE) ? (to - 8) : (to + 8);
        pos_move_piece(p, to, from);
        pos_put_piece(p, capsq, them, PAWN);
    } else {
        if (mtype == MT_PROMOTION) {
            int promoPt = MOVE_PROMO(move) + KNIGHT;

            pos_remove_piece(p, to);
            pos_put_piece(p, from, us, PAWN);
        } else {
            pos_move_piece(p, to, from);
        }

        if (u->captured) {
            pos_put_piece(p, to, u->capturedColor, u->captured);
        }
    }

    p->key = u->key;
    p->pawnKey = u->pawnKey;
    p->castling = u->castling;
    p->ep = u->ep;
    p->rule50 = u->rule50;
    p->checkers = u->checkers;
    p->npm[0] = u->npm[0];
    p->npm[1] = u->npm[1];
}

static void do_null_move(Position *p, UndoInfo *u) {
    u->key = p->key;
    u->pawnKey = p->pawnKey;
    u->ep = p->ep;
    u->rule50 = p->rule50;
    u->checkers = p->checkers;
    u->castling = p->castling;
    u->npm[0] = p->npm[0]; u->npm[1] = p->npm[1];
    u->captured = 0;

    if (p->ep >= 0) p->key ^= zobEp[SQ_FILE(p->ep)];
    p->ep = -1;
    p->side ^= 1;
    p->key ^= zobSide;
    p->ply++;
    p->rule50++;
    p->checkers = 0;
}

static void undo_null_move(Position *p, const UndoInfo *u) {
    p->side ^= 1;
    p->ply--;
    p->key = u->key;
    p->pawnKey = u->pawnKey;
    p->ep = u->ep;
    p->rule50 = u->rule50;
    p->checkers = u->checkers;
}

static int see_ge(const Position *p, uint16_t move, int threshold) {
    int from = MOVE_FROM(move), to = MOVE_TO(move);
    int mtype = MOVE_TYPE(move);

    if (mtype == MT_CASTLING) return 0 >= threshold;
    if (mtype == MT_EP) {

        return 0 >= threshold;
    }

    int target = p->board[to];
    int swap = (target ? PieceValue[target] : 0) - threshold;
    if (swap < 0) return 0;

    int attacker = p->board[from];
    if (mtype == MT_PROMOTION) attacker = MOVE_PROMO(move) + KNIGHT;
    swap = PieceValue[attacker] - swap;
    if (swap <= 0) return 1;

    Bitboard occ = all_pieces(p) ^ ((Bitboard)1 << from) ^ ((Bitboard)1 << to);
    if (mtype == MT_EP) {
        int capsq = (p->side == WHITE) ? (to - 8) : (to + 8);
        occ ^= (Bitboard)1 << capsq;
    }

    Bitboard attackers = attackers_to(p, to, occ) & occ;
    int stm = p->side ^ 1;
    int result = 1;

    Bitboard stmAttackers;
    while (1) {
        stmAttackers = attackers & p->byColor[stm];
        if (!stmAttackers) break;

        int nextPt = 0;
        for (int pt = PAWN; pt <= KING; pt++) {
            if (stmAttackers & p->byType[pt]) {
                nextPt = pt;
                break;
            }
        }

        Bitboard bit = stmAttackers & p->byType[nextPt];
        int sq = lsb(bit);
        occ ^= ((Bitboard)1 << sq);

        if (nextPt == PAWN || nextPt == BISHOP || nextPt == QUEEN) {
            attackers |= bishop_attacks(to, occ) & (p->byType[BISHOP] | p->byType[QUEEN]);
        }
        if (nextPt == ROOK || nextPt == QUEEN) {
            attackers |= rook_attacks(to, occ) & (p->byType[ROOK] | p->byType[QUEEN]);
        }
        attackers &= occ;

        stm ^= 1;
        swap = PieceValue[nextPt] - swap;

        if (swap <= 0) {
            result ^= 1;
            break;
        }
        result ^= 1;

        if (nextPt == KING) {

            if (attackers & p->byColor[stm]) result ^= 1;
            break;
        }
    }

    return result;
}

#define PHASE_KNIGHT 1
#define PHASE_BISHOP 1
#define PHASE_ROOK   2
#define PHASE_QUEEN  4
#define PHASE_TOTAL  24

static int evaluate(const Position *p) {

    if (!p->byType[PAWN] && !p->byType[ROOK] && !p->byType[QUEEN]) {
        int knights = popcount(p->byType[KNIGHT]);
        int bishops = popcount(p->byType[BISHOP]);
        if (knights + bishops <= 1) return 0;
    }

    int mg[2] = {0, 0}, eg[2] = {0, 0};
    int phase = 0;

    for (int sq = 0; sq < 64; sq++) {
        int pt = p->board[sq];
        if (!pt) continue;
        int color = p->pieceColor[sq];
        int idx = (color == WHITE) ? sq : SQ_FLIP(sq);

        mg[color] += PieceValue[pt] + pst_mg[pt][idx];
        eg[color] += PieceValue[pt] + pst_eg[pt][idx];

        switch (pt) {
            case KNIGHT: case BISHOP: phase += PHASE_KNIGHT; break;
            case ROOK: phase += PHASE_ROOK; break;
            case QUEEN: phase += PHASE_QUEEN; break;
        }
    }
    if (phase > PHASE_TOTAL) phase = PHASE_TOTAL;

    for (int color = 0; color < 2; color++) {
        Bitboard ourPawns = pieces_ct(p, color, PAWN);
        Bitboard theirPawns = pieces_ct(p, color ^ 1, PAWN);

        Bitboard tmp = ourPawns;
        while (tmp) {
            int sq = pop_lsb(&tmp);
            int f = SQ_FILE(sq), r = SQ_RANK(sq);
            int relR = (color == WHITE) ? r : (7 - r);

            if (ourPawns & FileBB[f] & ~((Bitboard)1 << sq)) {
                mg[color] -= 10;
                eg[color] -= 20;
            }

            if (!(ourPawns & AdjacentFiles[f])) {
                mg[color] -= 15;
                eg[color] -= 20;
            }

            {
                Bitboard ahead;
                if (color == WHITE) {
                    ahead = 0;
                    for (int rr = r + 1; rr < 8; rr++)
                        ahead |= RankBB[rr];
                } else {
                    ahead = 0;
                    for (int rr = 0; rr < r; rr++)
                        ahead |= RankBB[rr];
                }
                Bitboard blockFiles = FileBB[f] | AdjacentFiles[f];
                if (!(theirPawns & ahead & blockFiles)) {

                    static const int ppBonus_mg[8] = {0, 5, 10, 20, 40, 65,100, 0};
                    static const int ppBonus_eg[8] = {0,15, 30, 50, 80,120,180, 0};
                    mg[color] += ppBonus_mg[relR];
                    eg[color] += ppBonus_eg[relR];
                }
            }
        }
    }

    for (int color = 0; color < 2; color++) {
        if (popcount(pieces_ct(p, color, BISHOP)) >= 2) {
            mg[color] += 30;
            eg[color] += 50;
        }
    }

    for (int color = 0; color < 2; color++) {
        Bitboard rooks = pieces_ct(p, color, ROOK);
        while (rooks) {
            int sq = pop_lsb(&rooks);
            int f = SQ_FILE(sq);
            if (!(pieces_ct(p, color, PAWN) & FileBB[f])) {
                if (!(pieces_ct(p, color ^ 1, PAWN) & FileBB[f])) {
                    mg[color] += 20;
                    eg[color] += 15;
                } else {
                    mg[color] += 10;
                    eg[color] += 8;
                }
            }
        }
    }

    for (int color = 0; color < 2; color++) {
        int ksq = p->kingSquare[color];
        int kf = SQ_FILE(ksq), kr = SQ_RANK(ksq);
        Bitboard ourPawns = pieces_ct(p, color, PAWN);

        int shield = 0;

        if (color == WHITE && kr > 0) {
            shield -= kr * 30;
        } else if (color == BLACK && kr < 7) {
            shield -= (7 - kr) * 30;
        }

        for (int df = -1; df <= 1; df++) {
            int nf = kf + df;
            if (nf < 0 || nf > 7) continue;
            int shieldRank  = (color == WHITE) ? 1 : 6;
            int shieldRank2 = (color == WHITE) ? 2 : 5;
            if (ourPawns & ((Bitboard)1 << SQ_MAKE(shieldRank, nf)))
                shield += 12;
            else if (ourPawns & ((Bitboard)1 << SQ_MAKE(shieldRank2, nf)))
                shield += 6;
            else
                shield -= 12;
        }
        mg[color] += shield;
    }

    for (int color = 0; color < 2; color++) {
        Bitboard occ = all_pieces(p);
        Bitboard enemyPawnAtk = 0;
        Bitboard ePawns = pieces_ct(p, color ^ 1, PAWN);
        while (ePawns) {
            int sq = pop_lsb(&ePawns);
            enemyPawnAtk |= PawnAttacks[color ^ 1][sq];
        }
        Bitboard safe = ~(p->byColor[color] | enemyPawnAtk);

        Bitboard knights = pieces_ct(p, color, KNIGHT);
        while (knights) {
            int sq = pop_lsb(&knights);
            int mob = popcount(KnightAttacks[sq] & safe);
            mg[color] += (mob - 4) * 4;
            eg[color] += (mob - 4) * 4;
        }
        Bitboard bishops = pieces_ct(p, color, BISHOP);
        while (bishops) {
            int sq = pop_lsb(&bishops);
            int mob = popcount(bishop_attacks(sq, occ) & safe);
            mg[color] += (mob - 7) * 5;
            eg[color] += (mob - 7) * 5;
        }
        Bitboard rooks = pieces_ct(p, color, ROOK);
        while (rooks) {
            int sq = pop_lsb(&rooks);
            int mob = popcount(rook_attacks(sq, occ) & safe);
            mg[color] += (mob - 7) * 3;
            eg[color] += (mob - 7) * 3;
        }
        Bitboard queens = pieces_ct(p, color, QUEEN);
        while (queens) {
            int sq = pop_lsb(&queens);
            int mob = popcount(queen_attacks(sq, occ) & safe);
            mg[color] += (mob - 14) * 2;
            eg[color] += (mob - 14) * 1;
        }
    }

    int mgScore = mg[WHITE] - mg[BLACK];
    int egScore = eg[WHITE] - eg[BLACK];
    int mgPhase = phase;
    if (mgPhase > PHASE_TOTAL) mgPhase = PHASE_TOTAL;
    int egPhase = PHASE_TOTAL - mgPhase;
    int score = (mgScore * mgPhase + egScore * egPhase) / PHASE_TOTAL;

    if (g_aggression != 100 || g_king_attack != 100 || g_positional != 100) {

        for (int color = 0; color < 2; color++) {
            int enemyKsq = p->kingSquare[color ^ 1];
            int ekr = SQ_RANK(enemyKsq), ekf = SQ_FILE(enemyKsq);
            int attack_bonus = 0;
            Bitboard pieces = p->byColor[color] & ~p->byType[PAWN] & ~p->byType[KING];
            while (pieces) {
                int sq = pop_lsb(&pieces);
                int dist = abs(SQ_RANK(sq) - ekr) + abs(SQ_FILE(sq) - ekf);
                if (dist <= 5) {
                    int pt = p->board[sq];
                    int w = (pt == QUEEN) ? 15 : (pt == ROOK) ? 10 : 7;
                    attack_bonus += w * (6 - dist);
                }
            }
            int side_mult = (color == WHITE) ? 1 : -1;
            score += side_mult * attack_bonus * (g_king_attack - 100) / 100;
        }
    }

    return (p->side == WHITE) ? score : -score;
}

static long long perft(Position *p, int depth) {
    if (depth == 0) return 1;
    uint16_t moves[MAX_MOVES];
    int n = generate_legal_moves(p, moves);
    if (depth == 1) return n;
    long long nodes = 0;
    for (int i = 0; i < n; i++) {
        UndoInfo u;
        do_move(p, moves[i], &u);
        nodes += perft(p, depth - 1);
        undo_move(p, moves[i], &u);
    }
    return nodes;
}

static int16_t mainHistory[2][64][64];
static int16_t captureHistory[16][64][7];
static uint16_t counterMoves[16][64];
static int16_t contHist1[16][64][16][64];
static int16_t contHist2[16][64][16][64];
#define PAWN_HIST_ENTRIES 512
static int16_t pawnHistory[PAWN_HIST_ENTRIES][16][64];

static int16_t pawnCorrHist[2][4096];
static int16_t minorCorrHist[2][4096];
static int16_t nonPawnCorrHist[2][4096];
static int16_t contCorrHist[2][16][64];

#define HIST_GRAVITY 7183
#define HIST_CAP_GRAVITY 10000

static void update_history(int16_t *entry, int bonus) {
    int clamped = bonus;
    int D = HIST_GRAVITY;
    if (clamped > D) clamped = D;
    if (clamped < -D) clamped = -D;
    *entry += (int16_t)(clamped - (*entry) * abs(clamped) / D);
}

static inline void update_corr_entry(int16_t *entry, int diff, int weight) {
    int new_val = *entry + (diff * weight) / 1024;
    if (new_val > 1024) new_val = 1024;
    if (new_val < -1024) new_val = -1024;
    *entry = (int16_t)new_val;
}

static int get_corrected_eval(const Position *p, const SStack *ss, int staticEval) {
    if (staticEval == VALUE_NONE || in_check(p)) return staticEval;

    int us = p->side;

    int pawnWeight     = 15341;
    int minorWeight    = 10569;
    int nonPawnWeight  =  9842;
    int contWeight     =  8192;

    int pawnIdx    = (int)(p->pawnKey & 4095);
    int minorIdx   = (int)((p->key ^ p->pawnKey) & 4095);
    int nonPawnIdx = (int)((p->key ^ (p->pawnKey >> 16)) & 4095);

    int correction = (pawnWeight    * pawnCorrHist[us][pawnIdx]
                    + minorWeight   * minorCorrHist[us][minorIdx]
                    + nonPawnWeight * nonPawnCorrHist[us][nonPawnIdx]) / 65536;

    if (ss && ss->ply > 0 && (ss - 1)->currentMove) {
        int pp = (ss - 1)->movedPiece;
        int pt_to = (ss - 1)->movedTo;
        if (pp >= 0 && pp < 16 && pt_to >= 0 && pt_to < 64)
            correction += (contWeight * contCorrHist[us][pp][pt_to]) / 65536;
    }

    int corrected = staticEval + correction / 32;
    if (corrected > MATE_TH) corrected = MATE_TH;
    if (corrected < -MATE_TH) corrected = -MATE_TH;
    return corrected;
}

static void update_correction_history(const Position *p, const SStack *ss, int depth, int bestValue, int staticEval) {
    if (staticEval == VALUE_NONE || in_check(p)) return;
    if (abs(bestValue) >= MATE_TH) return;

    int us = p->side;
    int diff = bestValue - staticEval;
    int weight = depth * depth;
    if (weight > 128) weight = 128;

    int pawnIdx    = (int)(p->pawnKey & 4095);
    int minorIdx   = (int)((p->key ^ p->pawnKey) & 4095);
    int nonPawnIdx = (int)((p->key ^ (p->pawnKey >> 16)) & 4095);

    update_corr_entry(&pawnCorrHist[us][pawnIdx], diff, weight);
    update_corr_entry(&minorCorrHist[us][minorIdx], diff, weight);
    update_corr_entry(&nonPawnCorrHist[us][nonPawnIdx], diff, weight);

    if (ss && ss->ply > 0 && (ss - 1)->currentMove) {
        int pp = (ss - 1)->movedPiece;
        int pt_to = (ss - 1)->movedTo;
        if (pp >= 0 && pp < 16 && pt_to >= 0 && pt_to < 64)
            update_corr_entry(&contCorrHist[us][pp][pt_to], diff, weight);
    }
}

static void clear_history(void) {
    memset(mainHistory, 0, sizeof(mainHistory));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(counterMoves, 0, sizeof(counterMoves));
    memset(contHist1, 0, sizeof(contHist1));
    memset(contHist2, 0, sizeof(contHist2));
    memset(pawnHistory, 0, sizeof(pawnHistory));
    memset(pawnCorrHist, 0, sizeof(pawnCorrHist));
    memset(minorCorrHist, 0, sizeof(minorCorrHist));
    memset(nonPawnCorrHist, 0, sizeof(nonPawnCorrHist));
    memset(contCorrHist, 0, sizeof(contCorrHist));
}

static long long g_nodes;
static double g_time_limit;
static clock_t g_t_start;
static int g_abort;
static int g_max_depth_reached;

static uint16_t g_root_moves[MAX_MOVES];
static int      g_root_scores[MAX_MOVES];
static int      g_root_n;

static int LMR_table[64][64];

static void init_lmr(void) {
    for (int d = 0; d < 64; d++) {
        for (int m = 0; m < 64; m++) {
            if (d == 0 || m == 0) { LMR_table[d][m] = 0; continue; }
            LMR_table[d][m] = (int)(0.4 + log((double)d) * log((double)m) * 0.4);
            if (LMR_table[d][m] < 0) LMR_table[d][m] = 0;
        }
    }
}

static int time_check_counter = 0;
static int time_up(void) {
    if (g_abort) return 1;
    if (++time_check_counter & 1023) return 0;
    if ((double)(clock() - g_t_start) / CLOCKS_PER_SEC > g_time_limit) {
        g_abort = 1;
        return 1;
    }
    return 0;
}

static void score_moves(const Position *p, uint16_t *moves, int *scores, int n,
                        uint16_t tt_move, const SStack *ss) {
    int us = p->side;
    for (int i = 0; i < n; i++) {
        uint16_t m = moves[i];
        if (m == tt_move && tt_move != MOVE_NONE) {
            scores[i] = 2000000;
            continue;
        }
        int from = MOVE_FROM(m), to = MOVE_TO(m);
        int pt = p->board[from];
        int cap = p->board[to];
        int mtype = MOVE_TYPE(m);

        if (cap || mtype == MT_EP) {

            int capVal = (mtype == MT_EP) ? PieceValue[PAWN] : PieceValue[cap];
            int seeVal = see_ge(p, m, 0) ? 1 : 0;
            int capType = (mtype == MT_EP) ? PAWN : cap;
            int capHist = (pt >= 0 && pt < 16 && capType >= 0 && capType < 7) ? captureHistory[pt][to][capType] : 0;
            scores[i] = 1000000 + capVal * 10 - PieceValue[pt] + seeVal * 500000 + capHist;
        } else if (mtype == MT_PROMOTION) {
            scores[i] = 900000 + MOVE_PROMO(m) * 100;
        } else if (ss && ss->killers[0] == m) {
            scores[i] = 800000;
        } else if (ss && ss->killers[1] == m) {
            scores[i] = 700000;
        } else {

            int hScore = mainHistory[us][from][to];
            if (ss && ss->ply >= 1 && (ss - 1)->currentMove) {
                int pp = (ss - 1)->movedPiece, pto = (ss - 1)->movedTo;
                if (pp >= 0 && pp < 16 && pto >= 0 && pto < 64) {
                    hScore += 2 * contHist1[pp][pto][pt][to];
                    if (counterMoves[pp][pto] == m) hScore += 30000;
                }
            }
            if (ss && ss->ply >= 2 && (ss - 2)->currentMove) {
                int pp = (ss - 2)->movedPiece, pto = (ss - 2)->movedTo;
                if (pp >= 0 && pp < 16 && pto >= 0 && pto < 64)
                    hScore += contHist2[pp][pto][pt][to];
            }
            int pIdx = (int)(p->pawnKey & 511);
            hScore += pawnHistory[pIdx][pt][to];
            scores[i] = hScore;
        }
    }
}

static void pick_best(uint16_t *moves, int *scores, int n, int idx) {
    int best = idx;
    for (int j = idx + 1; j < n; j++) {
        if (scores[j] > scores[best]) best = j;
    }
    if (best != idx) {
        uint16_t tm = moves[idx]; moves[idx] = moves[best]; moves[best] = tm;
        int ts = scores[idx]; scores[idx] = scores[best]; scores[best] = ts;
    }
}

static int search(Position *p, SStack *ss, int depth, int alpha, int beta,
                  int cutNode, int isPV);
static int qsearch(Position *p, SStack *ss, int alpha, int beta);

static int qsearch(Position *p, SStack *ss, int alpha, int beta) {
    if (time_up()) return 0;
    g_nodes++;

    if (p->rule50 >= 100) return 0;
    if (!p->byType[PAWN] && !p->byType[ROOK] && !p->byType[QUEEN]) {
        if (popcount(p->byType[KNIGHT]) + popcount(p->byType[BISHOP]) <= 1) return 0;
    }

    int inChk = in_check(p);
    int bestValue;

    if (!inChk) {
        bestValue = evaluate(p);
        if (bestValue >= beta) return bestValue;
        if (bestValue > alpha) alpha = bestValue;
    } else {
        bestValue = -INF;
    }

    uint16_t moves[MAX_MOVES];
    int n;
    if (inChk) {
        n = generate_legal_moves(p, moves);
        if (n == 0) return -MATE_VAL + p->ply;
    } else {
        n = generate_moves(p, moves, 1);
    }

    int scores[MAX_MOVES];
    score_moves(p, moves, scores, n, MOVE_NONE, NULL);

    int moveCount = 0;
    for (int i = 0; i < n; i++) {
        pick_best(moves, scores, n, i);
        uint16_t m = moves[i];

        if (!inChk && !is_legal_move(p, m)) continue;

        int givesCheck = gives_check(p, m);

        if (!inChk && !givesCheck) {
            int to = MOVE_TO(m);
            int capVal = (MOVE_TYPE(m) == MT_EP) ? PieceValue[PAWN] : PieceValue[(int)p->board[to]];
            if (bestValue + capVal + 200 < alpha && MOVE_TYPE(m) != MT_PROMOTION)
                continue;

            if (!see_ge(p, m, -50)) continue;
        }

        moveCount++;
        UndoInfo u;
        do_move(p, m, &u);
        int score = -qsearch(p, ss + 1, -beta, -alpha);
        undo_move(p, m, &u);

        if (g_abort) return 0;

        if (score > bestValue) {
            bestValue = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) return score;
            }
        }
    }

    return bestValue;
}

static int search(Position *p, SStack *ss, int depth, int alpha, int beta,
                  int cutNode, int isPV) {
    if (time_up()) return 0;

    if (depth <= 0) return qsearch(p, ss, alpha, beta);

    g_nodes++;
    int rootNode = (ss->ply == 0);
    ss->key = p->key;

    if (!rootNode) {
        if (p->rule50 >= 100) return 0;

        if (ss && ss->ply >= 4) {
            int limit = (p->rule50 < ss->ply) ? p->rule50 : ss->ply;
            for (int i = 4; i <= limit; i += 2) {
                if ((ss - i)->key == p->key) return 0;
            }
        }

        if (!p->byType[PAWN] && !p->byType[ROOK] && !p->byType[QUEEN]) {
            if (popcount(p->byType[KNIGHT]) + popcount(p->byType[BISHOP]) <= 1) return 0;
        }
    }

    if (!rootNode) {
        int mateAlpha = -MATE_VAL + p->ply;
        int mateBeta  =  MATE_VAL - p->ply - 1;
        if (alpha < mateAlpha) alpha = mateAlpha;
        if (beta > mateBeta) beta = mateBeta;
        if (alpha >= beta) return alpha;
    }

    int inChk = in_check(p);

    if (inChk) depth++;

    int ttMove = MOVE_NONE, ttValue = VALUE_NONE, ttEval = VALUE_NONE;
    int ttDepth = -9, ttBound = TT_BOUND_NONE;
    int ttHit = tt_probe(p->key, depth, p->ply, &ttMove, &ttValue, &ttEval,
                         &ttDepth, &ttBound);

    if (!isPV && !rootNode && ttHit && ttDepth >= depth) {
        if (ttBound == TT_BOUND_EXACT) return ttValue;
        if (ttBound == TT_BOUND_LOWER && ttValue >= beta) return ttValue;
        if (ttBound == TT_BOUND_UPPER && ttValue <= alpha) return ttValue;
    }

    int staticEval;
    if (inChk) {
        staticEval = VALUE_NONE;
    } else if (ttHit && ttEval != VALUE_NONE) {
        staticEval = ttEval;

        if (ttBound == TT_BOUND_LOWER && ttValue > staticEval) staticEval = ttValue;
        if (ttBound == TT_BOUND_UPPER && ttValue < staticEval) staticEval = ttValue;
    } else {
        staticEval = evaluate(p);
    }

    staticEval = get_corrected_eval(p, ss, staticEval);
    ss->staticEval = staticEval;

    int improving = (ss->ply >= 2 && !inChk && staticEval > (ss - 2)->staticEval);

    if (!isPV && !inChk && !rootNode) {

        if (depth <= 3 && staticEval + 350 * depth < alpha) {
            int razorScore = qsearch(p, ss, alpha, beta);
            if (razorScore <= alpha) return razorScore;
        }

        if (depth < 9 && staticEval - 80 * depth + 60 * improving >= beta
            && staticEval < MATE_TH) {
            return staticEval;
        }

        if (depth >= 3 && staticEval >= beta && p->npm[p->side] > 0) {
            int R = 3 + depth / 4;
            if (staticEval - beta > 200) R++;
            if (R > depth - 1) R = depth - 1;

            UndoInfo u;
            do_null_move(p, &u);
            int nullScore = -search(p, ss + 1, depth - 1 - R, -beta, -beta + 1, !cutNode, 0);
            undo_null_move(p, &u);

            if (g_abort) return 0;
            if (nullScore >= beta) {

                if (depth >= 12) {
                    int verScore = search(p, ss, depth - R, beta - 1, beta, 0, 0);
                    if (verScore >= beta) return verScore;
                } else {
                    return nullScore > MATE_TH ? beta : nullScore;
                }
            }
        }

        if (depth >= 5 && abs(beta) < MATE_TH) {
            int probCutBeta = beta + 200 - 50 * improving;
            uint16_t pcMoves[MAX_MOVES];
            int pcN = generate_moves(p, pcMoves, 1);
            for (int i = 0; i < pcN; i++) {
                if (!is_legal_move(p, pcMoves[i])) continue;
                if (!see_ge(p, pcMoves[i], probCutBeta - staticEval)) continue;

                UndoInfo u;
                do_move(p, pcMoves[i], &u);
                int pcScore = -search(p, ss + 1, depth - 4, -probCutBeta, -probCutBeta + 1, !cutNode, 0);
                undo_move(p, pcMoves[i], &u);

                if (pcScore >= probCutBeta) return pcScore;
            }
        }
    }

    if (depth >= 4 && !ttMove && !rootNode) depth--;

    uint16_t moves[MAX_MOVES];
    int n = generate_moves(p, moves, 0);
    int scores[MAX_MOVES];
    score_moves(p, moves, scores, n, ttMove, ss);

    int bestValue = -INF;
    uint16_t bestMove = MOVE_NONE;
    int origAlpha = alpha;
    int moveCount = 0;

    uint16_t quietsSearched[64];
    int nQuietsSearched = 0;

    for (int i = 0; i < n; i++) {
        pick_best(moves, scores, n, i);
        uint16_t m = moves[i];
        int from = MOVE_FROM(m), to = MOVE_TO(m);
        int pt = p->board[from];
        int cap = p->board[to];
        int mtype = MOVE_TYPE(m);
        int isCapture = (cap != 0 || mtype == MT_EP);
        int isPromo = (mtype == MT_PROMOTION);

        if (!is_legal_move(p, m)) continue;
        moveCount++;

        int givesCheck = gives_check(p, m);

        if (!isPV && !inChk && !isCapture && !isPromo && !givesCheck && bestValue > -MATE_TH) {
            int lmpThreshold = (3 + depth * depth) / (2 - improving);
            if (moveCount > lmpThreshold && depth <= 8) continue;
        }

        if (!isPV && !inChk && !isCapture && !isPromo && !givesCheck && depth <= 6
            && moveCount > 1 && staticEval + 120 * depth < alpha) {
            continue;
        }

        if (!isPV && !inChk && !isCapture && !givesCheck && depth <= 8
            && moveCount > 1 && !see_ge(p, m, -20 * depth * depth)) {
            continue;
        }

        if (!isPV && isCapture && !givesCheck && depth <= 6
            && !see_ge(p, m, -80 * depth)) {
            continue;
        }

        ss->currentMove = m;
        ss->movedPiece = pt;
        ss->movedTo = to;

        UndoInfo u;
        do_move(p, m, &u);

        int newDepth = depth - 1;
        int score;

        if (moveCount == 1) {
            score = -search(p, ss + 1, newDepth, -beta, -alpha, !cutNode, isPV);
        } else {

            int R = 0;
            if (depth >= 3 && moveCount >= 3 && !isCapture && !isPromo && !givesCheck) {
                int d_idx = (depth < 64) ? depth : 63;
                int m_idx = (moveCount < 64) ? moveCount : 63;
                R = LMR_table[d_idx][m_idx];

                if (cutNode) R += 1;
                if (improving) R -= 1;

                int hist = mainHistory[p->side ^ 1][from][to];
                R -= hist / 3000;

                if (R < 0) R = 0;
                if (R >= newDepth) R = newDepth - 1;
            }

            score = -search(p, ss + 1, newDepth - R, -alpha - 1, -alpha, !cutNode, 0);

            if (R > 0 && score > alpha) {
                score = -search(p, ss + 1, newDepth, -alpha - 1, -alpha, !cutNode, 0);
            }

            if (isPV && score > alpha && score < beta) {
                score = -search(p, ss + 1, newDepth, -beta, -alpha, 0, 1);
            }
        }

        undo_move(p, m, &u);
        if (g_abort) return 0;

        if (rootNode) {
            g_root_moves[moveCount - 1] = m;
            g_root_scores[moveCount - 1] = score;
        }

        if (score > bestValue) {
            bestValue = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {

                    if (!isCapture) {

                        if (ss->killers[0] != m) {
                            ss->killers[1] = ss->killers[0];
                            ss->killers[0] = m;
                        }

                        int bonus = depth * depth;
                        update_history(&mainHistory[p->side][from][to], bonus);

                        int prevPt1 = -1, prevTo1 = -1;
                        if (ss->ply > 0 && (ss - 1)->currentMove) {
                            prevPt1 = (ss - 1)->movedPiece;
                            prevTo1 = (ss - 1)->movedTo;
                            if (prevPt1 >= 0 && prevPt1 < 16 && prevTo1 >= 0 && prevTo1 < 64) {
                                counterMoves[prevPt1][prevTo1] = m;
                                update_history(&contHist1[prevPt1][prevTo1][pt][to], bonus);
                            }
                        }
                        int prevPt2 = -1, prevTo2 = -1;
                        if (ss->ply > 1 && (ss - 2)->currentMove) {
                            prevPt2 = (ss - 2)->movedPiece;
                            prevTo2 = (ss - 2)->movedTo;
                            if (prevPt2 >= 0 && prevPt2 < 16 && prevTo2 >= 0 && prevTo2 < 64) {
                                update_history(&contHist2[prevPt2][prevTo2][pt][to], bonus);
                            }
                        }

                        int pIdx = (int)(p->pawnKey & (PAWN_HIST_ENTRIES - 1));
                        update_history(&pawnHistory[pIdx][pt][to], bonus);

                        int malus = -depth * depth;
                        for (int j = 0; j < nQuietsSearched; j++) {
                            int qf = MOVE_FROM(quietsSearched[j]);
                            int qt = MOVE_TO(quietsSearched[j]);
                            int qpt = p->board[qf];
                            update_history(&mainHistory[p->side][qf][qt], malus);
                            if (prevPt1 >= 0 && prevPt1 < 16 && prevTo1 >= 0 && prevTo1 < 64) {
                                update_history(&contHist1[prevPt1][prevTo1][qpt][qt], malus);
                            }
                            if (prevPt2 >= 0 && prevPt2 < 16 && prevTo2 >= 0 && prevTo2 < 64) {
                                update_history(&contHist2[prevPt2][prevTo2][qpt][qt], malus);
                            }
                            update_history(&pawnHistory[pIdx][qpt][qt], malus);
                        }
                    } else {

                        int bonus = depth * depth;
                        int capType = (mtype == MT_EP) ? PAWN : u.captured;
                        if (pt >= 0 && pt < 16 && to >= 0 && to < 64 && capType >= 1 && capType <= 6) {
                            update_history(&captureHistory[pt][to][capType], bonus);
                        }
                    }
                    break;
                }
            }
        }

        if (!isCapture && nQuietsSearched < 64) {
            quietsSearched[nQuietsSearched++] = m;
        }
    }

    if (moveCount == 0) {
        if (inChk) return -MATE_VAL + p->ply;
        return 0;
    }

    if (bestValue <= -MATE_TH && !inChk) {
        bestValue = (staticEval < alpha) ? staticEval : alpha;
    }

    if (rootNode) g_root_n = moveCount;

    if (!g_abort) {
        int bound;
        if (bestValue <= origAlpha) bound = TT_BOUND_UPPER;
        else if (bestValue >= beta) bound = TT_BOUND_LOWER;
        else bound = TT_BOUND_EXACT;
        tt_store(p->key, depth, p->ply, bestValue, staticEval, bestMove, bound);

        if (!inChk && staticEval != VALUE_NONE && abs(bestValue) < MATE_TH) {
            if ((bound == TT_BOUND_EXACT) ||
                (bound == TT_BOUND_LOWER && bestValue > staticEval) ||
                (bound == TT_BOUND_UPPER && bestValue < staticEval)) {
                update_correction_history(p, ss, depth, bestValue, staticEval);
            }
        }
    }

    return bestValue;
}

static int iterative_deepening(Position *p, int max_depth, double time_limit,
                               uint16_t *best_move) {
    g_nodes = 0;
    g_t_start = clock();
    g_time_limit = time_limit;
    g_abort = 0;
    g_max_depth_reached = 0;
    g_root_n = 0;
    time_check_counter = 0;
    *best_move = MOVE_NONE;

    init_lmr();
    tt_new_search();

    SStack stack[MAX_PLY + 10];
    memset(stack, 0, sizeof(stack));
    SStack *ss = stack + 4;
    for (int i = -4; i < MAX_PLY + 4; i++) {
        (ss + i)->ply = i;
        (ss + i)->staticEval = VALUE_NONE;
    }

    int prevScore = 0;
    uint16_t prevBest = MOVE_NONE;

    for (int d = 1; d <= max_depth; d++) {
        int alpha, beta;

        if (d >= 4) {
            int delta = 25 + abs(prevScore) / 100;
            alpha = prevScore - delta;
            beta  = prevScore + delta;
            if (alpha < -INF) alpha = -INF;
            if (beta > INF) beta = INF;
        } else {
            alpha = -INF;
            beta  = INF;
        }

        g_root_n = 0;

        int score = search(p, ss, d, alpha, beta, 0, 1);

        if (g_abort) break;

        if (score <= alpha || score >= beta) {

            score = search(p, ss, d, -INF, INF, 0, 1);
            if (g_abort) break;
        }

        prevScore = score;
        g_max_depth_reached = d;

        int ttMv = MOVE_NONE, ttVal = 0, ttEv = 0, ttD = 0, ttB = 0;
        if (tt_probe(p->key, d, 0, &ttMv, &ttVal, &ttEv, &ttD, &ttB) && ttMv != MOVE_NONE) {
            *best_move = ttMv;
            prevBest = *best_move;
        } else if (g_root_n > 0) {
            int bestIdx = 0;
            for (int i = 1; i < g_root_n; i++) {
                if (g_root_scores[i] > g_root_scores[bestIdx]) bestIdx = i;
            }
            *best_move = g_root_moves[bestIdx];
            prevBest = *best_move;
        }

        if (score > MATE_TH || score < -MATE_TH) break;

        if ((double)(clock() - g_t_start) / CLOCKS_PER_SEC > time_limit * 0.5) break;
    }

    if (*best_move == MOVE_NONE) {
        uint16_t legal[MAX_MOVES];
        int nl = generate_legal_moves(p, legal);
        if (nl > 0) *best_move = legal[0];
    }

    return prevScore;
}

#ifndef STANDALONE_PERFT
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    init_tables();
    if (!tt_table) tt_resize(64);

    if (nrhs >= 1 && mxGetNumberOfElements(prhs[0]) == 1) {
        double val = mxGetScalar(prhs[0]);
        if ((int)val == -999) {
            tt_clear();
            clear_history();
            plhs[0] = mxCreateDoubleMatrix(1, 4, mxREAL);
            if (nlhs >= 2) plhs[1] = mxCreateDoubleScalar(0);
            if (nlhs >= 3) plhs[2] = mxCreateDoubleScalar(0);
            if (nlhs >= 4) plhs[3] = mxCreateDoubleScalar(0);
            if (nlhs >= 5) plhs[4] = mxCreateDoubleMatrix(0, 4, mxREAL);
            if (nlhs >= 6) plhs[5] = mxCreateDoubleMatrix(0, 1, mxREAL);
            return;
        }
    }

    if (nrhs < 5) mexErrMsgTxt("Need: board, side, castling, ep, max_depth [, time_limit] [, style]");

    double *bd = mxGetPr(prhs[0]);
    int mside = (int)mxGetScalar(prhs[1]);
    double *cd = mxGetPr(prhs[2]);
    double *ed = mxGetPr(prhs[3]);
    int max_depth = (int)mxGetScalar(prhs[4]);
    double time_limit = (nrhs >= 6) ? mxGetScalar(prhs[5]) : 5.0;

    g_aggression = 100; g_king_attack = 100;
    g_sacrifice = 100; g_positional = 100; g_risk = 100;
    if (nrhs >= 7 && mxGetNumberOfElements(prhs[6]) >= 5) {
        double *st = mxGetPr(prhs[6]);
        g_aggression = (int)st[0]; g_king_attack = (int)st[1];
        g_sacrifice = (int)st[2]; g_positional = (int)st[3]; g_risk = (int)st[4];
    }

    Position pos;
    pos_from_matlab(&pos, bd, mside, cd, ed);
    pos.checkers = compute_checkers(&pos);

    uint16_t best_move;
    int score = iterative_deepening(&pos, max_depth, time_limit, &best_move);

    plhs[0] = mxCreateDoubleMatrix(1, 5, mxREAL);
    double *md = mxGetPr(plhs[0]);
    if (best_move != MOVE_NONE) {
        int from = MOVE_FROM(best_move), to = MOVE_TO(best_move);
        md[0] = SQ_RANK(from) + 1;
        md[1] = SQ_FILE(from) + 1;
        md[2] = SQ_RANK(to) + 1;
        md[3] = SQ_FILE(to) + 1;
        if (MOVE_TYPE(best_move) == MT_PROMOTION) {
            static const int promo_map[4] = {KNIGHT, BISHOP, ROOK, QUEEN};
            md[4] = promo_map[MOVE_PROMO(best_move)];
        } else {
            md[4] = 0;
        }
    } else {
        md[0] = md[1] = md[2] = md[3] = md[4] = 0;
    }

    if (nlhs >= 2) plhs[1] = mxCreateDoubleScalar((double)score);

    if (nlhs >= 3) plhs[2] = mxCreateDoubleScalar((double)g_max_depth_reached);

    if (nlhs >= 4) plhs[3] = mxCreateDoubleScalar((double)g_nodes);

    if (nlhs >= 5) {
        int N = g_root_n;
        plhs[4] = mxCreateDoubleMatrix(N, 4, mxREAL);
        double *rm = mxGetPr(plhs[4]);
        for (int i = 0; i < N; i++) {
            uint16_t m = g_root_moves[i];
            int from = MOVE_FROM(m), to = MOVE_TO(m);
            rm[i + 0 * N] = SQ_RANK(from) + 1;
            rm[i + 1 * N] = SQ_FILE(from) + 1;
            rm[i + 2 * N] = SQ_RANK(to) + 1;
            rm[i + 3 * N] = SQ_FILE(to) + 1;
        }
    }

    if (nlhs >= 6) {
        int N = g_root_n;
        plhs[5] = mxCreateDoubleMatrix(N, 1, mxREAL);
        double *rs = mxGetPr(plhs[5]);
        for (int i = 0; i < N; i++) rs[i] = (double)g_root_scores[i];
    }
}
#endif
