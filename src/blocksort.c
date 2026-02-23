/*
 * libqbz2 - Block sorting (Burrows-Wheeler Transform)
 *
 * Hybrid approach:
 * - mainSort (radix + quicksort) for normal non-repetitive blocks
 * - SA-IS (O(n) suffix array via induced sorting) for repetitive blocks
 *   when the quicksort budget is exceeded
 * - fallbackSort (exponential radix sort) for small blocks (< 10000)
 *
 * SA-IS reference: Nong, Zhang, Chan - "Two Efficient Algorithms for
 * Linear Time Suffix Array Construction" (2009)
 */

#include "qbz2_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------*/
/*--- Bitvector for S/L type classification ---*/
/*---------------------------------------------*/

/* Bit = 1 means S-type, bit = 0 means L-type */
#define BV_GET(bv,i)  (((bv)[(i)>>3] >> ((i)&7)) & 1)
#define BV_SET(bv,i)  ((bv)[(i)>>3] |=  (unsigned char)(1 << ((i)&7)))
#define BV_CLR(bv,i)  ((bv)[(i)>>3] &= (unsigned char)(~(1 << ((i)&7))))

/* LMS (Left-Most S-type): position i > 0 where type[i]=S and type[i-1]=L */
#define IS_LMS(bv,i)  ((i) > 0 && BV_GET(bv,i) && !BV_GET(bv,(i)-1))


/*---------------------------------------------*/
/*--- Bucket boundary computation            ---*/
/*---------------------------------------------*/

/*
 * Compute bucket start or end positions from character counts.
 * Sum starts at 1 because SA[0] is always the sentinel.
 * If end=1: B[c] = one past last position in bucket c (tail pointer)
 * If end=0: B[c] = first position in bucket c (head pointer)
 */
static void getBuckets ( const Int32 *C, Int32 *B, Int32 K, int end )
{
   Int32 i, sum = 1;
   if (end) {
      for (i = 0; i < K; i++) { sum += C[i]; B[i] = sum; }
   } else {
      for (i = 0; i < K; i++) { B[i] = sum; sum += C[i]; }
   }
}


/*---------------------------------------------*/
/*--- Character frequency counting           ---*/
/*---------------------------------------------*/

static void getCounts_byte ( const UChar *T, Int32 *C, Int32 n )
{
   Int32 i;
   for (i = 0; i < 256; i++) C[i] = 0;
   for (i = 0; i < n; i++) C[(Int32)T[i]]++;
}

static void getCounts_int ( const Int32 *T, Int32 *C, Int32 n, Int32 K )
{
   Int32 i;
   for (i = 0; i < K; i++) C[i] = 0;
   for (i = 0; i < n; i++) C[T[i]]++;
}


/*---------------------------------------------*/
/*--- Induce L-type suffixes (byte version)  ---*/
/*---------------------------------------------*/

static void induceL_byte ( const UChar *T, Int32 *SA, const Int32 *C,
                           Int32 *B, Int32 n, const UChar *bv )
{
   Int32 i, j;
   getBuckets(C, B, 256, 0);
   for (i = 0; i <= n; i++) {
      j = SA[i];
      if (j <= 0) continue;   /* skip empty (-1) and position 0 */
      j--;
      if (!BV_GET(bv, j)) {   /* j is L-type */
         SA[B[(Int32)T[j]]] = j;
         B[(Int32)T[j]]++;
      }
   }
}


/*---------------------------------------------*/
/*--- Induce S-type suffixes (byte version)  ---*/
/*---------------------------------------------*/

static void induceS_byte ( const UChar *T, Int32 *SA, const Int32 *C,
                           Int32 *B, Int32 n, const UChar *bv )
{
   Int32 i, j;
   getBuckets(C, B, 256, 1);
   for (i = n; i >= 0; i--) {
      j = SA[i];
      if (j <= 0) continue;
      j--;
      if (BV_GET(bv, j)) {    /* j is S-type */
         B[(Int32)T[j]]--;
         SA[B[(Int32)T[j]]] = j;
      }
   }
}


/*---------------------------------------------*/
/*--- Induce L-type suffixes (int version)   ---*/
/*---------------------------------------------*/

static void induceL_int ( const Int32 *T, Int32 *SA, const Int32 *C,
                          Int32 *B, Int32 n, Int32 K, const UChar *bv )
{
   Int32 i, j;
   getBuckets(C, B, K, 0);
   for (i = 0; i <= n; i++) {
      j = SA[i];
      if (j <= 0) continue;
      j--;
      if (!BV_GET(bv, j)) {
         SA[B[T[j]]] = j;
         B[T[j]]++;
      }
   }
}


/*---------------------------------------------*/
/*--- Induce S-type suffixes (int version)   ---*/
/*---------------------------------------------*/

static void induceS_int ( const Int32 *T, Int32 *SA, const Int32 *C,
                          Int32 *B, Int32 n, Int32 K, const UChar *bv )
{
   Int32 i, j;
   getBuckets(C, B, K, 1);
   for (i = n; i >= 0; i--) {
      j = SA[i];
      if (j <= 0) continue;
      j--;
      if (BV_GET(bv, j)) {
         B[T[j]]--;
         SA[B[T[j]]] = j;
      }
   }
}


/*---------------------------------------------*/
/*--- Forward declarations                   ---*/
/*---------------------------------------------*/

static void sais_byte ( const UChar *T, Int32 *SA, Int32 n );
static void sais_int  ( const Int32 *T, Int32 *SA, Int32 n, Int32 K );


/*---------------------------------------------*/
/*--- SA-IS for byte strings (first level)   ---*/
/*---------------------------------------------*/

