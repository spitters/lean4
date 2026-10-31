# CatCrypt limb kernels

ELF64 relocatable objects, x86-64 System V, 64-bit limbs. The non-GMP runtime
archives them into `libleanrt` (see `../CMakeLists.txt`) and calls them from
`../mpn.cpp` and `../mpz.cpp`; the declarations and contracts are in `../mpz.h`,
the trust accounting in `src/runtime/CATCRYPT_MPN_TCB.md`. That path, and the
harness path below, are from the repository root, because
`stage0/src/runtime/catcrypt/` carries a copy of this file and of the objects.

Each object is the output of the CatCrypt byte encoder
(`X86Backend/LprogBytes.encodeLprogChecked` in
[catcrypt-compiler](https://github.com/spitters/catcrypt-compiler)) on a
front-compiled `Lprog`; no assembler produced them. The objects have no
relocations and define one global symbol each. They are unchanged by the
rebase of this patch series onto Lean v4.35.0.

| object | symbol | generator (catcrypt-compiler) | sha256 |
|---|---|---|---|
| `mpn_add_n_loop_owned.o` | `mpn_add_n_loop_owned` | `MpnLoopBytesEmit` | `3235324f95cb2f944a438198176d5c531587e5da3bbcdd1eb97f0ee902d6fe03` |
| `mpn_sub_n_loop_owned.o` | `mpn_sub_n_loop_owned` | `MpnLoopBytesEmit` | `b97bd9cff5b3b015038b855303030bc7d4aa8b690e4cafdaea4915877601d4f6` |
| `mpn_and_n_loop_owned.o` | `mpn_and_n_loop_owned` | `MpnLoopBytesEmit` | `1147a113b6d2aaf97223771f9a55e55244ef6950a1827ec37f8dddf3bb69dfbc` |
| `mpn_ior_n_loop_owned.o` | `mpn_ior_n_loop_owned` | `MpnLoopBytesEmit` | `3049338891028a98687678d83697cfd562b2b2135876f0d5f640c5915482420e` |
| `mpn_xor_n_loop_owned.o` | `mpn_xor_n_loop_owned` | `MpnLoopBytesEmit` | `e849ec3786232615af75019af177beb1c1528bdfab11b0cea5b2835fa7eaef07` |
| `mpn_shift_rt_owned.o` | `mpn_shift_rt` | `MpnRuntimeBytesEmit` | `3d92ad3f82bc39437d91937ad18c8cb54ebf027f652f9558b0d4d3a74d93bbfb` |
| `mpn_cmp_ct_owned.o` | `mpn_cmp_ct` | `MpnRuntimeBytesEmit` | `14d252f6a53555014bf15129cbe7c9a68f385bde933da48759346a4f1c38f0ea` |
| `mpn_mul_basecase_rt_owned.o` | `mpn_mul_basecase_rt` | `MpnRuntimeBytesEmit` | `3904b98a72300d7170466cbf96f1ac677cc8a338c70d53ed8959b760b18a7f90` |

Generator modules live under `CatCrypt/Crypto/SecureCompilation/X86Backend/` in
catcrypt-compiler (commit `912f105` for `MpnRuntimeBytesEmit`).
`MpnLoopBytesEmit.emitMain` writes the five equal-length loops and
`MpnRuntimeBytesEmit.emitTo <dir>` the three runtime-trip kernels; both fail
rather than write an object when a kernel lies outside the encoder's fragment.
The SHA-256 column identifies the objects linked here.

The kernels use BMI2 (`mulx`, `shlx`, `shrx`) and write below their adjusted
stack pointer, inside the System V red zone.

`src/runtime/catcrypt/harness` holds a differential test of `src/runtime/mpn.cpp`
and `src/runtime/mpz.cpp` with these objects against upstream's v4.35.0
versions of the two files; `make -C src/runtime/catcrypt/harness check` runs it
(details in `src/runtime/CATCRYPT_MPN_TCB.md`, "Testing").
