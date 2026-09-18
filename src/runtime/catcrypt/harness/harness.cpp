/* Differential harness for the non-GMP bignum layer of the Lean runtime.

   The same source is linked twice: against this tree's src/runtime/mpn.cpp and
   mpz.cpp with the CatCrypt kernel objects (64-bit digits), and against the
   v4.33.1 versions of the two files (32-bit digits). Operands are generated as
   32-bit words, identically in both builds, and packed into the build's digit
   width; every result is unpacked into 32-bit words with leading zero words
   removed, so the two builds print the same line exactly when they compute the
   same value. Each line is `<case> <op> <hash>`, the hash being FNV-1a over the
   result words and the operation's width-independent extra value. The Makefile
   compares the outputs; `HARNESS_DUMP=<case>` prints that case's operands and
   results in hex.

   mpn level: mpn_add, mpn_sub (the value when no borrow occurs, and the
   borrow), mpn_sub in place, mpn_compare, mpn_mul (also squaring), mpn_div
   (whose normalization and correction steps call mpn_mul, mpn_sub and mpn_add).
   mpz level: &, |, ^ (both signs, including x op= x), mul2k and div2k on
   non-negative operands (including a == b), *, +, -, /, %, cmp, log2, and the
   uint64/size_t/int conversions.

   Usage: harness <cases> <max-words> <seed> */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "runtime/mpn.h"
#include "runtime/mpz.h"

namespace lean {
/* mpz declares object_compactor a friend; the harness uses that access to
   build an mpz from digits and to read its representation. The runtime's own
   object_compactor is not linked into this binary. */
class object_compactor {
public:
    static void assign(mpz & z, bool neg, std::vector<mpn_digit> const & d) {
        if (d.empty()) {
            mpn_digit zero = 0;
            z.set(1, &zero);
        } else {
            z.set(d.size(), d.data());
        }
        z.m_sign = neg && !z.is_zero();
    }
    static bool sign(mpz const & z) { return z.m_sign; }
    static size_t size(mpz const & z) { return z.m_size; }
    static mpn_digit const * digits(mpz const & z) { return z.m_digits; }
};
}

using lean::mpn_digit;
using lean::mpz;
using acc = lean::object_compactor;
typedef std::vector<mpn_digit> digits;
typedef std::vector<uint32_t> words;

static const size_t WPD = sizeof(mpn_digit) / sizeof(uint32_t);  // words per digit

static uint64_t g_state;
static long g_dump = -1;
static unsigned long g_case = 0;
static unsigned long g_lines = 0;

static uint64_t rnd() {
    uint64_t x = g_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return g_state = x;
}
static uint64_t below(uint64_t n) { return rnd() % n; }

static words make_words(size_t n) {
    words v(n);
    uint64_t mode = below(8);
    for (size_t i = 0; i < n; i++) {
        switch (mode) {
        case 0: v[i] = 0; break;
        case 1: v[i] = 0xFFFFFFFFu; break;
        case 2: v[i] = below(10) == 0 ? (uint32_t)rnd() : 0; break;
        case 3: v[i] = below(2) ? 0xFFFFFFFFu : 0; break;
        default: v[i] = (uint32_t)rnd(); break;
        }
    }
    return v;
}

static size_t rand_len(size_t max) { return (size_t)below(max + 1); }

/* The digits of the build's width holding the value of w (little-endian); a
   partial top digit is zero-filled. */
static digits to_digits(words const & w) {
    digits d((w.size() + WPD - 1) / WPD, 0);
    for (size_t i = 0; i < w.size(); i++)
        d[i / WPD] |= (mpn_digit)w[i] << (32 * (i % WPD));
    return d;
}