static void sais_byte ( const UChar *T, Int32 *SA, Int32 n )
{
   Int32  i, j, n1, name;
   UChar  *bv;
   Int32  *C, *B;
   Int32  *s1;
   Int32  bvSize;

   /* Base cases */
   if (n <= 0) { SA[0] = 0; return; }
   if (n == 1) { SA[0] = 1; SA[1] = 0; return; }

   /* Allocate bitvector and bucket arrays */
   bvSize = (n + 1) / 8 + 2;
   bv = (UChar *)calloc((size_t)bvSize, 1);
   C  = (Int32 *)malloc(256 * sizeof(Int32));
   B  = (Int32 *)malloc(256 * sizeof(Int32));
   if (!bv || !C || !B) {
      free(bv); free(C); free(B);
      return;
   }

   /* ---- Step 1: Classify S/L types ---- */
   BV_SET(bv, n);      /* sentinel is S-type */
   BV_CLR(bv, n - 1);  /* T[n-1] > sentinel -> L-type */
   for (i = n - 2; i >= 0; i--) {
      if (T[i] < T[i + 1])      BV_SET(bv, i);
      else if (T[i] > T[i + 1]) BV_CLR(bv, i);
      else {
         if (BV_GET(bv, i + 1)) BV_SET(bv, i);
         else                   BV_CLR(bv, i);
      }
   }

   /* ---- Step 2: Get character frequencies ---- */
   getCounts_byte(T, C, n);

   /* ---- Step 3: Bucket sort LMS suffixes (approximate) ---- */
   for (i = 0; i <= n; i++) SA[i] = -1;
   SA[0] = n;  /* sentinel always first */

   getBuckets(C, B, 256, 1);  /* tails */
   for (i = n - 1; i >= 1; i--) {
      if (IS_LMS(bv, i)) {
         B[(Int32)T[i]]--;
         SA[B[(Int32)T[i]]] = i;
      }
   }

   /* ---- Step 4: Induce L-type ---- */
   induceL_byte(T, SA, C, B, n, bv);

   /* ---- Step 5: Induce S-type ---- */
   induceS_byte(T, SA, C, B, n, bv);

   /* ---- Step 6: Compact sorted LMS into SA[0..n1-1] ---- */
   n1 = 0;
   for (i = 0; i <= n; i++) {
      if (SA[i] >= 0 && IS_LMS(bv, SA[i]))
         SA[n1++] = SA[i];
   }
   /* n1 includes the sentinel (position n) which is the first LMS */

   /* ---- Step 7: Name LMS substrings ---- */
   for (i = n1; i <= n; i++) SA[i] = -1;

   name = 0;
   {
      Int32 prev = -1;
      for (i = 0; i < n1; i++) {
         Int32 pos = SA[i];
         Int32 diff = 0;
         if (prev < 0) {
            diff = 1;
         } else {
            Int32 d;
            for (d = 0; ; d++) {
               Int32 p1 = prev + d, p2 = pos + d;
               Int32 c1 = (p1 >= n) ? -1 : (Int32)T[p1];
               Int32 c2 = (p2 >= n) ? -1 : (Int32)T[p2];
               Int32 t1 = BV_GET(bv, p1);
               Int32 t2 = BV_GET(bv, p2);
               if (c1 != c2 || t1 != t2) { diff = 1; break; }
               if (d > 0) {
                  Int32 lms1 = IS_LMS(bv, p1);
                  Int32 lms2 = IS_LMS(bv, p2);
                  if (lms1 || lms2) {
                     if (!(lms1 && lms2)) diff = 1;
                     break;
                  }
               }
            }
         }
         if (diff) { name++; prev = pos; }
         /* Store name at SA[n1 + pos/2] */
         SA[n1 + (pos >> 1)] = name - 1;
      }
   }

   /* ---- Step 8: Collect reduced string ---- */
   /* s1 lives at the end of SA: SA[n+1-n1..n] */
   s1 = SA + n + 1 - n1;
   /* Scan BACKWARD to avoid overlap corruption */
   j = n1 - 1;
   for (i = n; i >= n1; i--) {
      if (SA[i] >= 0)
         s1[j--] = SA[i];
   }

   /* ---- Step 9: Recurse or direct inverse ---- */
   if (name < n1) {
      sais_int(s1, SA, n1 - 1, name);
   } else {
      for (i = 0; i < n1; i++)
         SA[s1[i]] = i;
   }

   /* ---- Step 10: Map back to original LMS positions ---- */
   {
      Int32 n1_lms;
      Int32 *buf;

      /* Build LMS position list (excluding sentinel) in s1 */
      j = 0;
      for (i = 1; i < n; i++) {
         if (IS_LMS(bv, i))
            s1[j++] = i;
      }
      n1_lms = j;  /* should be n1 - 1 */

      /* Save sorted LMS positions to temp buffer to avoid overlap issues.
       * Recursive SA[0] = sentinel of reduced problem.
       * SA[1..n1-1] are the non-sentinel entries giving sorted LMS order.
       * SA[i+1] indexes into s1[0..n1_lms-1].
       */
      buf = (Int32 *)malloc((size_t)n1_lms * sizeof(Int32));
      if (buf) {
         for (i = 0; i < n1_lms; i++)
            buf[i] = s1[SA[i + 1]];
      }

      /* ---- Step 11: Final induced sort ---- */
      for (i = 0; i <= n; i++) SA[i] = -1;
      SA[0] = n;  /* sentinel */

      getBuckets(C, B, 256, 1);  /* tails */
      if (buf) {
         for (i = n1_lms - 1; i >= 0; i--) {
            B[(Int32)T[buf[i]]]--;
            SA[B[(Int32)T[buf[i]]]] = buf[i];
         }
         free(buf);
      }

      induceL_byte(T, SA, C, B, n, bv);
      induceS_byte(T, SA, C, B, n, bv);
   }

   free(bv); free(C); free(B);
}


/*---------------------------------------------*/
/*--- SA-IS for integer strings (recursive)  ---*/
/*---------------------------------------------*/

