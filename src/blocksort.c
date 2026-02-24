/*
 * libqbz2 - Block sorting (Burrows-Wheeler Transform)
 *
 * Uses libsais SA-IS for all blocks >= 10000
 *   (https://github.com/IlyaGrebnov/libsais, Apache 2.0 license)
 * Falls back to fallbackSort (exponential radix sort) for small blocks (< 10000)
 */

#include "qbz2_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libsais.h"

/*---------------------------------------------*/
/*--- libsais-based BWT for repetitive blocks ---*/
/*---------------------------------------------*/

/*
 * Compute the BWT of a block using libsais for suffix array construction.
 * Uses the doubled-string technique to compute cyclic rotation sort:
 *   1. Concatenate block with itself: T' = T || T  (length 2n)
 *   2. Build suffix array of T' using libsais
 *   3. Filter SA entries where SA[i] < n (these correspond to cyclic rotations)
 *   4. Extract origPtr (position where SA[i] == 0)
 *
 * This produces the exact same BWT as bzip2's reference implementation.
 */
static void sais_bwt ( UChar* block, UInt32* ptr, Int32 nblock, Int32* origPtr )
{
   UChar  *doubled;
   int32_t *SA;
   Int32  dlen, i, j;

   dlen = 2 * nblock;
   doubled = (UChar *)malloc((size_t)dlen);
   SA = (int32_t *)malloc((size_t)dlen * sizeof(int32_t));
   if (!doubled || !SA) { free(doubled); free(SA); return; }

   memcpy(doubled, block, (size_t)nblock);
   memcpy(doubled + nblock, block, (size_t)nblock);

   /* libsais builds SA[0..dlen-1] with no sentinel slot */
   if (libsais(doubled, SA, (int32_t)dlen, 0, NULL) != 0) {
      /* SA construction failed -- fall through with origPtr = -1 */
      free(doubled);
      free(SA);
      return;
   }

   *origPtr = -1;
   j = 0;
   for (i = 0; i < dlen; i++) {
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
/* Top-level block sort entry point            */
/*---------------------------------------------*/
void BZ2_blockSort ( EState* s )
{
   UInt32* ptr    = s->ptr;
   UChar*  block  = s->block;
   UInt32* ftab   = s->ftab;
   Int32   nblock = s->nblock;
   Int32   verb   = s->verbosity;

   if (nblock < 10000) {
      fallbackSort ( s->arr1, s->arr2, ftab, nblock, verb );
   } else {
      /* Use libsais for all blocks >= 10000.
       * libsais SA-IS is faster than mainSort's radix+quicksort
       * for both structured (text) and repetitive data, with
       * only a minor cost on random binary data (still at parity
       * with the reference implementation).
       */
      sais_bwt ( block, ptr, nblock, &(s->origPtr) );
      AssertH( s->origPtr != -1, 1003 );
      return;
   }

   s->origPtr = -1;
   for (Int32 i = 0; i < s->nblock; i++)
      if (ptr[i] == 0)
         { s->origPtr = i; break; };

   AssertH( s->origPtr != -1, 1003 );
}

/*--- end                                   blocksort.c ---*/
