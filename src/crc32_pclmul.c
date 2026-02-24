/*
 * libqbz2 - Hardware-accelerated CRC-32 using PCLMULQDQ
 *
 * Computes the same CRC-32 as the software slicing-by-8 implementation
 * (polynomial 0x04C11DB7, MSB-first convention), but uses Intel's
 * carry-less multiply instruction (PCLMULQDQ) for high throughput
 * on buffers >= 64 bytes.
 *
 * Algorithm: parallel fold-and-reduce with Barrett reduction, based on
 * "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ
 * Instruction" (Intel white paper).
 *
 * bzip2 uses MSB-first (non-reflected) CRC-32 with polynomial
 * P = 0x104C11DB7 (degree 32). This implementation works directly
 * in the MSB-first domain by byte-reversing each 16-byte block with
 * PSHUFB so that the first data byte maps to the highest polynomial
 * degree (bit 127 of the XMM register).
 *
 * The fold computes V mod P where V is the byte-reversed message
 * polynomial. The actual CRC is V * x^32 mod P, so after reducing
 * to 32 bits we post-multiply by x^32 mod P with one extra Barrett.
 */

#ifdef __PCLMUL__

#include <stdint.h>
#include <string.h>
#include <immintrin.h>
#include <wmmintrin.h>  /* PCLMULQDQ intrinsics */
#include <tmmintrin.h>  /* PSHUFB (SSSE3) */

/*
 * MSB-first CRC-32 fold constants for polynomial P = 0x104C11DB7.
 * k_N = x^N mod P, computed in GF(2).
 */

/* 4-way parallel fold (64 bytes/iter): x^576 and x^512 mod P */
#define K_FOLD4_HI  0x08833794CULL   /* x^576 mod P */
#define K_FOLD4_LO  0x0E6228B11ULL   /* x^512 mod P */

/* Sequential 128-bit fold: x^192 and x^128 mod P */
#define K_FOLD1_HI  0x0C5B9CD4CULL   /* x^192 mod P */
#define K_FOLD1_LO  0x0E8A45605ULL   /* x^128 mod P */

/* 128-bit to 64-bit reduction: x^64 mod P */
#define K_REDUCE_64  0x0490D678DULL   /* x^64 mod P */

/* Barrett reduction: mu = floor(x^64 / P), P with leading term */
#define K_BARRETT_MU  0x104D101DFULL
#define K_BARRETT_P   0x104C11DB7ULL

/* Post-multiply constant: x^32 mod P = 0x04C11DB7 */
#define K_X32_MOD_P   0x004C11DB7ULL


/* Byte-reverse mask for PSHUFB: reverses byte order in a 128-bit register */
static const uint8_t bswap_mask_data[16] __attribute__((aligned(16))) = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};


/*
 * Fold one 128-bit block: new = data ^ clmul_lo(old, k) ^ clmul_hi(old, k)
 */
static inline __m128i fold_128(__m128i data, __m128i old, __m128i k)
{
    return _mm_xor_si128(data, _mm_xor_si128(
        _mm_clmulepi64_si128(old, k, 0x00),
        _mm_clmulepi64_si128(old, k, 0x11)));
}


/*
 * Barrett reduction: reduce a 64-bit GF(2) polynomial mod P to 32 bits.
 *
 * Standard Barrett for GF(2):
 *   T = R >> 32
 *   Q = (T * mu) >> 32
 *   CRC = R[31:0] ^ (Q * P)[31:0]
 *
 * In SIMD: R is in the low 64 bits of an XMM register.
 */
static inline uint32_t barrett_reduce(__m128i x)
{
    __m128i mu_v = _mm_set_epi64x(0, (long long)K_BARRETT_MU);
    __m128i p_v  = _mm_set_epi64x(0, (long long)K_BARRETT_P);

    /* T = R >> 32: shift right by 4 bytes, low 32 bits = R[63:32] */
    __m128i t = _mm_srli_si128(x, 4);
    /* Mask to 32 bits to get just R[63:32] */
    t = _mm_and_si128(t, _mm_set_epi32(0, 0, 0, -1));

    /* Q = (T * mu) >> 32 */
    __m128i q = _mm_clmulepi64_si128(t, mu_v, 0x00);
    q = _mm_srli_si128(q, 4);

    /* T2 = Q * P */
    __m128i t2 = _mm_clmulepi64_si128(q, p_v, 0x00);

    /* CRC = R[31:0] ^ T2[31:0] */
    __m128i result = _mm_xor_si128(x, t2);
    return (uint32_t)_mm_extract_epi32(result, 0);
}


/*
 * Hardware CRC-32 using PCLMULQDQ.
 * Requires at least 64 bytes of input.
 *
 * Works in the MSB-first domain directly. Data blocks are byte-reversed
 * with PSHUFB so the first byte maps to the highest polynomial degree.
 * The CRC init is XORed into the high 32 bits (bits [127:96]).
 */