static void sais_int ( const Int32 *T, Int32 *SA, Int32 n, Int32 K )
{
   Int32  i, j, n1, name;
   UChar  *bv;
   Int32  *C, *B;
   Int32  *s1;
   Int32  bvSize;

   /* Base cases */
   if (n <= 0) { SA[0] = 0; return; }
   if (n == 1) { SA[0] = 1; SA[1] = 0; return; }

   /* Allocate bitvector and bucket arrays */
   bvSize = (n + 1) / 8 + 2;
   bv = (UChar *)calloc((size_t)bvSize, 1);
   C  = (Int32 *)malloc((size_t)K * sizeof(Int32));
   B  = (Int32 *)malloc((size_t)K * sizeof(Int32));
   if (!bv || !C || !B) {
      free(bv); free(C); free(B);
      return;
   }

   /* ---- Step 1: Classify S/L types ---- */
   BV_SET(bv, n);
   BV_CLR(bv, n - 1);
   for (i = n - 2; i >= 0; i--) {
      if (T[i] < T[i + 1])      BV_SET(bv, i);
      else if (T[i] > T[i + 1]) BV_CLR(bv, i);
      else {
         if (BV_GET(bv, i + 1)) BV_SET(bv, i);
         else                   BV_CLR(bv, i);
      }
   }

   /* ---- Step 2: Get character frequencies ---- */
   getCounts_int(T, C, n, K);

   /* ---- Step 3: Bucket sort LMS suffixes ---- */
   for (i = 0; i <= n; i++) SA[i] = -1;
   SA[0] = n;

   getBuckets(C, B, K, 1);
   for (i = n - 1; i >= 1; i--) {
      if (IS_LMS(bv, i)) {
         B[T[i]]--;
         SA[B[T[i]]] = i;
      }
   }

   /* ---- Step 4: Induce L-type ---- */
   induceL_int(T, SA, C, B, n, K, bv);

   /* ---- Step 5: Induce S-type ---- */
   induceS_int(T, SA, C, B, n, K, bv);

   /* ---- Step 6: Compact sorted LMS ---- */
   n1 = 0;
   for (i = 0; i <= n; i++) {
      if (SA[i] >= 0 && IS_LMS(bv, SA[i]))
         SA[n1++] = SA[i];
   }

   /* ---- Step 7: Name LMS substrings ---- */
   for (i = n1; i <= n; i++) SA[i] = -1;

   name = 0;
   {
      Int32 prev = -1;
      for (i = 0; i < n1; i++) {
         Int32 pos = SA[i];
         Int32 diff = 0;
         if (prev < 0) {
            diff = 1;
         } else {
            Int32 d;
            for (d = 0; ; d++) {
               Int32 p1 = prev + d, p2 = pos + d;
               Int32 c1 = (p1 >= n) ? -1 : T[p1];
               Int32 c2 = (p2 >= n) ? -1 : T[p2];
               Int32 t1 = BV_GET(bv, p1);
               Int32 t2 = BV_GET(bv, p2);
               if (c1 != c2 || t1 != t2) { diff = 1; break; }
               if (d > 0) {
                  Int32 lms1 = IS_LMS(bv, p1);
                  Int32 lms2 = IS_LMS(bv, p2);
                  if (lms1 || lms2) {
                     if (!(lms1 && lms2)) diff = 1;
                     break;
                  }
               }
            }
         }
         if (diff) { name++; prev = pos; }
         SA[n1 + (pos >> 1)] = name - 1;
      }
   }

   /* ---- Step 8: Collect reduced string ---- */
   s1 = SA + n + 1 - n1;
   /* Scan BACKWARD to avoid overlap corruption */
   j = n1 - 1;
   for (i = n; i >= n1; i--) {
      if (SA[i] >= 0)
         s1[j--] = SA[i];
   }

   /* ---- Step 9: Recurse or direct inverse ---- */
   if (name < n1) {
      sais_int(s1, SA, n1 - 1, name);
   } else {
      for (i = 0; i < n1; i++)
         SA[s1[i]] = i;
   }

   /* ---- Step 10: Map back and final induced sort ---- */
   {
      Int32 n1_lms;
      Int32 *buf;

      j = 0;
      for (i = 1; i < n; i++) {
         if (IS_LMS(bv, i))
            s1[j++] = i;
      }
      n1_lms = j;

      buf = (Int32 *)malloc((size_t)n1_lms * sizeof(Int32));
      if (buf) {
         for (i = 0; i < n1_lms; i++)
            buf[i] = s1[SA[i + 1]];
      }

      /* ---- Step 11: Final induced sort ---- */
      for (i = 0; i <= n; i++) SA[i] = -1;
      SA[0] = n;

      getBuckets(C, B, K, 1);
      if (buf) {
         for (i = n1_lms - 1; i >= 0; i--) {
            B[T[buf[i]]]--;
            SA[B[T[buf[i]]]] = buf[i];
         }
         free(buf);
      }

      induceL_int(T, SA, C, B, n, K, bv);
      induceS_int(T, SA, C, B, n, K, bv);
   }

   free(bv); free(C); free(B);
}

/*---------------------------------------------*/
/*--- SA-IS based BWT for repetitive blocks  ---*/
/*---------------------------------------------*/

/*
 * Compute BWT using SA-IS on doubled string T+T.
 * This correctly handles circular rotations by computing
 * the suffix array of T+T and filtering entries < nblock.
 * Used as fallback when mainSort budget is exceeded.
 */
