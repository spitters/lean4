/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include <cstddef>
#ifdef LEAN_USE_GMP
#include <gmp.h>
#else
#include "runtime/mpn.h"
#endif
#include <string>
#include <iostream>
#include <limits>
#include <lean/lean.h>
#include "runtime/int.h"
#include "runtime/debug.h"

namespace lean {

/** \brief Wrapper for GMP integers */
class LEAN_EXPORT mpz {
    friend class object_compactor;
    friend class region_reader;
#ifdef LEAN_USE_GMP
    mpz_t m_val;
    mpz(__mpz_struct const * v) { mpz_init_set(m_val, v); }
#else
    bool        m_sign;
    size_t      m_size;
    mpn_digit * m_digits;
    void allocate(size_t s);
    void init();
    void init_str(char const * v);
    void init_uint(unsigned int v);
    void init_int(int v);
    void init_uint64(uint64 v);
    void init_int64(int64 v);
    void init_mpz(mpz const & v);
    void set(size_t sz, mpn_digit const * digits);
    mpz & add(bool sign, size_t sz, mpn_digit const * digits);
    mpz & mul(bool sign, size_t sz, mpn_digit const * digits);
    mpz & div(bool sign, size_t sz, mpn_digit const * digits);
    mpz & rem(size_t sz, mpn_digit const * digits);
#endif
public:
    mpz();
#ifdef LEAN_USE_GMP
    explicit mpz(mpz_t v);
#endif
    explicit mpz(char const * v);
    explicit mpz(unsigned int v);
    explicit mpz(int v);
    explicit mpz(uint64 v);
    explicit mpz(int64 v);
    static mpz of_size_t(size_t v) {
        if (sizeof(size_t) == sizeof(uint64)) // NOLINT
            return mpz((uint64) v); // NOLINT
        else
            return mpz((unsigned) v); // NOLINT
    }
    mpz(mpz const & s);
    mpz(mpz && s) noexcept;
    ~mpz();

#ifdef LEAN_USE_GMP
    void set(mpz_t r) const;
#endif

    friend void swap(mpz & a, mpz & b) noexcept;

    unsigned hash() const {
#ifdef LEAN_USE_GMP
        return static_cast<unsigned>(mpz_get_si(m_val));
#else
        return m_digits[0];
#endif
    }

    int sgn() const;

    friend int sgn(mpz const & a) { return a.sgn(); }

    bool is_pos() const {
#ifdef LEAN_USE_GMP
        return sgn() > 0;
#else
        return !m_sign && (m_size > 1 || m_digits[0] != 0);
#endif
    }

    bool is_neg() const {
#ifdef LEAN_USE_GMP
        return sgn() < 0;
#else
        return m_sign;
#endif
    }

    bool is_zero() const {
#ifdef LEAN_USE_GMP
        return sgn() == 0;
#else
        return m_size == 1 && m_digits[0] == 0;
#endif
    }

    bool is_nonpos() const { return !is_pos(); }

    bool is_nonneg() const { return !is_neg(); }

    void neg() {
#ifdef LEAN_USE_GMP
        mpz_neg(m_val, m_val);
#else
        if (!is_zero())
            m_sign = !m_sign;
#endif
    }

    friend mpz neg(mpz a) { a.neg(); return a; }

    void abs() {
#ifdef LEAN_USE_GMP
        mpz_abs(m_val, m_val);
#else
        m_sign = false;
#endif
    }

    friend mpz abs(mpz a) { a.abs(); return a; }

    bool is_int() const;
    bool is_unsigned_int() const;
    bool is_size_t() const;

    int get_int() const;
    unsigned int get_unsigned_int() const;
    size_t get_size_t() const;

    mpz & operator=(mpz const & v);
    mpz & operator=(mpz && v) { swap(*this, v); return *this; }
    mpz & operator=(char const * v);
    mpz & operator=(unsigned int v);
    mpz & operator=(int v);

    LEAN_EXPORT friend int cmp(mpz const & a, mpz const & b);
    LEAN_EXPORT friend int cmp(mpz const & a, unsigned b);
    LEAN_EXPORT friend int cmp(mpz const & a, int b);

