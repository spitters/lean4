/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    mpn.cpp

Abstract:

    Multi Precision Natural Numbers

Author:

    Christoph Wintersteiger (cwinter) 2011-11-16.

Revision History:

--*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/mpn.h"
#include "runtime/mpz.h"
#include "runtime/debug.h"
#include "runtime/buffer.h"

#define max(a,b)    (((a) > (b)) ? (a) : (b))

// CATCRYPT VERIFIED MPN: mpn_add, mpn_sub, mpn_compare and mpn_mul call the
// u32 entry points of libcatcrypt_mpn.a (declared in runtime/mpz.h), which run
// u64 limb kernels emitted by the CatCrypt compiler. mpn_div and mpn_to_string
// keep the fallback implementation below. See CATCRYPT_MPN_TCB.md.

namespace lean {

typedef uint64_t mpn_double_digit;
static_assert(sizeof(mpn_double_digit) == 2 * sizeof(mpn_digit), "size alignment");

static const mpn_digit zero = 0;

#define DIGIT_BITS (sizeof(mpn_digit)*8)
#define HALF_BITS (sizeof(mpn_digit)*4)

// CATCRYPT VERIFIED MPN: call counters (see runtime/mpz.h).
static const char * const g_catcrypt_mpn_op_names[CATCRYPT_MPN_NUM_OPS] = {
    "add", "sub", "compare", "mul", "and", "ior", "xor", "lshift", "rshift"
};

std::atomic<uint64_t> g_catcrypt_mpn_routed[CATCRYPT_MPN_NUM_OPS];
std::atomic<uint64_t> g_catcrypt_mpn_legacy[CATCRYPT_MPN_NUM_OPS];

static void catcrypt_mpn_print_stats() {
    fprintf(stderr, "catcrypt-mpn call counts (routed / legacy):\n");
    for (int op = 0; op < CATCRYPT_MPN_NUM_OPS; op++) {
        fprintf(stderr, "  %-8s %20llu %20llu\n", g_catcrypt_mpn_op_names[op],
                static_cast<unsigned long long>(g_catcrypt_mpn_routed[op].load()),
                static_cast<unsigned long long>(g_catcrypt_mpn_legacy[op].load()));
    }
}

static bool catcrypt_mpn_stats_init() {
    if (getenv("LEAN_CATCRYPT_MPN_STATS") == nullptr)
        return false;
    atexit(catcrypt_mpn_print_stats);
    return true;
}

// Dynamic initialization; a call made before it runs sees `false` (static
// storage is zero-initialized first) and is not counted.
bool g_catcrypt_mpn_stats = catcrypt_mpn_stats_init();

// CATCRYPT VERIFIED MPN: scratch digits for the zero-extended operands of
// mpn_add/mpn_sub. Up to CATCRYPT_MPN_STACK_DIGITS digits live in the buffer's
// inline storage; larger requests are heap-allocated by buffer::ensure_capacity.
#define CATCRYPT_MPN_STACK_DIGITS 1024
typedef buffer<mpn_digit, CATCRYPT_MPN_STACK_DIGITS> catcrypt_scratch;

// CATCRYPT VERIFIED MPN: both operands must be normalized mpz digit arrays (at
// least one digit, and a nonzero top digit unless the value is the single digit
// 0), the form every caller in mpz.cpp passes. mpn_cmp_oc_32 then orders
// operands of different lengths by length and aborts on an unnormalized one.
int mpn_compare(mpn_digit const * a, size_t const lnga,
                mpn_digit const * b, size_t const lngb) {
    lean_assert(lnga > 0 && (lnga == 1 || a[lnga - 1] != 0));
    lean_assert(lngb > 0 && (lngb == 1 || b[lngb - 1] != 0));
    catcrypt_mpn_count(CATCRYPT_MPN_CMP, true);
    return mpn_cmp_oc_32(a, (uint64_t)lnga, b, (uint64_t)lngb);
}

// CATCRYPT VERIFIED MPN: equal-length add over operands zero-extended to
// len+1 digits, so the carry lands in the top digit; writes len+1 digits to c
// and sets *plngc to the normalized length (at least 1).
void  mpn_add(mpn_digit const * a, size_t const lnga,
              mpn_digit const * b, size_t const lngb,
              mpn_digit * c, size_t const lngc_alloc,
              size_t * plngc) {
    catcrypt_mpn_count(CATCRYPT_MPN_ADD, true);
    size_t len = max(lnga, lngb);
    lean_assert(lngc_alloc == len+1 && len > 0);
    size_t ext = len + 1;
    catcrypt_scratch scratch;
    scratch.ensure_capacity(2 * ext);
    mpn_digit * ea = scratch.begin();
    mpn_digit * eb = ea + ext;
    memset(ea, 0, 2 * ext * sizeof(mpn_digit));
    if (lnga > 0) memcpy(ea, a, lnga * sizeof(mpn_digit));
    if (lngb > 0) memcpy(eb, b, lngb * sizeof(mpn_digit));
    mpn_add_n_oc_32(c, ea, eb, (uint64_t)ext);
    size_t &os = *plngc;
    for (os = ext; os > 1 && c[os-1] == 0; ) os--;
    lean_assert(os > 0 && os <= ext);
}

// CATCRYPT VERIFIED MPN: equal-length sub over operands zero-extended to
// len+1 digits; writes len digits of a - b (mod 2^(32 len)) to c and sets
// *pborrow to 1 exactly when a < b (the top digit of the extended difference
// is then nonzero). c may coincide with a or b (mpn_div calls it in place):
// the kernel writes to scratch and the result is copied afterwards.
void mpn_sub(mpn_digit const * a, size_t const lnga,
             mpn_digit const * b, size_t const lngb,
             mpn_digit * c, mpn_digit * pborrow) {
    catcrypt_mpn_count(CATCRYPT_MPN_SUB, true);
    size_t len = max(lnga, lngb);
    size_t ext = len + 1;
    catcrypt_scratch scratch;
    scratch.ensure_capacity(3 * ext);
    mpn_digit * ea  = scratch.begin();
    mpn_digit * eb  = ea + ext;
    mpn_digit * res = eb + ext;
    memset(ea, 0, 2 * ext * sizeof(mpn_digit));
    if (lnga > 0) memcpy(ea, a, lnga * sizeof(mpn_digit));
    if (lngb > 0) memcpy(eb, b, lngb * sizeof(mpn_digit));
    mpn_sub_n_oc_32(res, ea, eb, (uint64_t)ext);
    if (len > 0) memcpy(c, res, len * sizeof(mpn_digit));
    *pborrow = (res[len] != 0) ? (mpn_digit)1 : (mpn_digit)0;
}

// CATCRYPT VERIFIED MPN: writes the lnga+lngb digit product (unnormalized) to
// c, which must not overlap a or b.
void mpn_mul(mpn_digit const * a, size_t const lnga,
             mpn_digit const * b, size_t const lngb,
             mpn_digit * c) {
    catcrypt_mpn_count(CATCRYPT_MPN_MUL, true);
    mpn_mul_oc_32(c, a, (uint64_t)lnga, b, (uint64_t)lngb);
}

#define MASK_FIRST (~((mpn_digit)(-1) >> 1))
#define FIRST_BITS(N, X) ((X) >> (DIGIT_BITS-(N)))
#define LAST_BITS(N, X) (((X) << (DIGIT_BITS-(N))) >> (DIGIT_BITS-(N)))
#define BASE ((mpn_double_digit)0x01 << DIGIT_BITS)

class  mpn_buffer : public buffer<mpn_digit> {
public:
    mpn_buffer() : buffer<mpn_digit>() {}

