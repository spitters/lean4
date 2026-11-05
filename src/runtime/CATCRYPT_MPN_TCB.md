# CatCrypt verified `mpn`: trust-base accounting

`src/runtime/mpn.cpp` routes the equal-length limb add and subtract —
`lean::mpn_add` and `lean::mpn_sub`, the arithmetic hot path of `Nat`/`Int`
bignum in a `USE_GMP=OFF` build — through the verified kernels
`mpn_add_n_oc_32` / `mpn_sub_n_oc_32`, linked from the `catcrypt-mpn` static
archive `libcatcrypt_mpn.a`, when the runtime is configured with
`-DUSE_GMP=OFF -DCATCRYPT_MPN=ON` (see Build below). This note states exactly what that moves into and
out of the trusted computing base.

## What runs

`mpn_add`/`mpn_sub` zero-extend their two inputs to `m+1` equal-length u32
limbs (capturing the carry/borrow in the top limb) and call the `_oc_32`
kernel; the remaining `mpn` operations (`mpn_mul`, `mpn_div`, `mpn_compare`,
`mpn_to_string`) keep the runtime's legacy C.

Inside the archive, `mpn_{add,sub}_n_oc_32` are a small **safe-Rust seam** over
the u64 limb-loop kernels `mpn_{add,sub}_n_loop_owned`: the seam packs the u32
limbs into u64 pairs, calls the u64 kernel, recovers the carry/borrow, and
normalizes the length. The u64 kernels are the artifact the CatCrypt compiler
emits.

## Kernel provenance — verified, not trusted

The u64 add/sub limb loops are **front-compiled** in Lean
(`LowCT` → `catCryptDialect` → jasmin `Com` → `Lprog`, no hand-authored IR)
and turned into ELF object bytes by CatCrypt's **own translation-validated byte
encoder** (`X86Backend/LprogBytes.encodeLprogChecked`): no external assembler
and **no `jasminc`** on the artifact path. The byte encoder's fragment
membership is machine-checked at emit time. This is the change from the prior
integration, whose u32 kernel objects came through
`jasminc` — an external compiler that was in the trust base and left no
correctness theorem at u32. That path, and `jasminc`, are gone.

## Trusted computing base after this change

Trusted (unchanged bignum correctness still rests on these):

- The **safe-Rust seam** (`crates/catcrypt-mpn/src/lib.rs` of
  [catcrypt-compiler](https://github.com/spitters/catcrypt-compiler), `seam`/`cabi`):
  u32↔u64 packing, carry/borrow capture, and the normalization scan. Ordinary
  data-independent code, `#![deny(unsafe_code)]` except the two FFI islands,
  differential- and vector-tested (`tests/kat.rs`, 9 cases).
- The `extern "C"` ABI between `mpn.cpp` and the archive, and the zero-extend
  glue in `mpn.cpp` itself.
- `rustc` (for the seam glue only), the system assembler and linker.
- libc `memcpy`/`memset`/`abort` and the compiler-rt integer/float intrinsics
  the C toolchain already provides. The archive is built `no_std`
  (`--no-default-features`), so it pulls **no Rust std runtime** into the final
  link — its only undefined symbols are those primitives.
- Unchanged and still trusted: `mpn_mul`, `mpn_div`, `mpn_compare`,
  `mpn_to_string` (the runtime's legacy C).

Removed from the trust base by this change:

- `jasminc` (the external Jasmin compiler) and the u32 emission path that had no
  correctness theorem.
- The hand-written C inner loops of `mpn_add`/`mpn_sub` that a stock
  `USE_GMP=OFF` build would use.

## Not yet verified (honest limits)

- The seam and the `mpn.cpp` zero-extend glue are trusted, not verified. Each
  seam step is its own function so a later verified-kernel replacement swaps one
  call site; the u64 kernels are already the verified artifact.
- The kernels are verified at **u64** limb width; the u32 mpn surface is served
  by presenting them through the seam, not by a natively-u32 verified kernel.
  Native u32 correctness (`Add32Refines` over `BitVec 32`) is a separate,
  larger task.
- `mpn_mul`/`mpn_div`/`mpn_compare` are unaffected and remain trusted C.

## Build

The kernels are off by default; a stock `USE_GMP=OFF` build keeps the
runtime's own add/sub loops. To select them, build the archive `no_std` in the
crate `crates/catcrypt-mpn` of
[catcrypt-compiler](https://github.com/spitters/catcrypt-compiler):

```
cargo build --release --no-default-features --manifest-path <catcrypt-compiler>/crates/catcrypt-mpn/Cargo.toml
```

which writes `target/release/libcatcrypt_mpn.a`, and configure Lean with

```
cmake --preset release -DUSE_GMP=OFF -DCATCRYPT_MPN=ON -DCATCRYPT_MPN_LIB=<catcrypt-compiler>/crates/catcrypt-mpn/target/release
make -C build/release -j$(nproc)
```

`CATCRYPT_MPN_LIB` is the directory holding `libcatcrypt_mpn.a`; configuring
with `CATCRYPT_MPN=ON` stops with an error when it is missing, when the
directory holds no archive, or when `USE_GMP` is `ON`. The archive is copied to
`lib/lean/libcatcrypt_mpn.a` of each stage and linked as `-lcatcrypt_mpn` into
`libleanshared`, the toolchain executables and user binaries built by `leanc`.
`cargo test` (default `std` feature) in the crate runs its KAT and differential
tests; the archive the runtime links is the `no_std` build.
