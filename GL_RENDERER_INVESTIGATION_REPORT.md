# Investigation Report: Classic STALKER CoP OpenGL Backend vs OpenXRay r3-GL

**Date:** 2026-10-26  
**Branch:** android-port-glrender  
**Commit:** 03f2efe8a  

---

## 1. Executive Summary

**No classic OpenGL backend for r1/r2/r2a renderers exists in the original STALKER: Call of Pripyat (X-Ray 1.6) engine.**

The original X-Ray 1.6 engine (used in CoP) was **DirectX-only** with four renderer modes:
- **renderer_r1** (id 1) — Static lighting, DirectX 9
- **renderer_r2** (id 2) — Deferred shading, DirectX 9/10
- **renderer_r2.5** (id 3) — R2 + some R3 features, DirectX 10
- **renderer_r3** (id 4) — Full deferred + HDR, DirectX 10/11

The OpenGL renderer in OpenXRay (**renderer_rgl**, id 6) is a **completely new implementation** by the OpenXRay team (primarily CrossVR/Armada651), porting the R3/R4 renderer to OpenGL 4.1+ / GLES 3.0+. It is **not** derived from any classic CoP GL code.

---

## 2. What Exists (Evidence)

### 2.1 Original CoP Renderers (DirectX only)
| Path | Description | KLOC |
|------|-------------|------|
| `src/Layers/xrRender_R2/` | R2/R2.5 deferred renderer | ~10 KLOC |
| `src/Layers/xrRenderDX11/` | R3/R4 DX11 renderer | ~15+ KLOC |
| `src/Layers/xrRender/` | Shared R1/R2 base + R1 static | ~25+ KLOC |

### 2.2 OpenXRay OpenGL Renderer (r3-GL / renderer_rgl)
| Path | Description | KLOC |
|------|-------------|------|
| `src/Layers/xrRenderPC_GL/` | Main GL renderer (R3 port) | ~4.6 KLOC |
| `src/Layers/xrRenderGL/` | GL backend (state, textures, HW caps) | ~5.4 KLOC |
| `res/gamedata/shaders/gl/` | GLSL shaders (R3 feature set) | ~200+ shaders |

### 2.3 Removed/Deprecated Code
- **xrRenderer_GL** (removed in commit `cb51564da`, Nov 2025): An earlier experimental GL attempt, ~11.6 KLOC deleted
- **opengl-max branch**: WebGL2/WebAssembly support, not classic CoP GL

### 2.4 Known Forks Checked
| Fork | GL Renderer Status |
|------|-------------------|
| OpenXRay/xray-16 (this repo) | r3-GL only (modern, OpenGL 4.1+) |
| OpenXRay/xray-15 (Clear Sky) | DX only, no GL |
| OGSR-Engine | DX11 only (FSR3, DLSS focus) |
| xr3DPatch / AXR / OXR-merge | No classic GL backend found |

---

## 3. Technical Comparison: r3-GL vs Hypothetical Classic GL

| Aspect | OpenXRay r3-GL (current) | Classic CoP R2 (DX) | Classic CoP R1 (DX) |
|--------|-------------------------|---------------------|---------------------|
| **API** | OpenGL 4.1+ / GLES 3.0+ | DirectX 9/10 | DirectX 9 |
| **Shaders** | GLSL 410 / ESSL 300 | HLSL (DX9/DX10) | HLSL (DX9) |
| **Framebuffer** | HDR (FP16), MSAA | LDR/RGBA8, optional MSAA | LDR only |
| **Render Targets** | ~15+ (G-buffer, SMAP, accum, PP) | ~8-10 | ~3-4 |
| **Passes/Frame** | 12-18 (deferred + PP) | 8-12 (deferred) | 4-6 (forward) |
| **Sun Shadows** | Cascaded (3-4), minmax SM | Cascaded (3), basic PCF | None (static only) |
| **Deps** | GLAD, GLM, SDL2, gli | DX9/DX10 SDK | DX9 SDK |
| **Pure GL?** | Yes (no DX headers) | No (DX headers required) | No (DX headers required) |

---

## 4. Integration Options Assessment

### Option A: Integrate Classic GL as r1_gl / r2_gl alongside r3-GL
**Viability: IMPOSSIBLE**

- **No code exists** — There is no classic GL backend to integrate. The original engine never had one.
- Would require **writing from scratch** a complete OpenGL 2.1/3.3 implementation of R1 and R2 render paths (~15-20 KLOC).
- Shader translation: All R1/R2 HLSL shaders → GLSL 120/330 (200+ shaders).
- **Effort:** 6-12 months for one engineer.

---

### Option B: Replace r3-GL Entirely with Classic GL Backend
**Viability: IMPOSSIBLE**

- **No classic GL backend exists** to replace with.
- r3-GL **is** the only GL backend in the project.
- Removing r3-GL would leave **zero OpenGL support** on Linux/macOS/Android.

---

### Option C: Hybrid — r3-GL for R3, New Classic GL for R1/R2/R2a
**Viability: EXTREMELY DIFFICULT / NOT RECOMMENDED**

| Factor | Assessment |
|--------|------------|
| **Code existence** | ❌ Zero classic GL code exists |
| **Shader compatibility** | ❌ R1/R2 shaders use DX9 semantics, no GLSL equivalents |
| **Architecture mismatch** | R1 = forward, R2 = early deferred, r3-GL = modern deferred |
| **Maintenance burden** | Two complete GL backends to maintain |
| **Android/GLES** | Classic GL would need GLES 2.0/3.0 paths — massive effort |
| **Estimated effort** | 12-18 months for parity with current r3-GL |

**Only plausible path:** Implement R1 GL renderer as a **separate minimal forward renderer** for low-end devices (GLES 2.0), but this is a **new feature**, not integration of existing code.

---

## 5. Recommendation

### **Do not pursue classic GL backend integration.** It does not exist.

### Recommended Path Forward:

1. **Keep and improve r3-GL (renderer_rgl)** — It is the only viable OpenGL backend.
2. **Focus on r3-GL optimization for Android/GLES:**
   - Reduce render target count (current: ~15 → target: ~8)
   - Simplify shader variants (current: 200+ → target: <100)
   - Implement GLES 3.0 fallback path for minmax SM, MSAA
3. **If R1/R2 support on GL is needed:**
   - Implement a **minimal R1_GL** (forward, GLES 2.0 compatible) as a new renderer module
   - Scope: ~3-4 KLOC, basic static lighting only, no shadows
   - Use existing `xrRender` shared code + new GL shaders
4. **Deprecate R2/R2.5 on non-Windows** — They require DX9/DX10 features not portable to GLES.

---

## 6. Key Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Wasting months searching for non-existent code | HIGH | HIGH | ✅ This report closes the investigation |
| Attempting hybrid R1/R2 GL implementation | MEDIUM | HIGH | Defer; focus on r3-GL GLES optimization |
| Breaking Android GL renderer during refactor | MEDIUM | HIGH | CI validation via GitHub Actions (mandatory) |
| Shader explosion from R1+R2+R3 GL variants | HIGH | MEDIUM | Unified shader pipeline (FrameGraph) |

---

## 7. Conclusion

**The premise of "replacing r3-GL with classic CoP GL backend" is based on a false assumption.** The classic CoP engine had **no OpenGL renderer whatsoever**. The current r3-GL is the **first and only** OpenGL implementation in X-Ray engine history.

**Action:** Close this investigation. Redirect effort to r3-GL GLES optimization and FrameGraph unification.