static void sais_bwt ( UChar* block, UInt32* ptr, Int32 nblock, Int32* origPtr )
{
   UChar  *doubled;
   Int32  *SA;
   Int32  dlen, i, j;

   dlen = 2 * nblock;
   doubled = (UChar *)malloc((size_t)dlen);
   SA = (Int32 *)malloc(((size_t)dlen + 1) * sizeof(Int32));
   if (!doubled || !SA) { free(doubled); free(SA); return; }

   memcpy(doubled, block, (size_t)nblock);
   memcpy(doubled + nblock, block, (size_t)nblock);
   sais_byte(doubled, SA, dlen);

   *origPtr = -1;
   j = 0;
   for (i = 1; i <= dlen; i++) {
      if (SA[i] < nblock) {
         ptr[j] = (UInt32)SA[i];
         if (SA[i] == 0) *origPtr = j;
         j++;
      }
   }
   free(doubled);
   free(SA);
}


/*---------------------------------------------*/
/*--- Fallback O(N log(N)^2) sorting        ---*/
/*--- algorithm, for repetitive blocks      ---*/
/*---------------------------------------------*/

static
__inline__
void fallbackSimpleSort ( UInt32* fmap,
                          UInt32* eclass,
                          Int32   lo,
                          Int32   hi )
{
   Int32 i, j, tmp;
   UInt32 ec_tmp;

   if (lo == hi) return;

   if (hi - lo > 3) {
      for ( i = hi-4; i >= lo; i-- ) {
         tmp = fmap[i];
         ec_tmp = eclass[tmp];
         for ( j = i+4; j <= hi && ec_tmp > eclass[fmap[j]]; j += 4 )
            fmap[j-4] = fmap[j];
         fmap[j-4] = tmp;
      }
   }

   for ( i = hi-1; i >= lo; i-- ) {
      tmp = fmap[i];
      ec_tmp = eclass[tmp];
      for ( j = i+1; j <= hi && ec_tmp > eclass[fmap[j]]; j++ )
         fmap[j-1] = fmap[j];
      fmap[j-1] = tmp;
   }
}


#define fswap(zz1, zz2) \
   { Int32 zztmp = zz1; zz1 = zz2; zz2 = zztmp; }

#define fvswap(zzp1, zzp2, zzn)       \
{                                     \
   Int32 yyp1 = (zzp1);               \
   Int32 yyp2 = (zzp2);               \
   Int32 yyn  = (zzn);                \
   while (yyn > 0) {                  \
      fswap(fmap[yyp1], fmap[yyp2]);  \
      yyp1++; yyp2++; yyn--;          \
   }                                  \
}

#define fmin(a,b) ((a) < (b)) ? (a) : (b)

#define fpush(lz,hz) { stackLo[sp] = lz; \
                       stackHi[sp] = hz; \
                       sp++; }

#define fpop(lz,hz) { sp--;              \
                      lz = stackLo[sp];  \
                      hz = stackHi[sp]; }

#define FALLBACK_QSORT_SMALL_THRESH 10
#define FALLBACK_QSORT_STACK_SIZE   100


static
void fallbackQSort3 ( UInt32* fmap,
                      UInt32* eclass,
                      Int32   loSt,
                      Int32   hiSt )
{
   Int32 unLo, unHi, ltLo, gtHi, n, m;
   Int32 sp, lo, hi;
   UInt32 med, r, r3;
   Int32 stackLo[FALLBACK_QSORT_STACK_SIZE];
   Int32 stackHi[FALLBACK_QSORT_STACK_SIZE];

   r = 0;

   sp = 0;
   fpush ( loSt, hiSt );

   while (sp > 0) {

      AssertH ( sp < FALLBACK_QSORT_STACK_SIZE - 1, 1004 );

      fpop ( lo, hi );
      if (hi - lo < FALLBACK_QSORT_SMALL_THRESH) {
         fallbackSimpleSort ( fmap, eclass, lo, hi );
         continue;
      }

      r = ((r * 7621) + 1) % 32768;
      r3 = r % 3;
      if (r3 == 0) med = eclass[fmap[lo]]; else
      if (r3 == 1) med = eclass[fmap[(lo+hi)>>1]]; else
                   med = eclass[fmap[hi]];

      unLo = ltLo = lo;
      unHi = gtHi = hi;

      while (1) {
         while (1) {
            if (unLo > unHi) break;
            n = (Int32)eclass[fmap[unLo]] - (Int32)med;
            if (n == 0) {
               fswap(fmap[unLo], fmap[ltLo]);
               ltLo++; unLo++;
               continue;
            };
            if (n > 0) break;
            unLo++;
         }
         while (1) {
            if (unLo > unHi) break;
            n = (Int32)eclass[fmap[unHi]] - (Int32)med;
            if (n == 0) {
               fswap(fmap[unHi], fmap[gtHi]);
               gtHi--; unHi--;
               continue;
            };
            if (n < 0) break;
            unHi--;
         }
         if (unLo > unHi) break;
         fswap(fmap[unLo], fmap[unHi]); unLo++; unHi--;
      }

      AssertD ( unHi == unLo-1, "fallbackQSort3(2)" );

      if (gtHi < ltLo) continue;

      n = fmin(ltLo-lo, unLo-ltLo); fvswap(lo, unLo-n, n);
      m = fmin(hi-gtHi, gtHi-unHi); fvswap(unLo, hi-m+1, m);

      n = lo + unLo - ltLo - 1;
      m = hi - (gtHi - unHi) + 1;

      if (n - lo > hi - m) {
         fpush ( lo, n );
         fpush ( m, hi );
      } else {
         fpush ( m, hi );
         fpush ( lo, n );
      }
   }
}

#undef fmin
#undef fpush
#undef fpop
#undef fswap
#undef fvswap
#undef FALLBACK_QSORT_SMALL_THRESH
#undef FALLBACK_QSORT_STACK_SIZE


/*---------------------------------------------*/
/* Fallback sort: exponential radix sort for    */
/* repetitive blocks.                          */
/*---------------------------------------------*/

