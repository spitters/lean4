# CatCrypt verified `mpn`: trust-base accounting

This branch is Lean v4.35.0 with a patch series to the non-GMP runtime. In a
`USE_GMP=OFF` build, `src/runtime/mpn.cpp` and the non-GMP branch of
`src/runtime/mpz.cpp` call x86-64 limb kernels emitted by the CatCrypt compiler
([catcrypt-compiler](https://github.com/spitters/catcrypt-compiler)) for most
of the bignum digit arithmetic behind `Nat` and `Int`. The runtime's digits
(`mpn_digit`, `mpn.h`) are 64 bits wide, the kernels' limb width, where
upstream's non-GMP runtime uses 32-bit digits; the runtime therefore passes its
own digit arrays to the kernels. A build with `USE_GMP=ON`, the upstream default, is unaffected.
This note states which operations run on the kernels, what code sits between
the runtime and the kernels, and what that code adds to the trusted computing
base. Lean module names below are relative to
`CatCrypt/Crypto/SecureCompilation/` in catcrypt-compiler.

## Operations

| Runtime operation | Kernel |
|---|---|
| `mpn_add` | `mpn_add_n_loop_owned` |
| `mpn_sub` | `mpn_sub_n_loop_owned` |
| `mpn_compare` (equal lengths) | `mpn_cmp_ct` |
| `mpn_mul` | `mpn_mul_basecase_rt` |
| `mpz::operator&=` (both operands ≥ 0) | `mpn_and_n_loop_owned` |
| `mpz::operator\|=` (both operands ≥ 0) | `mpn_ior_n_loop_owned` |
| `mpz::operator^=` (both operands ≥ 0) | `mpn_xor_n_loop_owned` |
| `mul2k` (operand ≥ 0, shift not a multiple of 64) | `mpn_shift_rt` |
| `div2k` (operand ≥ 0, shift not a multiple of 64) | `mpn_shift_rt` |

`mpn_div` keeps the runtime's own code (Knuth's Algorithm D). For a divisor of
two or more digits, its multiply-and-subtract step and its add-back correction
call `mpn_mul` (one digit by the divisor), `mpn_sub` and `mpn_add`, so those
steps run on the kernels; the normalization shift, the quotient-digit estimate
(a 128-bit double digit, `unsigned __int128`, divided by the top divisor digit)
and the unnormalization are C. For a one-digit divisor, `mpn_div` calls no
kernel. `mpn_to_string` keeps the runtime's code. The bitwise operators
and the shifts take the runtime's digit loops when an operand is negative; in
the runtime these operators are reached only from `Nat` functions
(`lean_nat_big_land`, `lean_nat_big_lor`, `lean_nat_big_xor`,
`lean_nat_shiftl`, `lean_nat_big_shiftr`) and, for `div2k`, from the exponent
shift in `mpz::powm`, and all of these operands are non-negative.

Setting `LEAN_CATCRYPT_MPN_STATS` in the environment makes the runtime count,
per operation, the calls that reach a kernel and the calls that take the
runtime's digit loop, and print both totals to stderr at exit.

### Modular exponentiation

Lean v4.35.0 implements `Nat.powMod` in the runtime: `lean_nat_powmod`
(`object.cpp`) passes a zero modulus to `lean_nat_pow` and otherwise calls
`mpz::powm`, also for operands that fit in a machine word. The non-GMP
`mpz::powm` (`mpz.cpp`) is upstream's right-to-left square-and-multiply loop,
unchanged by this patch series. Its steps reach the kernels as follows:

- `result *= base` and `base *= base`: `mpz::mul`, then `mpn_mul`, hence
  `mpn_mul_basecase_rt`;
- `base %= m` and `result %= m`: `mpz::rem`, which returns at once when the
  dividend has fewer digits than `m` and otherwise calls `mpn_div`; for a
  modulus of two or more digits that division runs on `mpn_mul_basecase_rt`,
  `mpn_sub_n_loop_owned` and `mpn_add_n_loop_owned` as described above, for a
  one-digit modulus on C only;
- `div2k(e, e, 1)`, the exponent shift: `mpn_shift_rt`, a funnel shift by 63;
- `m == 1`: `cmp(mpz, int)`, then `mpn_compare`, hence `mpn_cmp_ct` when `m`
  has one digit.

The loop control (`is_zero`, and `mod8` for the low exponent bit), the parts of
`mpn_div` that are C, and the copies and allocations of the temporaries are not
kernels. The kernel calls of a `powm` are counted under `mul`, `sub`, `add`,
`compare` and `rshift` in the `LEAN_CATCRYPT_MPN_STATS` output.

## Kernel provenance

The kernels are ELF objects in `catcrypt/`, archived into `libleanrt` by
`CMakeLists.txt`; `catcrypt/README.md` lists each object's symbol, generator
module and SHA-256. Every object is the output of CatCrypt's byte encoder
(`X86Backend/LprogBytes.encodeLprogChecked`) on a front-compiled `Lprog`;
neither an assembler nor `jasminc` is on their artifact path.

The five equal-length loops (`add`, `sub`, `and`, `ior`, `xor`) come from
`MpnLoopBytesEmit`, the three runtime-trip kernels from `MpnRuntimeBytesEmit`,
from the unoptimized `toLprogPhysAbi` program. What is proved differs per
kernel:

- `and`, `ior`, `xor`, shift and compare: a machine-level (`lsem`) loop built
  from the front-compiled loop body, `loopLinstrRM … (loopBodyArrFullMN
  update …)`, computes the word-level contract (`MpnLogicalLsemTie`,
  `MpnShiftLsemTie`, `MpnCmpLsemTie`, discharged in the
  `JasminLsemMemArrTrip*` modules). That loop is a model of the emitted
  kernel rather than the encoded program; see "The model loop and the encoded
  program" below for what stands between them;
- multiply: the compiled program's denotation is the schoolbook product
  (`MpnMulBasecaseFrontDenote.bcRtProg_val`); no theorem connects the machine
  execution of the nested loop to that denotation;
- add and sub: the loops are compiled by the front from covered `LowCT`
  (`MpnLoopFrontEmit.addLprogFront`, `subLprogFront`). `MpnAddSubLsemTie`
  gives the value of the carry (borrow) accumulator at the machine loop's halt
  state, and `MpnAddSubOutFrame` gives what that loop leaves in its invariant
  array slots, but no theorem states that the compiled loop writes the sum or
  the difference to the output array. That statement cannot be made the way the
  bitwise kernels make theirs: the array export's accumulator IS the output
  array, while the carry family returns a register because the ABI returns the
  carry in `RAX`, so `ArrMemN addUpdate` is ill-typed rather than unproved. The
  compiler's `X86Backend/MpnAddSubFrontDenote` proves carry and borrow folds and
  per-trip output writes (`addLimbBody_run`) for the programs `addRtProgOpt` and
  `subRtProgOpt`; no theorem identifies those programs with the shipped
  `addLprogFront` and `subLprogFront`. The output limbs of the shipped objects are
  checked by the differential harness only.

## The model loop and the encoded program

The links between the `lsem` model loop and the physical-ABI `Lprog` that the
byte encoder encodes are individually proved, and the chain is not assembled.

Proved: the soundness side condition for the canonicalisation pass is
`ThreadCert`'s second clause (`VIR/JasminLsemExportCanon.lean`) — every array
reference the pass sends to the output must go through the current thread head,
which is what `canonicalizeMemrefThread_threadCmd_run`'s counterexample
violates; soundness under it is `lsemStraightMem_thread`, lifted by
`canonNodeRM_faithful`; register renaming is `lsemFuelMemF_live` with
`liveCert_of_ranges` and `physSigma_interf`.

Not assembled, for one reason: the exporter's node and the canon node are
related by a list equality whose precondition `ComRenOK` is **false** at the
and-body. The front emits the array-index scaling as `shlImm 64 0` — a shift by
zero, for a unit scale — and at an immediate shift the model chain and the
export body emit different instructions (`immShift_wire_ne`). Behavioural
agreement does not repair a list equality, so the route is to state the
exporter's node as the expansion image of the canon node and transport the run
with `lsemFuelMemF_expand` under `ComOpsPlain`, which holds for the and-body.

Two further links are untouched: the two-address lowering with its R15/R14
stack-slot routing, and the exporter term itself — the `toLprogPhysAbi_*`
family quantifies over a free `Lcmd` body under a hypothesis of the shape
`body = bindingHeads …` and never takes its pieces from the exporter.

One referent question is open. `and_canon_machine_val` is about `andUpdate`
from `MpnLogicalFrontEmit.logicBodyLowCT` at `NL = 8` with slots `rp`/`ap`/`bp`;
`MpnLoopFrontEmit.binLprog "mpn_and_n_loop_owned"` is a separate definition at
`N = 4` with slots `out`/`a`/`b`. Same shape, different terms, and no theorem
ties them. `MpnLoopBytesEmit.kernels` maps the linked symbol to
`MpnLoopFrontEmit.andLprog`, the second term.
`VIR/JasminLsemExportCanonLogicShipped.lean` instantiates
`canonNodeRM_faithful` at that body (`binAnd_canon_machine_denote`) and
concludes at the body's `JLoopBodyN.denote`; the identification of that
denotation with the elementwise conjunction (the fold identification of
`binBody`) is not proved, so the and-vertical does not yet reach the shipped
kernel's word-level contract.

## The instruction semantics

The kernels use BMI2 (`shlx`, `shrx`, `mulx`), so the runtime needs a CPU with
BMI2, and write below their adjusted stack pointer inside the System V red
zone. The runtime-trip kernels leave the final loop index in `RAX`; the runtime
ignores that register and reads results through the output pointer.

The model's instruction semantics is anchored to external specifications where
one exists. `X86Backend/SailX86Lean.lean` proves agreement with the
ACL2-derived Sail model (`rems-project/sail-x86-from-acl2`) for `add`, `sub`,
`adc`, `sbb`, `and`, `or`, `xor`, and with SDM-transcribed Sail for `mulx`,
`adcx` and `adox`. That model defines no BMI2 variable shift at all;
`X86Backend/SailX86BmiShift.lean` anchors `shlx`, `shrx` and `sarx` to Intel's
own executable specification (`github.com/intel/SDM`, `xml92/`), proving the
data word for every operand pair and the no-flag-write property over an
arbitrary state. The legacy flag-writing `SHL`/`SHR`/`SAR` and `MUL`/`IMUL`
have no external anchor; `SailX86Validation.lean` checks them as recorded
numbers only.

## Differential evidence

Beyond the harness in `catcrypt/harness` (see "Testing"), the runtime as a
whole was differential-tested against the GMP build on the v4.33.1 base of this
patch series: the same computation run under `USE_GMP=OFF` and under stock
Lean, with the outputs compared. The workload covers add and subtract at
`fib 3000` width, sixty modular squarings at 4096 bits, division and remainder
of `fib 9001` by a multi-limb divisor, `ble` and `beq`, shifts and all three
bitwise operations across a limb boundary, `gcd` of two multi-limb operands, a
cube through `pow`, and `log2`. The outputs are identical. On the v4.35.0 base,
big-`Nat` arithmetic computed by the stage-1 toolchain matched the same
computation in Python.

## Code between the runtime and the kernels

All of it is trusted and none of it is verified. It is C++ in `mpn.cpp` and
`mpz.cpp`, together with the toolchain and system libraries in the last item.

- `mpn_add`/`mpn_sub`: copy both operands, zero-extended to `len + 1` digits,
  into scratch (1024 digits of inline storage in a `buffer`, heap above that;
  there is no length limit), run the kernel into scratch, copy the result out
  (so the in-place calls in `mpn_div` are safe) and normalize the length.
- `mpn_compare`: both operands are normalized `mpz` digit arrays (asserted in
  debug builds); different lengths are ordered by length, equal lengths go to
  `mpn_cmp_ct`, whose result cell is decoded as `1`, `0` or `2^64 - 1`, any other
  value panics.
- `mpn_mul`: an empty operand gives a zero product; otherwise the kernel writes
  `lnga + lngb + 1` zeroed scratch digits, the top one is checked to be zero,
  and the product is copied out.
- `mpz.cpp`: the sign tests that select a kernel; for the bitwise operators the
  zero or copy extension beyond the shorter operand; for `mul2k` the zero fill
  of the whole-digit part and a copy for shifts by a multiple of 64; for
  `div2k` the digit offset, a copy for shifts by a multiple of 64, and the
  reading of the funnel shift by `64 - k % 64`; and the output lengths passed
  to `set()`, which normalizes as before.
- The C++ compiler for these files, the system linker, libc (`memcpy`,
  `memset`, `malloc`/`free` through the runtime's allocator), and the compiler
  runtime's 128-bit division `__udivmodti4`, which `mpn_div` calls (see
  "Limits").

## Removed from the trust base

Relative to the first commits of this patch series, which linked 32-bit-limb
kernels through a Rust static library, and to upstream's non-GMP digit loops:

- `jasminc` and the earlier u32 emission path.
- The GNU assembler (all eight objects come from the byte encoder).
- The Rust seam: the u32↔u64 packing, `rustc`, and its libc and compiler-rt
  dependencies. The runtime calls the kernels directly. (The 64-bit digits
  bring in `__udivmodti4` for `mpn_div`, listed above.)
- The runtime's hand-written digit loops for the operations in the table
  (for the bitwise operators and shifts, on non-negative operands).

## Testing

The differential harness in `src/runtime/catcrypt/harness` tests `mpn.cpp` and
`mpz.cpp` on their own; it needs a C++20 compiler, `make`, `git` and the tag
`v4.35.0` in the clone (`git fetch origin tag v4.35.0`), and no Lean build. It
compiles this tree's two files with the kernel objects (64-bit digits) into one
binary and, as the oracle, upstream's two files at `v4.35.0` (32-bit digits, no
kernels, extracted with `git show`) into another. Both binaries run the same
deterministic cases on operands generated as 32-bit words and print one hash
per result value; the outputs must be byte-identical.

