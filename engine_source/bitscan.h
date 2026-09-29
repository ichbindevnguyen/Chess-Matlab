#ifndef BITSCAN_H
#define BITSCAN_H

#include <stdint.h>

typedef uint64_t Bitboard;

#if defined(__GNUC__) || defined(__clang__)
    #define BS_GCC_CLANG 1
#elif defined(_MSC_VER)
    #define BS_MSVC 1
    #include <intrin.h>
#else
    #define BS_GENERIC 1
#endif

static inline int popcount(Bitboard b) {
#if defined(BS_GCC_CLANG)
    return __builtin_popcountll(b);
#elif defined(BS_MSVC)
    return (int)__popcnt64(b);
#else
    b = b - ((b >> 1) & 0x5555555555555555ULL);
    b = (b & 0x3333333333333333ULL) + ((b >> 2) & 0x3333333333333333ULL);
    b = (b + (b >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    return (int)((b * 0x0101010101010101ULL) >> 56);
#endif
}

static inline int lsb(Bitboard b) {
#if defined(BS_GCC_CLANG)
    return __builtin_ctzll(b);
#elif defined(BS_MSVC)
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return (int)idx;
#else
    static const int index64[64] = {
         0, 47,  1, 56, 48, 27,  2, 60,
        57, 49, 41, 37, 28, 16,  3, 61,
        54, 58, 35, 52, 50, 42, 21, 44,
        38, 32, 29, 23, 17, 11,  4, 62,
        46, 55, 26, 59, 40, 36, 15, 53,
        34, 51, 20, 43, 31, 22, 10, 45,
        25, 39, 14, 33, 19, 30,  9, 24,
        13, 18,  8, 12,  7,  6,  5, 63
    };
    static const Bitboard debruijn64 = 0x03f79d71b4cb0a89ULL;
    return index64[((b & (Bitboard)(-(int64_t)b)) * debruijn64) >> 58];
#endif
}

static inline int msb(Bitboard b) {
#if defined(BS_GCC_CLANG)
    return 63 - __builtin_clzll(b);
#elif defined(BS_MSVC)
    unsigned long idx;
    _BitScanReverse64(&idx, b);
    return (int)idx;
#else
    static const int index64[64] = {
         0, 47,  1, 56, 48, 27,  2, 60,
        57, 49, 41, 37, 28, 16,  3, 61,
        54, 58, 35, 52, 50, 42, 21, 44,
        38, 32, 29, 23, 17, 11,  4, 62,
        46, 55, 26, 59, 40, 36, 15, 53,
        34, 51, 20, 43, 31, 22, 10, 45,
        25, 39, 14, 33, 19, 30,  9, 24,
        13, 18,  8, 12,  7,  6,  5, 63
    };
    static const Bitboard debruijn64 = 0x03f79d71b4cb0a89ULL;
    b |= b >> 1;  b |= b >> 2;  b |= b >> 4;
    b |= b >> 8;  b |= b >> 16; b |= b >> 32;
    return index64[(b * debruijn64) >> 58];
#endif
}

static inline int pop_lsb(Bitboard *b) {
    int s = lsb(*b);
    *b &= *b - 1;
    return s;
}

typedef enum {
    DIR_NORTH =  8, DIR_SOUTH =  -8,
    DIR_EAST  =  1, DIR_WEST  =  -1,
    DIR_NE    =  9, DIR_NW    =  7,
    DIR_SE    = -7, DIR_SW    = -9
} Direction;

#define BS_FILE_A 0x0101010101010101ULL
#define BS_FILE_H 0x8080808080808080ULL

static inline Bitboard shift(Bitboard b, Direction d) {
    switch (d) {
        case DIR_NORTH: return  b << 8;
        case DIR_SOUTH: return  b >> 8;
        case DIR_EAST:  return (b & ~BS_FILE_H) << 1;
        case DIR_WEST:  return (b & ~BS_FILE_A) >> 1;
        case DIR_NE:    return (b & ~BS_FILE_H) << 9;
        case DIR_NW:    return (b & ~BS_FILE_A) << 7;
        case DIR_SE:    return (b & ~BS_FILE_H) >> 7;
        case DIR_SW:    return (b & ~BS_FILE_A) >> 9;
        default:        return 0ULL;
    }
}

#endif
