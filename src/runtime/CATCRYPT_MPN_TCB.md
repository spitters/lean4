# CatCrypt verified `mpn`: trust-base accounting

`src/runtime/mpn.cpp` routes the equal-length limb add and subtract —
`lean::mpn_add` and `lean::mpn_sub`, the arithmetic hot path of `Nat`/`Int`
bignum in a `USE_GMP=OFF` build — through the verified kernels
`mpn_add_n_oc_32` / `mpn_sub_n_oc_32`, linked from the `catcrypt-mpn` static
archive `libcatcrypt_mpn.a`. This note states exactly what that moves into and
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
integration (`bench/integration32/*.o`), whose u32 kernels came through
`jasminc` — an external compiler that was in the trust base and left no
correctness theorem at u32. That path, and `jasminc`, are gone.

## Trusted computing base after this change

Trusted (unchanged bignum correctness still rests on these):

- The **safe-Rust seam** (`crates/catcrypt-mpn/src/lib.rs`, `seam`/`cabi`):
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

```
cargo build --release --no-default-features \
  --manifest-path <catcrypt>/crates/catcrypt-mpn/Cargo.toml
```

produces `target/release/libcatcrypt_mpn.a`. The runtime CMake links it via the
`catcrypt_mpn` imported target (`src/runtime/CMakeLists.txt`); override the path
with `-DCATCRYPT_MPN_LIB=`. `cargo test` (default `std` feature) runs the KAT
harness; the archive the runtime links is the `no_std` `--no-default-features`
build.