/* The 32-bit words of the value of p[0..n), leading zero words removed. */
static words value_words(mpn_digit const * p, size_t n) {
    words w(n * WPD);
    for (size_t i = 0; i < w.size(); i++)
        w[i] = (uint32_t)(p[i / WPD] >> (32 * (i % WPD)));
    while (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

/* The normalized mpz digit form of the value w: leading zero digits removed,
   and the single digit 0 for the value zero. mpn_compare's callers pass it. */
static digits normalized(words const & w) {
    digits d = to_digits(w);
    while (!d.empty() && d.back() == 0) d.pop_back();
    if (d.empty()) d.push_back(0);
    return d;
}

static uint64_t rand_shift() {
    static const uint64_t fixed[] = {0, 1, 31, 32, 33, 63, 64, 65, 127, 128};
    return below(2) ? fixed[below(10)] : below(20001);
}

struct hasher {
    uint64_t h = 1469598103934665603ull;
    void word(uint64_t w) {
        for (int i = 0; i < 8; i++) {
            h ^= (w >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
};

static void dump_words(char const * label, words const & w) {
    printf("  %s [%zu words]:", label, w.size());
    for (size_t i = w.size(); i-- > 0;) printf(" %08x", w[i]);
    printf("\n");
}

static void emit(char const * op, hasher const & h) {
    printf("%lu %s %016llx\n", g_case, op, (unsigned long long)h.h);
    g_lines++;
}

static void emit_value(char const * op, mpn_digit const * p, size_t n, uint64_t extra) {
    words w = value_words(p, n);
    hasher h;
    h.word(w.size());
    h.word(extra);
    for (uint32_t x : w) h.word(x);
    emit(op, h);
    if ((long)g_case == g_dump) {
        dump_words(op, w);
        printf("  %s extra: %llu\n", op, (unsigned long long)extra);
    }
}

static void emit_mpz(char const * op, mpz const & z) {
    emit_value(op, acc::digits(z), acc::size(z), acc::sign(z) ? 1 : 0);
}

static void emit_int(char const * op, long long v) {
    hasher h;
    h.word((uint64_t)v);
    emit(op, h);
    if ((long)g_case == g_dump) printf("  %s = %lld\n", op, v);
}

static void mpn_case(size_t max_words) {
    size_t wa = rand_len(max_words), wb = rand_len(max_words);
    if (below(8) == 0) wb = wa;
    // mpn_add and mpn_sub require max(la, lb) > 0.
    if (wa == 0 && wb == 0) wa = wb = 1;
    words va = make_words(wa), vb = make_words(wb);
    if (wb == wa && below(2) == 0) vb = va;
    if ((long)g_case == g_dump) {
        dump_words("a", va);
        dump_words("b", vb);
    }
    digits a = to_digits(va), b = to_digits(vb);
    size_t la = a.size(), lb = b.size();
    if (la == 0 && lb == 0) { a.push_back(0); la = 1; }
    size_t len = la > lb ? la : lb;

    {   // add
        digits c(len + 1, (mpn_digit)0xDEADBEEF);
        size_t os = 0;
        lean::mpn_add(a.data(), la, b.data(), lb, c.data(), len + 1, &os);
        emit_value("mpn_add", c.data(), os, 0);
    }
    {   // sub, into a fresh buffer: the value when there is no borrow, and the borrow
        digits c(len, (mpn_digit)0xDEADBEEF);
        mpn_digit borrow = 7;
        lean::mpn_sub(a.data(), la, b.data(), lb, c.data(), &borrow);
        if (borrow == 0) emit_value("mpn_sub", c.data(), len, 0);
        emit_int("mpn_sub_borrow", (long long)borrow);
    }
    if (wa >= wb) {   // sub in place, as mpn_div calls it (then la >= lb at either width)
        digits c = a;
        mpn_digit borrow = 7;
        lean::mpn_sub(c.data(), la, b.data(), lb, c.data(), &borrow);
        if (borrow == 0) emit_value("mpn_sub_inplace", c.data(), la, 0);
        emit_int("mpn_sub_inplace_borrow", (long long)borrow);
    }
    {   // compare, on the normalized operands mpz passes
        digits na = normalized(va), nb = normalized(vb);
        emit_int("mpn_compare", lean::mpn_compare(na.data(), na.size(), nb.data(), nb.size()));
    }
    if (la > 0 && lb > 0) {   // mul
        digits c(la + lb, (mpn_digit)0xDEADBEEF);
        lean::mpn_mul(a.data(), la, b.data(), lb, c.data());
        emit_value("mpn_mul", c.data(), la + lb, 0);
    }
    if (la > 0) {   // square through one pointer
        digits c(2 * la, (mpn_digit)0xDEADBEEF);
        lean::mpn_mul(a.data(), la, a.data(), la, c.data());
        emit_value("mpn_sqr", c.data(), 2 * la, 0);
    }
    {   // div: numerator of lnum >= lden digits, denominator with a nonzero top
        size_t wden = 1 + rand_len(max_words > 1 ? max_words - 1 : 0);
        size_t wnum = wden + rand_len(max_words);
        words vnum = make_words(wnum), vden = make_words(wden);
        if (vden[wden - 1] == 0) vden[wden - 1] = 1 + (uint32_t)below(0xFFFFFFFEu);
        if (vnum[wnum - 1] == 0) vnum[wnum - 1] = 1;
        digits num = to_digits(vnum), den = to_digits(vden);
        size_t lnum = num.size(), lden = den.size();
        digits q(lnum - lden + 1, 0), r(lden, 0);
        lean::mpn_div(num.data(), lnum, den.data(), lden, q.data(), r.data());
        emit_value("mpn_div_q", q.data(), q.size(), 0);
        emit_value("mpn_div_r", r.data(), r.size(), 0);
    }
}

static void mpz_case(size_t max_words) {
    words wa = make_words(rand_len(max_words));
    words wb = below(8) == 0 ? wa : make_words(rand_len(max_words));
    bool na = below(4) == 0, nb = below(4) == 0;
    mpz a, b;
    acc::assign(a, na, to_digits(wa));
    acc::assign(b, nb, to_digits(wb));
    if ((long)g_case == g_dump) {
        dump_words("a", value_words(acc::digits(a), acc::size(a)));
        printf("  a sign %d\n", acc::sign(a));
        dump_words("b", value_words(acc::digits(b), acc::size(b)));
        printf("  b sign %d\n", acc::sign(b));
    }

    emit_mpz("mpz_and", a & b);
    emit_mpz("mpz_ior", a | b);
    emit_mpz("mpz_xor", a ^ b);
    {   // x op= x
        mpz x(a), y(a), z(a);
        x &= x;
        y |= y;
        z ^= z;
        emit_mpz("mpz_and_self", x);
        emit_mpz("mpz_ior_self", y);
        emit_mpz("mpz_xor_self", z);
    }

    mpz pa = abs(a);
    unsigned k = (unsigned)rand_shift();
    {
        mpz r;
        mul2k(r, pa, k);
        emit_mpz("mpz_mul2k", r);
        mpz s;
        div2k(s, pa, k);
        emit_mpz("mpz_div2k", s);
        mpz t(pa);
        mul2k(t, t, k);
        emit_mpz("mpz_mul2k_alias", t);
        mpz u(pa);
        div2k(u, u, k);
        emit_mpz("mpz_div2k_alias", u);
        mpz back;
        div2k(back, r, k);
        emit_int("mpz_shift_roundtrip", back == pa ? 1 : 0);
    }

    emit_mpz("mpz_mul", a * b);
    emit_mpz("mpz_add", a + b);
    emit_mpz("mpz_sub", a - b);
    emit_int("mpz_cmp", cmp(a, b));
    emit_int("mpz_log2", pa.is_zero() ? -1 : (long long)pa.log2());
    emit_int("mpz_mod64", (long long)a.mod64());
    emit_int("mpz_mod32", (long long)a.mod32());
    emit_int("mpz_is_size_t", pa.is_size_t() ? (long long)pa.get_size_t() : -1);
    emit_int("mpz_is_unsigned_int", pa.is_unsigned_int() ? (long long)pa.get_unsigned_int() : -1);
    emit_int("mpz_is_int", a.is_int() ? (long long)a.get_int() : -1);
    {
        uint64_t u = rnd();
        mpz m(u);
        emit_mpz("mpz_of_uint64", m);
        emit_int("mpz_of_uint64_back", (long long)m.mod64());
        emit_int("mpz_cmp_uint", cmp(a, (unsigned)u));
        emit_int("mpz_cmp_int", cmp(a, (int)u));
        mpz s(a);
        s += (unsigned)u;
        s -= (int)(u >> 32);
        s *= (unsigned)(u >> 16);
        emit_mpz("mpz_small_ops", s);
    }
    emit_int("mpz_hash", (long long)a.hash());
    {
        std::string str = a.to_string();
        hasher h;
        for (char ch : str) h.word((uint64_t)(unsigned char)ch);
        emit("mpz_to_string", h);
        mpz back(str.c_str());
        emit_int("mpz_string_roundtrip", back == a ? 1 : 0);
    }
    {
        mpz sq(a);
        sq *= sq;
        emit_mpz("mpz_sqr", sq);
    }
    if (!b.is_zero()) {
        emit_mpz("mpz_div", a / b);
        emit_mpz("mpz_rem", a % b);
    }
}

int main(int argc, char ** argv) {
    unsigned long cases = argc > 1 ? strtoul(argv[1], nullptr, 10) : 20000;
    size_t max_words = argc > 2 ? strtoul(argv[2], nullptr, 10) : 300;
    g_state = argc > 3 ? strtoull(argv[3], nullptr, 0) : 0x9e3779b97f4a7c15ull;
    if (g_state == 0) g_state = 1;
    if (char const * d = getenv("HARNESS_DUMP")) g_dump = strtol(d, nullptr, 10);

    unsigned long c = 0;
    for (unsigned long i = 0; i < cases; i++, c++) {
        g_case = c;
        mpn_case(max_words);
    }
    for (unsigned long i = 0; i < cases; i++, c++) {
        g_case = c;
        mpz_case(max_words);
    }
    fprintf(stderr, "harness: %lu cases (%lu mpn, %lu mpz), %lu operation lines, max %zu words, %zu-bit digits\n",
            c, cases, cases, g_lines, max_words, 8 * sizeof(mpn_digit));
    return 0;
}