#define       SET_BH(zz)  bhtab[(zz) >> 5] |= ((UInt32)1 << ((zz) & 31))
#define     CLEAR_BH(zz)  bhtab[(zz) >> 5] &= ~((UInt32)1 << ((zz) & 31))
#define     ISSET_BH(zz)  (bhtab[(zz) >> 5] & ((UInt32)1 << ((zz) & 31)))
#define      WORD_BH(zz)  bhtab[(zz) >> 5]
#define UNALIGNED_BH(zz)  ((zz) & 0x01f)

static
void fallbackSort ( UInt32* fmap,
                    UInt32* eclass,
                    UInt32* bhtab,
                    Int32   nblock,
                    Int32   verb )
{
   Int32 ftab[257];
   Int32 ftabCopy[256];
   Int32 H, i, j, k, l, r, cc, cc1;
   Int32 nNotDone;
   Int32 nBhtab;
   UChar* eclass8 = (UChar*)eclass;

   if (verb >= 4)
      VPrintf0 ( "        bucket sorting ...\n" );
   for (i = 0; i < 257;    i++) ftab[i] = 0;
   for (i = 0; i < nblock; i++) ftab[eclass8[i]]++;
   for (i = 0; i < 256;    i++) ftabCopy[i] = ftab[i];
   for (i = 1; i < 257;    i++) ftab[i] += ftab[i-1];

   for (i = 0; i < nblock; i++) {
      j = eclass8[i];
      k = ftab[j] - 1;
      ftab[j] = k;
      fmap[k] = i;
   }

   nBhtab = 2 + (nblock / 32);
   for (i = 0; i < nBhtab; i++) bhtab[i] = 0;
   for (i = 0; i < 256; i++) SET_BH(ftab[i]);

   for (i = 0; i < 32; i++) {
      SET_BH(nblock + 2*i);
      CLEAR_BH(nblock + 2*i + 1);
   }

   H = 1;
   while (1) {

      if (verb >= 4)
         VPrintf1 ( "        depth %6d has ", H );

      j = 0;
      for (i = 0; i < nblock; i++) {
         if (ISSET_BH(i)) j = i;
         k = fmap[i] - H; if (k < 0) k += nblock;
         eclass[k] = j;
      }

      nNotDone = 0;
      r = -1;
      while (1) {

         k = r + 1;
         while (ISSET_BH(k) && UNALIGNED_BH(k)) k++;
         if (ISSET_BH(k)) {
            while (WORD_BH(k) == 0xffffffff) k += 32;
            while (ISSET_BH(k)) k++;
         }
         l = k - 1;
         if (l >= nblock) break;
         while (!ISSET_BH(k) && UNALIGNED_BH(k)) k++;
         if (!ISSET_BH(k)) {
            while (WORD_BH(k) == 0x00000000) k += 32;
            while (!ISSET_BH(k)) k++;
         }
         r = k - 1;
         if (r >= nblock) break;

         if (r > l) {
            nNotDone += (r - l + 1);
            fallbackQSort3 ( fmap, eclass, l, r );

            cc = -1;
            for (i = l; i <= r; i++) {
               cc1 = eclass[fmap[i]];
               if (cc != cc1) { SET_BH(i); cc = cc1; };
            }
         }
      }

      if (verb >= 4)
         VPrintf1 ( "%6d unresolved strings\n", nNotDone );

      H *= 2;
      if (H > nblock || nNotDone == 0) break;
   }

   if (verb >= 4)
      VPrintf0 ( "        reconstructing block ...\n" );
   j = 0;
   for (i = 0; i < nblock; i++) {
      while (ftabCopy[j] == 0) j++;
      ftabCopy[j]--;
      eclass8[fmap[i]] = (UChar)j;
   }
   AssertH ( j < 256, 1005 );
}

#undef       SET_BH
#undef     CLEAR_BH
#undef     ISSET_BH
#undef      WORD_BH
#undef UNALIGNED_BH


/*---------------------------------------------*/
/*--- The main, O(N^2 log(N)) sorting       ---*/
/*--- algorithm.  Faster for "normal"       ---*/
/*--- non-repetitive blocks.                ---*/
/*---------------------------------------------*/

static
__inline__
Bool mainGtU ( UInt32  i1,
               UInt32  i2,
               UChar*  block,
               UInt16* quadrant,
               UInt32  nblock,
               Int32*  budget )
{
   Int32  k;
   UInt16 s1, s2;

   AssertD ( i1 != i2, "mainGtU" );

   /* Compare first 8 bytes as a single word */
   {
      uint64_t w1, w2;
      memcpy(&w1, &block[i1], 8);
      memcpy(&w2, &block[i2], 8);
      w1 = __builtin_bswap64(w1);
      w2 = __builtin_bswap64(w2);
      if (w1 != w2) return (w1 > w2);
   }
   i1 += 8; i2 += 8;

   /* Compare next 4 bytes as a single word */
   {
      uint32_t u1, u2;
      memcpy(&u1, &block[i1], 4);
      memcpy(&u2, &block[i2], 4);
      u1 = __builtin_bswap32(u1);
      u2 = __builtin_bswap32(u2);
      if (u1 != u2) return (u1 > u2);
   }
   i1 += 4; i2 += 4;

   k = nblock + 8;

   do {

      {
         uint64_t w1, w2;
         memcpy(&w1, &block[i1], 8);
         memcpy(&w2, &block[i2], 8);
         w1 = __builtin_bswap64(w1);
         w2 = __builtin_bswap64(w2);

         if (w1 != w2) {
            /* Find the first differing byte position */
            int pos = __builtin_clzll(w1 ^ w2) >> 3;
            /* Check quadrants for all positions before the differing byte */
            { int p; for (p = 0; p < pos; p++) {
               s1 = quadrant[i1+p]; s2 = quadrant[i2+p];
               if (s1 != s2) return (s1 > s2);
            }}
            return (w1 > w2);
         }

         /* All 8 bytes equal - check all 8 quadrants */
         s1 = quadrant[i1];   s2 = quadrant[i2];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+1]; s2 = quadrant[i2+1];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+2]; s2 = quadrant[i2+2];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+3]; s2 = quadrant[i2+3];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+4]; s2 = quadrant[i2+4];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+5]; s2 = quadrant[i2+5];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+6]; s2 = quadrant[i2+6];
         if (s1 != s2) return (s1 > s2);
         s1 = quadrant[i1+7]; s2 = quadrant[i2+7];
         if (s1 != s2) return (s1 > s2);
      }
      i1 += 8; i2 += 8;

      if (i1 >= nblock) i1 -= nblock;
      if (i2 >= nblock) i2 -= nblock;

      k -= 8;
      (*budget)--;
   }
      while (k >= 0);

   return False;
}