    friend bool operator<(mpz const & a, mpz const & b) { return cmp(a, b) < 0; }
    friend bool operator<(mpz const & a, unsigned b) { return cmp(a, b) < 0; }
    friend bool operator<(mpz const & a, int b) { return cmp(a, b) < 0; }
    friend bool operator<(unsigned a, mpz const & b) { return cmp(b, a) > 0; }
    friend bool operator<(int a, mpz const & b) { return cmp(b, a) > 0; }

    friend bool operator>(mpz const & a, mpz const & b) { return cmp(a, b) > 0; }
    friend bool operator>(mpz const & a, unsigned b) { return cmp(a, b) > 0; }
    friend bool operator>(mpz const & a, int b) { return cmp(a, b) > 0; }
    friend bool operator>(unsigned a, mpz const & b) { return cmp(b, a) < 0; }
    friend bool operator>(int a, mpz const & b) { return cmp(b, a) < 0; }

    friend bool operator<=(mpz const & a, mpz const & b) { return cmp(a, b) <= 0; }
    friend bool operator<=(mpz const & a, unsigned b) { return cmp(a, b) <= 0; }
    friend bool operator<=(mpz const & a, int b) { return cmp(a, b) <= 0; }
    friend bool operator<=(unsigned a, mpz const & b) { return cmp(b, a) >= 0; }
    friend bool operator<=(int a, mpz const & b) { return cmp(b, a) >= 0; }

    friend bool operator>=(mpz const & a, mpz const & b) { return cmp(a, b) >= 0; }
    friend bool operator>=(mpz const & a, unsigned b) { return cmp(a, b) >= 0; }
    friend bool operator>=(mpz const & a, int b) { return cmp(a, b) >= 0; }
    friend bool operator>=(unsigned a, mpz const & b) { return cmp(b, a) <= 0; }
    friend bool operator>=(int a, mpz const & b) { return cmp(b, a) <= 0; }

    friend bool operator==(mpz const & a, mpz const & b) { return cmp(a, b) == 0; }
    friend bool operator==(mpz const & a, unsigned b) { return cmp(a, b) == 0; }
    friend bool operator==(mpz const & a, int b) { return cmp(a, b) == 0; }
    friend bool operator==(unsigned a, mpz const & b) { return cmp(b, a) == 0; }
    friend bool operator==(int a, mpz const & b) { return cmp(b, a) == 0; }

    friend bool operator!=(mpz const & a, mpz const & b) { return cmp(a, b) != 0; }
    friend bool operator!=(mpz const & a, unsigned b) { return cmp(a, b) != 0; }
    friend bool operator!=(mpz const & a, int b) { return cmp(a, b) != 0; }
    friend bool operator!=(unsigned a, mpz const & b) { return cmp(b, a) != 0; }
    friend bool operator!=(int a, mpz const & b) { return cmp(b, a) != 0; }

    mpz & operator+=(mpz const & o);
    mpz & operator+=(unsigned u);
    mpz & operator+=(int u);
    mpz & operator+=(uint64 u) { return u > std::numeric_limits<unsigned>::max() ? *this += mpz(u) : *this += static_cast<unsigned>(u); }

    mpz & operator-=(mpz const & o);
    mpz & operator-=(unsigned u);
    mpz & operator-=(int u);
    mpz & operator-=(uint64 u) { return u > std::numeric_limits<unsigned>::max() ? *this -= mpz(u) : *this -= static_cast<unsigned>(u); }

    mpz & operator*=(mpz const & o);
    mpz & operator*=(unsigned u);
    mpz & operator*=(int u);
    mpz & operator*=(uint64 u) { return u > std::numeric_limits<unsigned>::max() ? *this *= mpz(u) : *this *= static_cast<unsigned>(u); }

    mpz & operator/=(mpz const & o);
    mpz & operator/=(unsigned u);
    mpz & operator/=(uint64 u) { return u > std::numeric_limits<unsigned>::max() ? *this /= mpz(u) : *this /= static_cast<unsigned>(u); }
    mpz & operator/=(int u) { return operator/=(mpz(u)); } // TODO(Leo): improve

    mpz & operator%=(mpz const & o);
    friend mpz rem(mpz const & a, mpz const & b) { mpz r(a); return r %= b; }

    mpz pow(unsigned int exp) const;
    /** \brief Modular exponentiation: returns `this^exp mod m`. Requires `m != 0`. */
    mpz powm(mpz const & exp, mpz const & m) const;