```
make -C src/runtime/catcrypt/harness check          # 20000 mpn + 20000 mpz cases up to 300 words,
                                                    # then 40 + 40 cases up to 6000 words
make -C src/runtime/catcrypt/harness check DEBUG=1  # the same with LEAN_DEBUG assertions
make -C src/runtime/catcrypt/harness stats          # kernel and digit-loop call counts of a short run
make -C src/runtime/catcrypt/harness dump CASE=<n>  # operands and results of one case, both binaries
```

`check` ends with `PASS:` and the number of compared result lines; a
difference prints the first differing lines and fails. The cases cover
`mpn_add`, `mpn_sub` (also in place), `mpn_compare`, `mpn_mul`, `mpn_div`, and
the `mpz` operators (`&=`, `|=`, `^=` with both signs, `mul2k`, `div2k`, `*`,
`+`, `-`, `/`, `%`, `cmp`, `log2`, the small-integer conversions and operators,
`to_string` and parsing), with operands up to 6000 words, and `mpz::powm`. A
`powm` case draws a modulus that is 1, one word, or up to 16 words (128 words,
that is 4096 bits, in the 6000-word run), a base of up to twice the modulus
length, and an exponent of up to 2 words (16 words, 512 bits, in the 6000-word
run), with zero bases and exponents included. Each binary prints `mpz::powm`,
the harness's own left-to-right square-and-multiply over `*` and `%`
(`harness_powm`), and whether the two agree. The patched `mpz::powm` is
therefore compared with upstream's `powm` and with a second exponentiation
order, both computed by upstream's 32-bit digit code.