/*---------------------------------------------*/
/* Shell sort increments (Knuth's sequence) */
static
Int32 incs[14] = { 1, 4, 13, 40, 121, 364, 1093, 3280,
                   9841, 29524, 88573, 265720,
                   797161, 2391484 };

static
void mainSimpleSort ( UInt32* ptr,
                      UChar*  block,
                      UInt16* quadrant,
                      Int32   nblock,
                      Int32   lo,
                      Int32   hi,
                      Int32   d,
                      Int32*  budget )
{
   Int32 i, j, h, bigN, hp;
   UInt32 v;

   bigN = hi - lo + 1;
   if (bigN < 2) return;

   hp = 0;
   while (incs[hp] < bigN) hp++;
   hp--;

   for (; hp >= 0; hp--) {
      h = incs[hp];

      i = lo + h;
      while (True) {

         /*-- copy 1 --*/
         if (i > hi) break;
         v = ptr[i];
         j = i;
         while ( mainGtU (
                    ptr[j-h]+d, v+d, block, quadrant, nblock, budget
                 ) ) {
            ptr[j] = ptr[j-h];
            j = j - h;
            if (j <= (lo + h - 1)) break;
         }
         ptr[j] = v;
         i++;

         /*-- copy 2 --*/
         if (i > hi) break;
         v = ptr[i];
         j = i;
         while ( mainGtU (
                    ptr[j-h]+d, v+d, block, quadrant, nblock, budget
                 ) ) {
            ptr[j] = ptr[j-h];
            j = j - h;
            if (j <= (lo + h - 1)) break;
         }
         ptr[j] = v;
         i++;

         /*-- copy 3 --*/
         if (i > hi) break;
         v = ptr[i];
         j = i;
         while ( mainGtU (
                    ptr[j-h]+d, v+d, block, quadrant, nblock, budget
                 ) ) {
            ptr[j] = ptr[j-h];
            j = j - h;
            if (j <= (lo + h - 1)) break;
         }
         ptr[j] = v;
         i++;

         if (*budget < 0) return;
      }
   }
}


/*---------------------------------------------*/
/* 3-way quicksort for strings (Bentley-Sedgewick) */

#define mswap(zz1, zz2) \
   { Int32 zztmp = zz1; zz1 = zz2; zz2 = zztmp; }

#define mvswap(zzp1, zzp2, zzn)       \
{                                     \
   Int32 yyp1 = (zzp1);               \
   Int32 yyp2 = (zzp2);               \
   Int32 yyn  = (zzn);                \
   while (yyn > 0) {                  \
      mswap(ptr[yyp1], ptr[yyp2]);    \
      yyp1++; yyp2++; yyn--;          \
   }                                  \
}

static
__inline__
UChar mmed3 ( UChar a, UChar b, UChar c )
{
   UChar t;
   if (a > b) { t = a; a = b; b = t; };
   if (b > c) {
      b = c;
      if (a > b) b = a;
   }
   return b;
}

#define mmin(a,b) ((a) < (b)) ? (a) : (b)

#define mpush(lz,hz,dz) { stackLo[sp] = lz; \
                          stackHi[sp] = hz; \
                          stackD [sp] = dz; \
                          sp++; }

#define mpop(lz,hz,dz) { sp--;             \
                         lz = stackLo[sp]; \
                         hz = stackHi[sp]; \
                         dz = stackD [sp]; }


#define mnextsize(az) (nextHi[az]-nextLo[az])

#define mnextswap(az,bz)                                        \
   { Int32 tz;                                                  \
     tz = nextLo[az]; nextLo[az] = nextLo[bz]; nextLo[bz] = tz; \
     tz = nextHi[az]; nextHi[az] = nextHi[bz]; nextHi[bz] = tz; \
     tz = nextD [az]; nextD [az] = nextD [bz]; nextD [bz] = tz; }


#define MAIN_QSORT_SMALL_THRESH 20
#define MAIN_QSORT_DEPTH_THRESH (BZ_N_RADIX + BZ_N_QSORT)
#define MAIN_QSORT_STACK_SIZE 100