    friend mpz operator+(mpz a, mpz const & b) { return a += b; }
    friend mpz operator+(mpz a, unsigned b)  { return a += b; }
    friend mpz operator+(mpz a, uint64 b)  { return a += b; }
    friend mpz operator+(mpz a, int b)  { return a += b; }
    friend mpz operator+(unsigned a, mpz b) { return b += a; }
    friend mpz operator+(uint64 a, mpz b) { return b += a; }
    friend mpz operator+(int a, mpz b) { return b += a; }

    friend mpz operator-(mpz a, mpz const & b) { return a -= b; }
    friend mpz operator-(mpz a, unsigned b) { return a -= b; }
    friend mpz operator-(mpz a, uint64 b) { return a -= b; }
    friend mpz operator-(mpz a, int b) { return a -= b; }
    friend mpz operator-(unsigned a, mpz b) { b.neg(); return b += a; }
    friend mpz operator-(uint64 a, mpz b) { b.neg(); return b += a; }
    friend mpz operator-(int a, mpz b) { b.neg(); return b += a; }

    friend mpz operator*(mpz a, mpz const & b) { return a *= b; }
    friend mpz operator*(mpz a, unsigned b) { return a *= b; }
    friend mpz operator*(mpz a, uint64 b) { return a *= b; }
    friend mpz operator*(mpz a, int b) { return a *= b; }
    friend mpz operator*(unsigned a, mpz b) { return b *= a; }
    friend mpz operator*(uint64 a, mpz b) { return b *= a; }
    friend mpz operator*(int a, mpz b) { return b *= a; }

    friend mpz operator/(mpz a, mpz const & b) { return a /= b; }
    friend mpz operator/(mpz a, unsigned b) { return a /= b; }
    friend mpz operator/(mpz a, uint64 b) { return a /= b; }
    friend mpz operator/(mpz a, int b) { return a /= b; }
    friend mpz operator/(unsigned a, mpz const & b) { mpz r(a); return r /= b; }
    friend mpz operator/(uint64 a, mpz const & b) { mpz r(a); return r /= b; }
    friend mpz operator/(int a, mpz const & b) { mpz r(a); return r /= b; }

    friend mpz operator%(mpz a, mpz const & b) { return a %= b; }

    static mpz divexact(mpz const & n, mpz const & d);

    static mpz ediv(mpz const & n, mpz const & d);
    static mpz ediv(int n, mpz const & d) { return ediv(mpz(n), d); }
    static mpz ediv(mpz const& n, int d) { return ediv(n, mpz(d)); }

    static mpz emod(mpz const & n, mpz const & d);
    static mpz emod(int n, mpz const & d) { return emod(mpz(n), d); }
    static mpz emod(mpz const & n, int d) { return emod(n, mpz(d)); };

    mpz & operator&=(mpz const & o);
    mpz & operator|=(mpz const & o);
    mpz & operator^=(mpz const & o);

    friend mpz operator&(mpz a, mpz const & b) { return a &= b; }
    friend mpz operator|(mpz a, mpz const & b) { return a |= b; }
    friend mpz operator^(mpz a, mpz const & b) { return a ^= b; }

    // a <- b * 2^k
    friend void mul2k(mpz & a, mpz const & b, unsigned k);
    // a <- b / 2^k
    friend void div2k(mpz & a, mpz const & b, unsigned k);

    uint8 mod8() const;
    uint16 mod16() const;
    uint32 mod32() const;
    uint64 mod64() const;

    int8 smod8() const;
    int16 smod16() const;
    int32 smod32() const;
    int64 smod64() const;

    /**
       \brief Return the position of the most significant bit.
       Return 0 if the number is negative
    */
    size_t log2() const;

    /**
       \brief Return an upper bound on the size in bytes of the representation,
       i.e. the number of limbs times the bytes per limb. Used to bound memory usage.
    */
    size_t size_in_bytes() const;

    friend void power(mpz & a, mpz const & b, unsigned k);
    friend void _power(mpz & a, mpz const & b, unsigned k) { power(a, b, k); }
    friend mpz pow(mpz a, unsigned k) { power(a, a, k); return a; }

