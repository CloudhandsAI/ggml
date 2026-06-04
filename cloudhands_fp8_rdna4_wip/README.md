# fp8 coopmat mul_mm on RDNA4 — WIP / reproduction artifacts

Measured 2026-06-03 on a Radeon AI PRO R9700 (gfx1201, RADV/Mesa 25.2.8). These are
work-in-progress reproduction artifacts for an fp8 matmul path in ggml-vulkan; not yet a
finished integration (no `GGML_TYPE_E4M3` / dispatch yet — see "Remaining" below).

## Result

A properly-built fp8 matmul is **2.19× faster than ggml's shipped f16 path** on RDNA4,
fully correct. Driving ggml's *actual* `mul_mm.comp` (coopmat path, AMD-large warptile),
m=4096 n=512 k=14336, output verified (D[0]=14336, zero unwritten cells):

| config | TFLOPS | vs f16-aligned |
|---|---|---|
| f16-aligned (what ggml ships) | 26.8 | 1.00× |
| fp8, f16 weights converted (naive) | 16.8 | 0.62× (slower) |
| fp8, native weights, unaligned | 21.9 | 0.82× (slower) |
| **fp8, native weights, aligned `fe4m3vec4`** | **58.8** | **2.19× (faster)** |

Two levers turn 0.62×→2.19×: (1) **native fp8 weights** (`DATA_A_E4M3`, 1 byte) — half the
bytes loaded, and this matmul is bandwidth-bound, so a direct win + no f16→fp8 conversion;
(2) **aligned `fe4m3vec4` loads** (LOAD_VEC=4) — fp8 has no 8-wide vector type, but the
4-wide aligned load recovers the forced-unaligned ~2× penalty.

The 2.19× is a same-harness ratio (f16 and fp8 driven identically); absolute numbers sit
below ggml's fully-tuned f16 (47.81 via test-backend-ops) due to a warptile gap — the ratio
holds. Peak fp8 coopmat is 1.71× f16 peak (194.8 vs 114.0 TFLOPS, saturated).

## Files

- `fp8-shader.patch` — the edits to ggml's `mul_mm.comp` / `mul_mm_funcs.glsl` / `types.glsl`
  that route a new `DATA_A_E4M3` (native fp8 weight) type through the F16-style direct-load
  path (`A_TYPE = floate4m3_t`, BK=32). Apply over `src/ggml-vulkan/vulkan-shaders/`.
- `ggmm_driver.cpp` — standalone Vulkan harness that drives ggml's real `mul_mm.comp` spv
  with the correct push-constants (note: store is column-major → `stride_d = M`; one K-split
  → `k_split = K`) + AMD-large coopmat warptile spec-constants + `requiredSubgroupSize=32`.
  Builds the f16/fp8 variant spvs and benchmarks them. The source of the table above.
- `gemm_standalone.comp` — a from-scratch multi-warp coopmat GEMM (f16/fp8 via `-DFP8`) used
  to validate fp8 coopmat correctness independently of ggml's shader.
- `glslc_shim.py` — fp8-capable `glslc` for the in-tree build: routes only fp8 shaders
  (`-DDATA_A_E4M3`) to a `glslang` built from glslang `main` (which has the fp8 GLSL types
  `floate4m3_t`/`fe4m3vec4`, injecting `GL_GOOGLE_include_directive`), and passes every other
  shader through to the stock `glslc` unchanged. Install: `cp glslc glslc.real;
  cp glslc_shim.py glslc`; set `GLSLANG_BIN` + `REAL_GLSLC`.

## Toolchain note

The fp8 GLSL types are merged in glslang `main` but not yet in a released Vulkan SDK, so
the shaders require a `glslc`/`glslang` built from glslang `main` (hence the shim).

## Remaining (to productionize)

1. `GGML_TYPE_E4M3` weight type + traits (to_float/from_float) so test-backend-ops can drive
   + correctness-check it.
2. Generator (`vulkan-shaders-gen.cpp`): emit the `matmul_*_e4m3` aligned coopmat variant.
3. `ggml-vulkan.cpp`: register the e4m3 pipeline + dispatch MUL_MAT(e4m3, f32).
4. Build via the shim; `test-backend-ops MUL_MAT(e4m3)` for correctness (fp8 tol) + perf.

Depends on the `AMD_RDNA4` detection (branch `cloudhands/vulkan-rdna4-detect`).