static
void mainQSort3 ( UInt32* ptr,
                  UChar*  block,
                  UInt16* quadrant,
                  Int32   nblock,
                  Int32   loSt,
                  Int32   hiSt,
                  Int32   dSt,
                  Int32*  budget )
{
   Int32 unLo, unHi, ltLo, gtHi, n, m, med;
   Int32 sp, lo, hi, d;

   Int32 stackLo[MAIN_QSORT_STACK_SIZE];
   Int32 stackHi[MAIN_QSORT_STACK_SIZE];
   Int32 stackD [MAIN_QSORT_STACK_SIZE];

   Int32 nextLo[3];
   Int32 nextHi[3];
   Int32 nextD [3];

   sp = 0;
   mpush ( loSt, hiSt, dSt );

   while (sp > 0) {

      AssertH ( sp < MAIN_QSORT_STACK_SIZE - 2, 1001 );

      mpop ( lo, hi, d );
      if (hi - lo < MAIN_QSORT_SMALL_THRESH ||
          d > MAIN_QSORT_DEPTH_THRESH) {
         mainSimpleSort ( ptr, block, quadrant, nblock, lo, hi, d, budget );
         if (*budget < 0) return;
         continue;
      }

      med = (Int32)
            mmed3 ( block[ptr[ lo         ]+d],
                    block[ptr[ hi         ]+d],
                    block[ptr[ (lo+hi)>>1 ]+d] );

      unLo = ltLo = lo;
      unHi = gtHi = hi;

      while (True) {
         while (True) {
            if (unLo > unHi) break;
            n = ((Int32)block[ptr[unLo]+d]) - med;
            if (n == 0) {
               mswap(ptr[unLo], ptr[ltLo]);
               ltLo++; unLo++; continue;
            };
            if (n >  0) break;
            unLo++;
         }
         while (True) {
            if (unLo > unHi) break;
            n = ((Int32)block[ptr[unHi]+d]) - med;
            if (n == 0) {
               mswap(ptr[unHi], ptr[gtHi]);
               gtHi--; unHi--; continue;
            };
            if (n <  0) break;
            unHi--;
         }
         if (unLo > unHi) break;
         mswap(ptr[unLo], ptr[unHi]); unLo++; unHi--;
      }

      AssertD ( unHi == unLo-1, "mainQSort3(2)" );

      if (gtHi < ltLo) {
         mpush(lo, hi, d+1 );
         continue;
      }

      n = mmin(ltLo-lo, unLo-ltLo); mvswap(lo, unLo-n, n);
      m = mmin(hi-gtHi, gtHi-unHi); mvswap(unLo, hi-m+1, m);

      n = lo + unLo - ltLo - 1;
      m = hi - (gtHi - unHi) + 1;

      nextLo[0] = lo;  nextHi[0] = n;   nextD[0] = d;
      nextLo[1] = m;   nextHi[1] = hi;  nextD[1] = d;
      nextLo[2] = n+1; nextHi[2] = m-1; nextD[2] = d+1;

      if (mnextsize(0) < mnextsize(1)) mnextswap(0,1);
      if (mnextsize(1) < mnextsize(2)) mnextswap(1,2);
      if (mnextsize(0) < mnextsize(1)) mnextswap(0,1);

      AssertD (mnextsize(0) >= mnextsize(1), "mainQSort3(8)" );
      AssertD (mnextsize(1) >= mnextsize(2), "mainQSort3(9)" );

      mpush (nextLo[0], nextHi[0], nextD[0]);
      mpush (nextLo[1], nextHi[1], nextD[1]);
      mpush (nextLo[2], nextHi[2], nextD[2]);
   }
}

#undef mswap
#undef mvswap
#undef mpush
#undef mpop
#undef mmin
#undef mnextsize
#undef mnextswap
#undef MAIN_QSORT_SMALL_THRESH
#undef MAIN_QSORT_DEPTH_THRESH
#undef MAIN_QSORT_STACK_SIZE


/*---------------------------------------------*/
/* Main sort: 2-byte radix sort + quicksort    */
/*---------------------------------------------*/

#define BIGFREQ(b) (ftab[((b)+1) << 8] - ftab[(b) << 8])
#define SETMASK (1 << 21)
#define CLEARMASK (~(SETMASK))