uint32_t crc32_pclmul(uint32_t crc_msb, const uint8_t *buf, uint32_t len)
{
    const uint8_t *p = buf;
    const uint8_t *end = buf + len;

    __m128i bswap = _mm_load_si128((const __m128i *)bswap_mask_data);

    __m128i k_fold4 = _mm_set_epi64x((long long)K_FOLD4_HI,
                                      (long long)K_FOLD4_LO);
    __m128i k_fold1 = _mm_set_epi64x((long long)K_FOLD1_HI,
                                      (long long)K_FOLD1_LO);

    /* Load initial 64 bytes, byte-reverse each 16-byte block */
    __m128i x0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p +  0)), bswap);
    __m128i x1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16)), bswap);
    __m128i x2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 32)), bswap);
    __m128i x3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 48)), bswap);
    p += 64;

    /* XOR the MSB-first CRC into the high 32 bits of x0.
     * After byte-reversal, byte 0 of data is at bits [127:120],
     * so the CRC init belongs in bits [127:96]. */
    {
        __m128i crc_xor = _mm_slli_si128(_mm_cvtsi32_si128((int)crc_msb), 12);
        x0 = _mm_xor_si128(x0, crc_xor);
    }

    /* Main loop: fold 64 bytes at a time using 4-way parallel fold */
    while (p + 64 <= end) {
        __m128i y0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p +  0)), bswap);
        __m128i y1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16)), bswap);
        __m128i y2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 32)), bswap);
        __m128i y3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 48)), bswap);
        p += 64;

        x0 = fold_128(y0, x0, k_fold4);
        x1 = fold_128(y1, x1, k_fold4);
        x2 = fold_128(y2, x2, k_fold4);
        x3 = fold_128(y3, x3, k_fold4);
    }

    /* Reduce 4 x 128-bit to 1 x 128-bit using sequential folds */
    x0 = fold_128(x1, x0, k_fold1);
    x0 = fold_128(x2, x0, k_fold1);
    x0 = fold_128(x3, x0, k_fold1);

    /* Process remaining 16-byte blocks */
    while (p + 16 <= end) {
        __m128i y = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)p), bswap);
        p += 16;
        x0 = fold_128(y, x0, k_fold1);
    }

    /*
     * Reduce x0 from 128 bits to 32-bit CRC.
     *
     * x0 = [V_hi : V_lo] (128 bits)
     *
     * Step 1: 128 -> 96 bits
     *   V_hi * (x^64 mod P) XOR V_lo -> 96-bit result in [95:0]
     */
    __m128i k_reduce = _mm_set_epi64x(0, (long long)K_REDUCE_64);
    {
        __m128i hi = _mm_srli_si128(x0, 8);        /* V_hi in low 64 bits */
        __m128i folded = _mm_clmulepi64_si128(hi, k_reduce, 0x00);
        /* folded has up to 95 bits. XOR with V_lo (low 64 of x0). */
        /* But folded can extend beyond 64 bits (up to 95). */
        /* x0[63:0] has V_lo. folded can have bits up to [94:0]. */
        /* Zero out hi part of x0 first, then XOR with folded. */
        __m128i mask_lo64 = _mm_set_epi64x(0, -1LL);
        x0 = _mm_xor_si128(_mm_and_si128(x0, mask_lo64), folded);
        /* x0 now has up to 95 bits in [95:0] */
    }

    /*
     * Step 2: 96 -> 64 bits
     *   x0 = [bits95_64 : bits63_0]
     *   bits95_64 * (x^64 mod P) XOR bits63_0 -> 64-bit result
     *
     *   bits[95:64] is at byte positions 8-11 in the XMM register.
     *   To extract: shift right by 8 bytes, mask to 32 bits.
     */
    {
        __m128i hi32 = _mm_srli_si128(x0, 8);      /* bits[127:64] -> low 64 */
        hi32 = _mm_and_si128(hi32, _mm_set_epi32(0, 0, 0, -1));  /* mask to 32 bits = bits[95:64] */
        __m128i folded = _mm_clmulepi64_si128(hi32, k_reduce, 0x00);
        /* XOR with bits[63:0] of x0 */
        __m128i lo64 = _mm_and_si128(x0, _mm_set_epi64x(0, -1LL));
        x0 = _mm_xor_si128(lo64, folded);
        /* x0 now has up to 63 bits in [63:0] */
    }

    /*
     * Step 3: Barrett reduction — 64-bit to 32-bit
     *   Gives V mod P (32 bits).
     */
    uint32_t crc_v_mod_p = barrett_reduce(x0);

    /*
     * Step 4: Post-multiply by x^32 mod P to get CRC = V * x^32 mod P.
     *   product = crc_v_mod_p * (x^32 mod P)  [up to 62 bits]
     *   Barrett reduce to 32 bits.
     */
    {
        __m128i a = _mm_cvtsi32_si128((int)crc_v_mod_p);
        __m128i b = _mm_set_epi64x(0, (long long)K_X32_MOD_P);
        __m128i product = _mm_clmulepi64_si128(a, b, 0x00);
        crc_msb = barrett_reduce(product);
    }

    /* Handle remaining tail bytes (< 16) using software byte-at-a-time. */
    {
        extern uint32_t BZ2_crc32Table[256];
        while (p < end) {
            crc_msb = (crc_msb << 8) ^
                      BZ2_crc32Table[(crc_msb >> 24) ^ *p++];
        }
    }

    return crc_msb;
}

#endif /* __PCLMUL__ */