    mpn_buffer(size_t nsz, const mpn_digit & elem = 0):buffer<mpn_digit>() {
        for (size_t i = 0; i < nsz; i++) push_back(elem);
    }

    void resize(size_t nsz, const mpn_digit & elem = 0) {
        buffer<mpn_digit>::resize(static_cast<unsigned>(nsz), elem);
    }

    mpn_digit & operator[](size_t idx) {
        return buffer<mpn_digit>::operator[](static_cast<unsigned>(idx));
    }

    const mpn_digit & operator[](size_t idx) const {
        return buffer<mpn_digit>::operator[](static_cast<unsigned>(idx));
    }
};

static size_t div_normalize(mpn_digit const * numer, size_t const lnum,
                            mpn_digit const * denom, size_t const lden,
                            mpn_buffer & n_numer,
                            mpn_buffer & n_denom) {
    size_t d = 0;
    while (lden > 0 && ((denom[lden-1] << d) & MASK_FIRST) == 0) d++;
    lean_assert(d < DIGIT_BITS);

    n_numer.resize(lnum+1);
    n_denom.resize(lden);

    if (d == 0) {
        n_numer[lnum] = 0;
        for (size_t i = 0; i < lnum; i++)
            n_numer[i] = numer[i];
        for (size_t i = 0; i < lden; i++)
            n_denom[i] = denom[i];
    }
    else if (lnum != 0) {
        lean_assert(lden > 0);
        mpn_digit q = FIRST_BITS(d, numer[lnum-1]);
        n_numer[lnum] = q;
        for (size_t i = lnum-1; i > 0; i--)
            n_numer[i] = (numer[i] << d) | FIRST_BITS(d, numer[i-1]);
        n_numer[0] = numer[0] << d;
        for (size_t i = lden-1; i > 0; i--)
            n_denom[i] = denom[i] << d | FIRST_BITS(d, denom[i-1]);
        n_denom[0] = denom[0] << d;
    }
    else {
        d = 0;
    }
    return d;
}

static void div_unnormalize(mpn_buffer & numer, mpn_buffer & denom,
                            size_t const d, mpn_digit * rem) {
    if (d == 0) {
        for (size_t i = 0; i < denom.size(); i++)
            rem[i] = numer[i];
    }
    else {
        for (size_t i = 0; i < denom.size()-1; i++)
            rem[i] = numer[i] >> d | (LAST_BITS(d, numer[i+1]) << (DIGIT_BITS-d));
        rem[denom.size()-1] = numer[denom.size()-1] >> d;
    }
}

static void div_1(mpn_buffer & numer, mpn_digit const denom,
                  mpn_digit * quot) {
    mpn_double_digit q_hat, temp, ms;
    mpn_digit borrow;

    for (size_t j = numer.size()-1; j > 0; j--) {
        temp = (((mpn_double_digit)numer[j]) << DIGIT_BITS) | ((mpn_double_digit)numer[j-1]);
        q_hat = temp / (mpn_double_digit) denom;
        if (q_hat >= BASE) {
            lean_unreachable(); // is this reachable with normalized v?
        }
        lean_assert(q_hat < BASE);
        ms = temp - (q_hat * (mpn_double_digit) denom);
        borrow = ms > temp;
        numer[j-1] = (mpn_digit) ms;
        numer[j] = ms >> DIGIT_BITS;
        quot[j-1] = (mpn_digit) q_hat;
        if (borrow) {
            quot[j-1]--;
            numer[j] = numer[j-1] + denom;
        }
    }
}

static void div_n(mpn_buffer & numer, mpn_buffer const & denom,
                  mpn_digit * quot, mpn_digit * rem,
                  mpn_buffer & ms, mpn_buffer & ab) {
    lean_assert(denom.size() > 1);

    // This is essentially Knuth's Algorithm D.
    size_t m = numer.size() - denom.size();
    size_t n = denom.size();

    lean_assert(numer.size() == m+n);

    ms.resize(n+1);

    mpn_double_digit q_hat, temp, r_hat;
    mpn_digit borrow;

    for (size_t j = m-1; j != (size_t)-1; j--) {
        temp = (((mpn_double_digit)numer[j+n]) << DIGIT_BITS) | ((mpn_double_digit)numer[j+n-1]);
        q_hat = temp / (mpn_double_digit) denom[n-1];
        r_hat = temp % (mpn_double_digit) denom[n-1];
        recheck:
        if (q_hat >= BASE ||
            ((q_hat * denom[n-2]) > ((r_hat << DIGIT_BITS) + numer[j+n-2]))) {
                q_hat--;
                r_hat += denom[n-1];
                if (r_hat < BASE) goto recheck;
        }
        lean_assert(q_hat < BASE);
        // Replace numer[j+n]...numer[j] with
        // numer[j+n]...numer[j] - q * (denom[n-1]...denom[0])
        mpn_digit q_hat_small = (mpn_digit)q_hat;
        mpn_mul(&q_hat_small, 1, denom.data(), n, ms.data());
        mpn_sub(&numer[j], n+1, ms.data(), n+1, &numer[j], &borrow);
        quot[j] = q_hat_small;
        if (borrow) {
            quot[j]--;
            ab.resize(n+2);
            size_t real_size;
            mpn_add(denom.data(), n, &numer[j], n+1, ab.data(), n+2, &real_size);
            for (size_t i = 0; i < n+1; i++)
                numer[j+i] = ab[i];
        }
    }
}

void mpn_div(mpn_digit const * numer, size_t const lnum,
             mpn_digit const * denom, size_t const lden,
             mpn_digit * quot,
             mpn_digit * rem) {

    if (lnum < lden) {
        for (size_t i = 0; i < (lnum-lden+1); i++)
            quot[i] = 0;
        for (size_t i = 0; i < lden; i++)
            rem[i] = (i < lnum) ? numer[i] : 0;
        return;
    }

    bool all_zero = true;
    for (size_t i = 0; i < lden && all_zero; i++)
        if (denom[i] != zero) all_zero = false;

    lean_assert(!all_zero);

    lean_assert(denom[lden-1] != 0);

    if (lnum == 1 && lden == 1) {
        *quot = numer[0] / denom[0];
        *rem  = numer[0] % denom[0];
    }
    else if (lnum < lden || (lnum == lden && numer[lnum-1] < denom[lden-1])) {
        *quot = 0;
        for (size_t i = 0; i < lden; i++)
            rem[i] = (i < lnum) ? numer[i] : 0;
    }
    else  {
        mpn_buffer u, v, t_ms, t_ab;
        size_t d = div_normalize(numer, lnum, denom, lden, u, v);
        if (lden == 1)
            div_1(u, v[0], quot);
        else
            div_n(u, v, quot, rem, t_ms, t_ab);
        div_unnormalize(u, v, d, rem);
    }

#ifdef LEAN_DEBUG
    mpn_buffer temp(lnum+1, 0);
    mpn_mul(quot, lnum-lden+1, denom, lden, temp.data());
    size_t real_size;
    mpn_add(temp.data(), lnum, rem, lden, temp.data(), lnum+1, &real_size);
    bool ok = true;
    for (size_t i = 0; i < lnum && ok; i++)
        if (temp[i] != numer[i]) ok = false;
    if (temp[lnum] != 0) ok = false;
    lean_assert(ok);
#endif
}

char * mpn_to_string(mpn_digit const * a, size_t const lng, char * buf, size_t const lbuf) {
    lean_assert(buf && lbuf > 0);

    if (lng == 1) {
#ifdef _WINDOWS
        sprintf_s(buf, lbuf, "%u", *a);
#else
        snprintf(buf, lbuf, "%u", *a);
#endif
    }
    else {
        mpn_buffer temp(lng, 0), t_numer(lng+1, 0), t_denom(1, 0);
        for (unsigned i = 0; i < lng; i++)
            temp[i] = a[i];

        size_t j = 0;
        mpn_digit rem;
        mpn_digit ten = 10;
        while (!temp.empty() && (temp.size() > 1 || temp[0] != 0)) {
            size_t d = div_normalize(&temp[0], temp.size(), &ten, 1, t_numer, t_denom);
            div_1(t_numer, t_denom[0], &temp[0]);
            div_unnormalize(t_numer, t_denom, d, &rem);
            buf[j++] = '0' + rem;
            while (!temp.empty() && temp.back() == 0)
                temp.pop_back();
        }
        buf[j] = 0;

        j--;
        size_t mid = (j/2) + ((j % 2) ? 1 : 0);
        for (size_t i = 0; i < mid; i++)
            std::swap(buf[i], buf[j-i]);
    }
    return buf;
}
}