    friend void gcd(mpz & g, mpz const & a, mpz const & b);
    friend mpz gcd(mpz const & a, mpz const & b) { mpz r; gcd(r, a, b); return r; }

    LEAN_EXPORT friend std::ostream & operator<<(std::ostream & out, mpz const & v);

    std::string to_string() const;
};

struct mpz_cmp_fn {
    int operator()(mpz const & v1, mpz const & v2) const { return cmp(v1, v2); }
};
}

#ifndef LEAN_USE_GMP
#include <atomic>
#include <cstdint>

/* CATCRYPT VERIFIED MPN: u32-digit entry points of the catcrypt-mpn static
   archive (libcatcrypt_mpn.a), backed by u64 kernels emitted by the CatCrypt
   compiler; see CATCRYPT_MPN_TCB.md. Lengths are in u32 limbs. In every entry
   the output must not overlap an input; inputs may overlap each other; a
   pointer may be null only when its length is zero. */
extern "C" {
/* op[0..n) = ap[0..n) op bp[0..n) mod 2^(32 n); carry/borrow dropped. */
void mpn_add_n_oc_32(unsigned int * op, unsigned int const * ap, unsigned int const * bp, uint64_t n);
void mpn_sub_n_oc_32(unsigned int * op, unsigned int const * ap, unsigned int const * bp, uint64_t n);
/* op[i] = ap[i] & | ^ bp[i] for i < n. */
void mpn_and_n_oc_32(unsigned int * op, unsigned int const * ap, unsigned int const * bp, uint64_t n);
void mpn_ior_n_oc_32(unsigned int * op, unsigned int const * ap, unsigned int const * bp, uint64_t n);
void mpn_xor_n_oc_32(unsigned int * op, unsigned int const * ap, unsigned int const * bp, uint64_t n);
/* Three-way comparison of the values ap[0..an) and bp[0..bn): 1, 0 or -1. */
int mpn_cmp_oc_32(unsigned int const * ap, uint64_t an, unsigned int const * bp, uint64_t bn);
/* rp[0 .. an + k/32 + 1) = ap[0..an) * 2^k, unnormalized. */
void mpn_lshift_oc_32(unsigned int * rp, unsigned int const * ap, uint64_t an, uint64_t k);
/* rp[0 .. an - k/32) = floor(ap[0..an) / 2^k), unnormalized; writes nothing
   when k/32 >= an. */
void mpn_rshift_oc_32(unsigned int * rp, unsigned int const * ap, uint64_t an, uint64_t k);
/* rp[0 .. an + bn) = ap[0..an) * bp[0..bn), unnormalized. */
void mpn_mul_oc_32(unsigned int * rp, unsigned int const * ap, uint64_t an,
                   unsigned int const * bp, uint64_t bn);
}

namespace lean {
/* CATCRYPT VERIFIED MPN: per-operation call counters. `routed` counts calls
   that reach the archive, `legacy` calls that take the runtime's own digit
   loop. Counting is enabled when the environment variable
   LEAN_CATCRYPT_MPN_STATS is set at startup; the totals are printed to stderr
   at process exit (atexit). When disabled, a call costs one load of
   g_catcrypt_mpn_stats. Defined in mpn.cpp. */
enum catcrypt_mpn_op {
    CATCRYPT_MPN_ADD, CATCRYPT_MPN_SUB, CATCRYPT_MPN_CMP, CATCRYPT_MPN_MUL,
    CATCRYPT_MPN_AND, CATCRYPT_MPN_IOR, CATCRYPT_MPN_XOR,
    CATCRYPT_MPN_LSHIFT, CATCRYPT_MPN_RSHIFT,
    CATCRYPT_MPN_NUM_OPS
};
extern bool g_catcrypt_mpn_stats;
extern std::atomic<uint64_t> g_catcrypt_mpn_routed[CATCRYPT_MPN_NUM_OPS];
extern std::atomic<uint64_t> g_catcrypt_mpn_legacy[CATCRYPT_MPN_NUM_OPS];

inline void catcrypt_mpn_count(catcrypt_mpn_op op, bool routed) {
    if (LEAN_UNLIKELY(g_catcrypt_mpn_stats)) {
        (routed ? g_catcrypt_mpn_routed : g_catcrypt_mpn_legacy)[op]
            .fetch_add(1, std::memory_order_relaxed);
    }
}
}
#endif