## Building

The non-GMP toolchain builds as upstream Lean does, with `USE_GMP=OFF`, on
x86-64 Linux:

```
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_GMP=OFF
make -C build/release -j"$(nproc)"
elan toolchain link catcrypt-mpn build/release/stage1
```

A project then selects the toolchain with `catcrypt-mpn` as the content of its
`lean-toolchain` file, or with `elan override set catcrypt-mpn`. Running a
compiled program with `LEAN_CATCRYPT_MPN_STATS=1` in the environment prints the
kernel call counts at exit.

## Limits

- The glue is trusted, not verified.
- The machine-level theorems describe a model loop, not the encoded `Lprog`.
  Every link between the two is proved and the chain is not assembled; the
  obstacle is the `shlImm _ 0` wire disagreement, not a missing lemma. The
  multiply kernel is verified to the program denotation only, and the add and
  sub loops carry a carry-register and a frame theorem but no output-limb value
  theorem (see above).
- `mpn_div` and `mpn_to_string` remain the runtime's C; the quotient-digit
  estimate of `mpn_div` divides 128-bit values through the compiler runtime
  (`__udivmodti4`, from libgcc or compiler-rt). `mpz::powm` is upstream's C
  loop over the kernel-backed multiplication, division and shift.
- The non-GMP runtime builds only for x86-64 Linux (ELF objects, System V ABI).
- Compacted `.olean` files store `mpz` digits at the digit width; files written
  by a runtime with another width do not load correctly. `stage0/src/runtime`
  carries the same files, so both stages agree.
