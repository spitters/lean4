# CatCrypt verified `mpn`: trust-base accounting

In a `USE_GMP=OFF` build, `src/runtime/mpn.cpp` and the non-GMP branch of
`src/runtime/mpz.cpp` call x86-64 limb kernels emitted by the CatCrypt compiler
for most of the bignum digit arithmetic behind `Nat` and `Int`. The runtime's
digits are 64 bits wide (`mpn_digit`, `mpn.h`), the kernels' limb width, so the
runtime passes its own digit arrays to the kernels. This note states which
operations run on the kernels, what code sits between the runtime and the
kernels, and what that code adds to the trusted computing base.

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

`mpn_div` keeps the runtime's own Knuth D code, but its normalization and
correction steps call `mpn_mul`, `mpn_sub` and `mpn_add`, so those parts run on
the kernels. `mpn_to_string` keeps the runtime's code. The bitwise operators
and the shifts take the runtime's digit loops when an operand is negative; in
the runtime these operators are reached only from `Nat` functions
(`lean_nat_big_land`, `lean_nat_big_lor`, `lean_nat_big_xor`,
`lean_nat_shiftl`, `lean_nat_big_shiftr`), whose operands are non-negative.

Setting `LEAN_CATCRYPT_MPN_STATS` in the environment makes the runtime count,
per operation, the calls that reach a kernel and the calls that take the
runtime's digit loop, and print both totals to stderr at exit.

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
  output limbs are checked by the differential harness only.

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
ties them. Settle which is the shipped kernel before reading this vertical as
reaching it.

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

Beyond the harness in `catcrypt/harness`, the runtime as a whole has been
differential-tested against the GMP build: the same computation run under
`USE_GMP=OFF` and under stock Lean, with the outputs compared. The workload
covers add and subtract at `fib 3000` width, sixty modular squarings at 4096
bits, division and remainder of `fib 9001` by a multi-limb divisor, `ble` and
`beq`, shifts and all three bitwise operations across a limb boundary, `gcd` of
two multi-limb operands, a cube through `pow`, and `log2`. The outputs are
identical.

## Code between the runtime and the kernels

All of it is trusted and none of it is verified. It is C++ in `mpn.cpp` and
`mpz.cpp`; no other language or library is involved.

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
- The C++ compiler for these files, the system linker, and libc (`memcpy`,
  `memset`, `malloc`/`free` through the runtime's allocator).

## Removed from the trust base

- `jasminc` and the earlier u32 emission path.
- The GNU assembler (all eight objects come from the byte encoder).
- The Rust seam: the u32↔u64 packing, `rustc`, and its libc and compiler-rt
  dependencies. The runtime calls the kernels directly.
- The runtime's hand-written digit loops for the operations in the table
  (for the bitwise operators and shifts, on non-negative operands).

## Testing

`make -C src/runtime/catcrypt/harness check` (and `check DEBUG=1`) compiles this
directory's `mpn.cpp` and `mpz.cpp` with the kernel objects, and the v4.33.1
versions of the same files (32-bit digits) as the oracle. Both run the same
deterministic cases on operands generated as 32-bit words; results are compared
as values. It covers `mpn_add`, `mpn_sub` (also in place), `mpn_compare`,
`mpn_mul`, `mpn_div`, and the `mpz` operators (`&=`, `|=`, `^=` with both
signs, `mul2k`, `div2k`, `*`, `+`, `-`, `/`, `%`, `cmp`, `log2`, the small-integer
conversions and operators, `to_string` and parsing), with operands up to 6000
words.

## Limits

- The glue is trusted, not verified.
- The machine-level theorems describe a model loop, not the encoded `Lprog`.
  Every link between the two is proved and the chain is not assembled; the
  obstacle is the `shlImm _ 0` wire disagreement, not a missing lemma. The
  multiply kernel is verified to the program denotation only, and the add and
  sub loops carry a carry-register and a frame theorem but no output-limb value
  theorem (see above).
- `mpn_div` and `mpn_to_string` remain the runtime's C.
- The non-GMP runtime builds only for x86-64 Linux (ELF objects, System V ABI).
- Compacted `.olean` files store `mpz` digits at the digit width; files written
  by a runtime with another width do not load correctly. `stage0/src/runtime`
  carries the same files, so both stages agree.