static
void mainSort ( UInt32* ptr,
                UChar*  block,
                UInt16* quadrant,
                UInt32* ftab,
                Int32   nblock,
                Int32   verb,
                Int32*  budget )
{
   Int32  i, j, k, ss, sb;
   Int32  runningOrder[256];
   Bool   bigDone[256];
   Int32  copyStart[256];
   Int32  copyEnd  [256];
   UChar  c1;
   Int32  numQSorted;
   UInt16 s;
   if (verb >= 4) VPrintf0 ( "        main sort initialise ...\n" );

   /*-- set up the 2-byte frequency table --*/
   for (i = 65536; i >= 0; i--) ftab[i] = 0;

   j = block[0] << 8;
   i = nblock-1;
   for (; i >= 3; i -= 4) {
      quadrant[i] = 0;
      j = (j >> 8) | ( ((UInt16)block[i]) << 8);
      ftab[j]++;
      quadrant[i-1] = 0;
      j = (j >> 8) | ( ((UInt16)block[i-1]) << 8);
      ftab[j]++;
      quadrant[i-2] = 0;
      j = (j >> 8) | ( ((UInt16)block[i-2]) << 8);
      ftab[j]++;
      quadrant[i-3] = 0;
      j = (j >> 8) | ( ((UInt16)block[i-3]) << 8);
      ftab[j]++;
   }
   for (; i >= 0; i--) {
      quadrant[i] = 0;
      j = (j >> 8) | ( ((UInt16)block[i]) << 8);
      ftab[j]++;
   }

   for (i = 0; i < BZ_N_OVERSHOOT; i++) {
      block   [nblock+i] = block[i];
      quadrant[nblock+i] = 0;
   }

   if (verb >= 4) VPrintf0 ( "        bucket sorting ...\n" );

   /*-- Complete the initial radix sort --*/
   for (i = 1; i <= 65536; i++) ftab[i] += ftab[i-1];

   s = block[0] << 8;
   i = nblock-1;
   for (; i >= 3; i -= 4) {
      s = (s >> 8) | (block[i] << 8);
      j = ftab[s] -1;
      ftab[s] = j;
      ptr[j] = i;
      s = (s >> 8) | (block[i-1] << 8);
      j = ftab[s] -1;
      ftab[s] = j;
      ptr[j] = i-1;
      s = (s >> 8) | (block[i-2] << 8);
      j = ftab[s] -1;
      ftab[s] = j;
      ptr[j] = i-2;
      s = (s >> 8) | (block[i-3] << 8);
      j = ftab[s] -1;
      ftab[s] = j;
      ptr[j] = i-3;
   }
   for (; i >= 0; i--) {
      s = (s >> 8) | (block[i] << 8);
      j = ftab[s] -1;
      ftab[s] = j;
      ptr[j] = i;
   }

   for (i = 0; i <= 255; i++) {
      bigDone     [i] = False;
      runningOrder[i] = i;
   }

   {
      Int32 vv;
      Int32 h = 1;
      do h = 3 * h + 1; while (h <= 256);
      do {
         h = h / 3;
         for (i = h; i <= 255; i++) {
            vv = runningOrder[i];
            j = i;
            while ( BIGFREQ(runningOrder[j-h]) > BIGFREQ(vv) ) {
               runningOrder[j] = runningOrder[j-h];
               j = j - h;
               if (j <= (h - 1)) goto zero;
            }
            zero:
            runningOrder[j] = vv;
         }
      } while (h != 1);
   }

   numQSorted = 0;

   for (i = 0; i <= 255; i++) {

      ss = runningOrder[i];

      for (j = 0; j <= 255; j++) {
         if (j != ss) {
            sb = (ss << 8) + j;
            if ( ! (ftab[sb] & SETMASK) ) {
               Int32 lo = ftab[sb]   & CLEARMASK;
               Int32 hi = (ftab[sb+1] & CLEARMASK) - 1;
               if (hi > lo) {
                  if (verb >= 4)
                     VPrintf4 ( "        qsort [0x%x, 0x%x]   "
                                "done %d   this %d\n",
                                ss, j, numQSorted, hi - lo + 1 );
                  mainQSort3 (
                     ptr, block, quadrant, nblock,
                     lo, hi, BZ_N_RADIX, budget
                  );
                  numQSorted += (hi - lo + 1);
                  if (*budget < 0) return;
               }
            }
            ftab[sb] |= SETMASK;
         }
      }

      AssertH ( !bigDone[ss], 1006 );

      {
         for (j = 0; j <= 255; j++) {
            copyStart[j] =  ftab[(j << 8) + ss]     & CLEARMASK;
            copyEnd  [j] = (ftab[(j << 8) + ss + 1] & CLEARMASK) - 1;
         }
         for (j = ftab[ss << 8] & CLEARMASK; j < copyStart[ss]; j++) {
            k = ptr[j]-1; if (k < 0) k += nblock;
            c1 = block[k];
            if (!bigDone[c1])
               ptr[ copyStart[c1]++ ] = k;
         }
         for (j = (ftab[(ss+1) << 8] & CLEARMASK) - 1; j > copyEnd[ss]; j--) {
            k = ptr[j]-1; if (k < 0) k += nblock;
            c1 = block[k];
            if (!bigDone[c1])
               ptr[ copyEnd[c1]-- ] = k;
         }
      }

      AssertH ( (copyStart[ss]-1 == copyEnd[ss])
                ||
                (copyStart[ss] == 0 && copyEnd[ss] == nblock-1),
                1007 )

      for (j = 0; j <= 255; j++) ftab[(j << 8) + ss] |= SETMASK;

      bigDone[ss] = True;

      if (i < 255) {
         Int32 bbStart  = ftab[ss << 8] & CLEARMASK;
         Int32 bbSize   = (ftab[(ss+1) << 8] & CLEARMASK) - bbStart;
         Int32 shifts   = 0;

         while ((bbSize >> shifts) > 65534) shifts++;

         for (j = bbSize-1; j >= 0; j--) {
            Int32 a2update     = ptr[bbStart + j];
            UInt16 qVal        = (UInt16)(j >> shifts);
            quadrant[a2update] = qVal;
            if (a2update < BZ_N_OVERSHOOT)
               quadrant[a2update + nblock] = qVal;
         }
         AssertH ( ((bbSize-1) >> shifts) <= 65535, 1002 );
      }

   }

   if (verb >= 4)
      VPrintf3 ( "        %d pointers, %d sorted, %d scanned\n",
                 nblock, numQSorted, nblock - numQSorted );
}


/*---------------------------------------------*/
/* Top-level block sort entry point            */
/*---------------------------------------------*/
void BZ2_blockSort ( EState* s )
{
   UInt32* ptr    = s->ptr;
   UChar*  block  = s->block;
   UInt32* ftab   = s->ftab;
   Int32   nblock = s->nblock;
   Int32   verb   = s->verbosity;
   Int32   wfact  = s->workFactor;
   UInt16* quadrant;
   Int32   budget;
   Int32   budgetInit;
   Int32   i;

   if (nblock < 10000) {
      fallbackSort ( s->arr1, s->arr2, ftab, nblock, verb );
   } else {
      /* Set up quadrant area */
      i = nblock+BZ_N_OVERSHOOT;
      if (i & 1) i++;
      quadrant = (UInt16*)(&(block[i]));

      if (wfact < 1  ) wfact = 1;
      if (wfact > 100) wfact = 100;
      budgetInit = nblock * ((wfact-1) / 3);
      budget = budgetInit;

      mainSort ( ptr, block, quadrant, ftab, nblock, verb, &budget );
      if (verb >= 3)
         VPrintf3 ( "      %d work, %d block, ratio %5.2f\n",
                    budgetInit - budget,
                    nblock,
                    (float)(budgetInit - budget) /
                    (float)(nblock==0 ? 1 : nblock) );
      if (budget < 0) {
         if (verb >= 2)
            VPrintf0 ( "    too repetitive; using SA-IS"
                       " sorting algorithm\n" );
         sais_bwt ( block, ptr, nblock, &(s->origPtr) );
         AssertH( s->origPtr != -1, 1003 );
         return;  /* origPtr already set by sais_bwt */
      }
   }

   s->origPtr = -1;
   for (i = 0; i < s->nblock; i++)
      if (ptr[i] == 0)
         { s->origPtr = i; break; };

   AssertH( s->origPtr != -1, 1003 );
}

/*--- end                                   blocksort.c ---*/
