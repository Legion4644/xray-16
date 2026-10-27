# Escopo de Adaptação: r1_gl e r2_gl como Backends OpenGL ES Mínimos

**Data:** 2026-10-26  
**Branch:** android-port-glrender  
**Commit:** 03f2efe8a  

---

## 1. Arquivos xrRender/ Agnósticos de API (Reaproveitáveis)

Estes arquivos **não incluem headers DirectX** (`d3d9.h`, `d3d10`, `d3d11`, `dxgi`, `D3D`, `D3DX`) e contêm lógica puramente algorítmica/estrutural. São candidatos diretos a reaproveitamento.

### Core de Visibilidade/HOM/Setores (~15 arquivos)
| Arquivo | Função |
|---------|--------|
| `HOM.h/cpp` | Hierarchical Occlusion Maps - culling por oclusão |
| `r__sector.h/cpp` | Detecção e travessia de setores/portais |
| `r__sector_detect.cpp` | Detecção de setor da câmera |
| `r__sector_traversal.cpp` | Travessia BSP/portal para visible set |
| `r__dsgraph_structure.h` | Estrutura do grafo de renderização (contextos, passes) |
| `r__dsgraph_types.h` | Tipos do grafo de renderização |
| `r__dsgraph_build.cpp` | Build do grafo de renderização (culling, sorting) |
| `r__dsgraph_render.cpp` | Render do grafo (dispatch de draw calls) |
| `r__dsgraph_render_lods.cpp` | LOD selection no grafo |
| `r__occlusion.h/cpp` | Occlusion queries (HW/SW) |
| `r__sync_point.cpp/h` | Sincronização multi-thread |
| `r__pixel_calculator.cpp/h` | Cálculo de cobertura de pixel |

### Modelos/Visuals/Animação (~20 arquivos)
| Arquivo | Função |
|---------|--------|
| `FVisual.h/cpp` | Base de visual (forward/deferred) |
| `FHierrarhyVisual.h/cpp` | Visual hierárquico (LODs, children) |
| `FProgressive.h/cpp` | Progressive meshes (R1 usa, R2 não) |
| `FLOD.h/cpp` | Level of Detail |
| `FSkinned.h/cpp` | Skinned meshes (CPU skinning) |
| `FSkinnedTypes.h` | Tipos de skinning |
| `SkeletonAnimated.h/cpp` | Animação esquelética |
| `SkeletonCustom.h/cpp` | Custom skeleton |
| `SkeletonRigid.cpp` | Rigid skeleton |
| `SkeletonX.h/cpp` | Skeleton X (novo formato) |
| `SkeletonXSkinXW*.cpp` | Skinning SSE/CPU |
| `FTreeVisual.h/cpp` | Tree visual (wind, LOD) |
| `FBasicVisual.h/cpp` | Visual básico |
| `ModelPool.h/cpp` | Pool de modelos (cache, instancing) |
| `NvTriStrip*.cpp/h` | Tri-stripification (NVIDIA) |

### Detalhes/Grama/Partículas (~12 arquivos)
| Arquivo | Função |
|---------|--------|
| `DetailManager.h/cpp` | Detail manager (slots, cache, visibility) |
| `DetailManager_CACHE.cpp` | Cache de slots de detail |
| `DetailManager_Decompress.cpp` | Decompressão de detail |
| `DetailManager_VS.cpp` | Vertex shader de detail |
| `DetailManager_soft.cpp` | Soft detail (CPU fallback) |
| `DetailModel.h/cpp` | Modelo de detail individual |
| `DetailFormat.h` | Formato de detail |
| `ParticleEffect.h/cpp` | Sistema de partículas |
| `ParticleEffectDef.h/cpp` | Definições de partículas |
| `ParticleGroup.h/cpp` | Grupo de partículas |
| `PSLibrary.h/cpp` | Library de particle systems |

### Luzes/Sombras/Wallmarks (~12 arquivos)
| Arquivo | Função |
|---------|--------|
| `light.h/cpp` | Light base class |
| `light_gi.cpp/h` | Global illumination |
| `light_smapvis.cpp/h` | Shadow map visibility |
| `light_vis.cpp` | Light visibility (R2/R3) |
| `LightTrack.h/cpp` | Light tracking (hemicube) |
| `Light_DB.h/cpp` | Database de luzes |
| `Light_Package.h/cpp` | Package de luzes para render |
| `Light_Render_Direct.h/cpp` | Direct light rendering |
| `Light_Render_Direct_ComputeXFS.cpp` | Compute xform para sun |
| `WallmarksEngine.h/cpp` | Engine de wallmarks (static/skeleton) |
| `occRasterizer.h/cpp` | Rasterizador de oclusão SW |

### Shaders/Constantes/Resources (~15 arquivos)
| Arquivo | Função |
|---------|--------|
| `Shader.h/cpp` | Shader wrapper (VS/PS/GS/HS/DS/CS) |
| `SH_Constant.h/cpp` | Shader constants |
| `SH_Matrix.h/cpp` | Shader matrices |
| `SH_Atomic.h/cpp` | Atomic shader resources |
| `SH_Texture.h` | Shader texture binding |
| `SH_RT.h` | Shader render target binding |
| `Blender.h/cpp` | Blender base (compilação de shaders) |
| `Blender_CLSID.h` | Class IDs de blenders |
| `Blender_Recorder.h` | Recorder de blender (independente de API) |
| `Blender_Recorder_StandartBinding.cpp` | Binding padrão de constantes |
| `ResourceManager.h` | Resource manager interface |
| `ResourceManager_Loader.cpp` | Carregamento de recursos |
| `ResourceManager_Reset.cpp` | Reset de recursos |
| `ETextureParams.h/cpp` | Parâmetros de textura |
| `TextureDescrManager.h/cpp` | Texture description manager |

### UI/Debug/Utils (~10 arquivos)
| Arquivo | Função |
|---------|--------|
| `du_*.h/cpp` | Debug utilities (box, cone, cylinder, sphere) |
| `VertexCache.h/cpp` | Vertex cache simulation |
| `xrStripify.h/cpp` | Stripification |
| `xr_effgamma.h/cpp` | Gamma correction |
| `r_constants.h/cpp` | Render constants |
| `r_constants_cache.h` | Constant cache |
| `r_sun_cascades.h` | Sun cascades config |

---

## 2. Arquivos xrRender/ DX-Dependentes (Backend de Draw - Substituir)

Estes arquivos **incluem headers DirectX** e implementam o backend de draw. Precisam de substitutos GL.

### Backend Core (críticos)
| Arquivo | Substituição GL Necessária |
|---------|---------------------------|
| `R_Backend.h/cpp` | Command buffer, state management, draw calls |
| `R_Backend_Runtime.h/cpp` | Runtime state (samplers, shaders, RTs) |
| `R_Backend_DBG.cpp` | Debug markers (PIX/NSight) |
| `R_Backend_xform.cpp/h` | Transform setup (view/proj/world) |
| `R_Backend_hemi.h/cpp` | Hemicube rendering |
| `R_Backend_tree.h/cpp` | Tree rendering |
| `R_DStreams.h/cpp` | Dynamic vertex/index streams |

### Hardware/Device/Caps
| Arquivo | Substituição GL Necessária |
|---------|---------------------------|
| `D3DUtils.h/cpp` | D3D utilities → GL utilities (glHW.cpp já existe) |
| `D3DXRenderBase.h/cpp` | Base D3D renderer → GL renderer base |
| `HWCaps.h` | Caps → glHWCaps.cpp |
| `QueryHelper.h` | Query helper (occlusion/timestamp) |

### Shaders/Resources (API-specific)
| Arquivo | Substituição GL Necessária |
|---------|---------------------------|
| `ShaderResourceTraits.h` | Shader reflection → glsl_reflection |
| `ResourceManager.h/cpp` | Parcial: parte DX-specific |
| `ResourceManager_Resources.cpp` | Criação de shaders/textures/buffers DX |
| `ResourceManager_Scripting.cpp` | Shader scripting (HLSL-specific) |
| `SH_Texture.cpp` | Texture binding DX |
| `SH_RT.h` | RT binding DX |

### Visuals DX-specific
| Arquivo | Substituição GL Necessária |
|---------|---------------------------|
| `FVisual.cpp` | `#if RENDER==R_R1` paths DX |
| `FTreeVisual.cpp` | DX tree rendering |
| `FProgressive.cpp` | Progressive mesh DX |
| `FSkinnedTypes.h` | Vertex declaration DX |
| `FVF.h` | Flexible Vertex Format (DX) |
| `Texture.cpp` | Texture creation/upload DX |

### Effects/Blenders DX (renderers específicos)
| Arquivo | Função |
|---------|--------|
| `dxDebugRender.cpp/h` | Debug render |
| `dxEnvironmentRender.cpp/h` | Environment (sky, clouds) |
| `dxFontRender.cpp/h` | Font rendering |
| `dxImGuiRender.cpp/h` | ImGui backend |
| `dxLensFlareRender.cpp/h` | Lens flares |
| `dxRainRender.cpp/h` | Rain rendering |
| `dxStatGraphRender.cpp/h` | Stat graph |
| `dxThunderbolt*.cpp/h` | Thunderbolts |
| `dxUIRender.cpp/h` | UI rendering |
| `dxUIShader.cpp/h` | UI shaders |
| `dxUISequenceVideoItem.cpp/h` | Video UI |
| `dxWallMarkArray.cpp/h` | Wallmark arrays |
| `dxParticleCustom.cpp/h` | Custom particles |
| `blenders/*.cpp` | Blenders específicos (dx11*, blender_*) |

---

## 3. Shaders HLSL r1 — Pipeline Forward Mínimo

### Diretório: `res/gamedata/shaders/r1/` (58 arquivos)

### Essenciais para r1_gl (Forward + Static Lighting)

| Categoria | Arquivos | Qtd | Prioridade |
|-----------|----------|-----|------------|
| **Base/Common** | `common.h` | 1 | **CRÍTICO** - macros, structs, funções compartilhadas |
| **Base Forward** | `impl.vs`, `impl.ps`, `impl_dt.vs`, `impl_dt.ps` | 4 | **CRÍTICO** - shader base (diffuse, normal map, detail) |
| **Lightmaps** | `lmap.vs`, `lmap.ps`, `lmap_dt.vs`, `lmap_dt.ps` | 4 | **CRÍTICO** - static lighting (lightmaps) |
| **Vertex/Transform** | `simple.vs`, `base_lplanes.vs` | 2 | **CRÍTICO** - vertex transform simples |
| **Details/Grama** | `detail_still.vs`, `detail_wave.vs` | 2 | **CRÍTICO** - grama/vegetação |
| **Models** | `model_def_hq.vs/ps`, `model_def_hq_dt.vs/ps` | 4 | **CRÍTICO** - modelos estáticos HQ |
| **Dynamic Lights (opt)** | `add_point.ps`, `add_spot.ps`, `impl_point.vs`, `impl_spot.vs` | 6 | **OPCIONAL** - point/spot lights (r1 clássico tem suporte limitado) |
| **Tree/Vegetation** | `tree.vs`, `tree_s.vs`, `tree_w.vs`, `*_dt.vs`, `*_point.vs`, `*_spot.vs` | 14 | **ALTO** - árvores/vegetação |
| **Water** | `water.vs`, `waterd.vs` | 2 | **MÉDIO** - água |
| **Wallmarks** | `wmark.vs`, `wmark_blend.vs`, `effects_wallmark*.s` | 4 | **MÉDIO** - wallmarks |
| **Skinning** | `skin.h`, `shared_dynlight*.vs` | 4 | **ALTO** - skinned meshes |
| **Vert/Simple** | `vert.vs/ps`, `vert_dt.vs/ps`, `vert_point.vs`, `vert_spot.vs` | 6 | **BAIXO** - vertex lighting legacy |

### Estimativa de Tradução GLSL ES 3.0
| Tier | Shaders | Estimativa |
|------|---------|------------|
| **Core mínimo** (base, lmap, impl, simple, detail) | ~13 | ~1.5 KLOC GLSL |
| **Models + Trees + Skinning** | ~22 | ~2.5 KLOC GLSL |
| **Complete r1** (todos 58) | 58 | ~5-6 KLOC GLSL |

**Recomendação:** Começar com **core mínimo (13 shaders)** — cobre 80% do visual. Trees/models/skinning podem usar shaders unificados.

---

## 4. Shaders HLSL r2 — Pipeline Deferred Enxuto

### Diretórios: `res/gamedata/shaders/r2/` + `r2/shared/`

### Estrutura
- `r2/common.h` (13 KB) - macros, structs, funções deferred
- `r2/skin.h` (5.7 KB) - skinning deferred
- `r2/water.vs/ps` - água deferred
- `r2/shared/common.h` (1.9 KB) - shared utilities

### Shaders Deferred Principais (em `res/gamedata/shaders/gl/` já portados)

| Pass | Shaders GL Existentes | Essencial para r2_gl |
|------|----------------------|---------------------|
| **G-Buffer** | `gbuffer*.vs/ps` | **SIM** - Position/Normal/Albedo+Spec |
| **Sun Lighting** | `accum_sun.vs/ps`, `accum_sun_near/far.ps` | **SIM** - cascaded sun |
| **Spot/Omni** | `accum_spot*.ps`, `accum_omni*.ps` | **SIM** - point/spot lights |
| **Shadow Map** | `smap*.vs/ps` | **SIM** - depth-only sun |
| **Combine/Post** | `combine*.ps`, `phase_combine.ps` | **SIM** - final combine + tonemap |
| **SSAO** | `ssao*.ps` | **OPCIONAL** - SSAO/HBAO |
| **Bloom** | `bloom*.ps` | **OPCIONAL** - bloom |
| **Volumetric** | `accum_volumetric*.ps` | **OPCIONAL** - sun shafts |
| **Rain** | `rain*.ps` | **OPCIONAL** - rain |

### Estimativa r2_gl Enxuto (sem advanced PP)

| Componente | Shaders GLSL ES Necessários | KLOC Estimado |
|------------|----------------------------|---------------|
| **G-Buffer** (3 RTs: Pos, Normal, Color+Spec) | 2-3 (vs/ps) | ~0.8 KLOC |
| **Sun Shadow Map** (depth only, 3 cascades) | 1-2 (vs/ps) | ~0.5 KLOC |
| **Sun Lighting** (near/far, minmax opt) | 4-6 | ~1.5 KLOC |
| **Spot/Omni Lighting** | 4-6 | ~1.5 KLOC |
| **Combine + Tonemap** (HDR-lite) | 2-3 | ~0.8 KLOC |
| **UI/Font/Debug** (reaproveita r1_gl) | 3-4 | ~0.5 KLOC |
| **TOTAL r2_gl enxuto** | **~18-24 shaders** | **~5-6 KLOC** |

**Comparação:** r3-GL atual tem **359 shaders GL** (~40 KLOC). r2_gl enxuto = **~15%** da complexidade.

---

## 5. Render Targets — Contagem Real

### r1 (Forward + Static Lighting) — Pior Caso
| RT | Formato | Uso |
|----|---------|-----|
| Backbuffer | RGBA8 / sRGB | Color final |
| Depth/Stencil | D24S8 | Depth test + stencil (wallmarks) |
| **TOTAL** | | **2 RTs** |

> **Nota:** R1 não usa G-buffer, não usa accumulation buffer, não usa HDR. Apenas backbuffer + depth. Wallmarks usam stencil no depth buffer.

### r2 (Deferred Enxuto) — Pior Caso (sem MSAA, sem advanced PP)
| RT | Formato | Uso |
|----|---------|-----|
| **G-Buffer (3-4)** | | |
| rt_Position | RG16F (ou RGB10A2) | Posição world/view-space |
| rt_Normal | RG16F (ou RGB10A2) | Normal + hemi |
| rt_Color | RGBA8 (ou RG16F) | Albedo + spec/gloss |
| rt_Accumulator | RGBA16F | HDR accumulation (opcional: RGBA8 para LDR) |
| **Shadow Maps** | | |
| rt_smap_depth | D24/D32F | Sun cascades (3 slices array ou 3 RTs) |
| **Post/Combine** | | |
| rt_Generic_0 | RGBA8 | Combine intermediate |
| rt_Generic_1 | RGBA8 | Bloom/SSAO intermediate |
| **TOTAL (sem MSAA, HDR-lite)** | | **7-8 RTs** |

> **Comparação:** r3-GL atual usa **~15 RTs** (HDR full, MSAA, minmax SM, volumetric, bloom chain, luminance chain, etc.). r2_gl enxuto = **~50%**.

---

## 6. Ponto de Registro + IRender Mínimo

### Registro (entry_point.cpp)
```cpp
// Adicionar ao array s_render_modules:
#if defined(XR_PLATFORM_ANDROID)
    xray::render::android_gles::GetRendererModule(),  // r1_gl / r2_gl
#endif
#if !defined(XR_PLATFORM_ANDROID) || defined(XR_ANDROID_GLRENDER)
    xray::render::render_gl::GetRendererModule(),     // r3-GL
#endif
```

### RendererModule Interface (EngineAPI.h:53-61)
```cpp
class RendererModule {
    virtual const xr_vector<std::pair<pcstr, int>>& ObtainSupportedModes() = 0;
    virtual bool CheckGameRequirements() = 0;
    virtual void SetupEnv(pcstr mode) = 0;
    virtual void ClearEnv() = 0;
};
```

### IRender Mínimo para Boot (Render.h:131-398)

#### r1_gl — Métodos Obrigatórios (~45)
| Categoria | Métodos |
|-----------|---------|
| **Lifecycle** | `create()`, `destroy()`, `reset_begin()`, `reset_end()`, `OnDeviceCreate()`, `OnDeviceDestroy()`, `Create()`, `Reset()`, `SetupStates()`, `ObtainRequiredWindowFlags()` |
| **Shaders** | `shader_compile()`, `getShaderPath()`, `addShaderOption()`, `clearAllShaderOptions()` |
| **Resources** | `model_Create()`, `model_CreateChild()`, `model_Duplicate()`, `model_Delete()`, `model_CreatePE()`, `model_CreateParticles()`, `model_CreateDM()`, `model_Delete(DM)`, `models_Prefetch()`, `models_Clear()`, `texture_load()` |
| **Render Loop** | `Calculate()`, `Render()`, `RenderMenu()`, `Begin()`, `End()`, `Clear()`, `ClearTarget()`, `OnFrame()`, `OnCameraUpdated()`, `BeforeWorldRender()`, `AfterWorldRender()` |
| **Visuals** | `add_Visual()`, `getVisual()`, `getVB_Format()`, `getVB()`, `getIB()`, `getSWI()` |
| **Lights** | `light_create()`, `glow_create()`, `light_destroy()`, `glow_destroy()` |
| **Wallmarks** | `add_StaticWallmark()`, `clear_static_wallmarks()`, `add_SkeletonWallmark()` |
| **Occlusion** | `occ_visible()` (3 overloads) |
| **Gamma/Post** | `setGamma()`, `setBrightness()`, `setContrast()`, `updateGamma()`, `SetPostProcessParams()` |
| **Stats/Debug** | `DumpStatistics()`, `Screenshot()`, `GetDeviceState()`, `GetForceGPU_REF()`, `GetCacheStatPolys()` |

#### r2_gl — Acréscimos sobre r1_gl (~15 métodos)
| Categoria | Métodos Adicionais |
|-----------|-------------------|
| **Deferred** | `rmNear()`, `rmFar()`, `rmNormal()` (viewport splits) |
| **Shadow Maps** | `phase_smap_direct()`, `phase_smap_direct_tsh()`, `create_minmax_SM()`, `use_minmax_sm_this_frame()` |
| **G-Buffer** | `phase_accumulator()`, `u_setrt()`, `u_stencil_optimize()`, `u_compute_texgen*()` |
| **Advanced** | `is_sun_static()`, `get_dx_level()`, `GetBackendAPI()`, `GetGeneration()` (retorna R2) |

---

## 7. Escopo r2_gl como Degrau 2 — Reaproveitamento do Degrau 1

### O que r2 Clássico Reusa do xrRender/ Shared
| Componente | Compartilhado com r1_gl |
|------------|------------------------|
| Visibility/HOM/Sectors | ✅ 100% |
| Model Pool/Visuals | ✅ 100% |
| Detail Manager | ✅ 100% |
| Particle System | ✅ 100% |
| Light DB / Tracking | ✅ 100% |
| Wallmarks Engine | ✅ 100% |
| Shader System (Blender, Constants, Resources interface) | ✅ 90% (apenas backend muda) |
| Sector/Traversal | ✅ 100% |
| Animation/Skinning | ✅ 100% |

### O que r2 Acrescenta (Específico Deferred)
| Componente | Pode Reaproveitar Infra r1_gl? |
|------------|-------------------------------|
| **G-Buffer Setup** | ❌ Novo — MRT setup, formats |
| **Sun Shadow Map** | ✅ Parcial — infra de RT + shader loader; shaders novos |
| **Deferred Lighting Passes** | ❌ Novo — accum_direct, accum_spot, accum_omni |
| **Combine + Tonemap** | ✅ Parcial — RT management + shader loader; shader novo |
| **HDR/Accumulator** | ❌ Novo — FP16 blend, accumulation logic |
| **SSAO/HBAO** | ❌ Opcional — pode ficar para degrau 3 |
| **Volumetric/God Rays** | ❌ Opcional — degrau 3 |

### Infra Compartilhável (Backend GL Fino — Comum r1_gl/r2_gl)
| Módulo | Descrição | Reaproveitável |
|--------|-----------|----------------|
| **GL Context/Device** | SDL_GL_CreateContext, gladLoadGL, caps query | ✅ 100% |
| **Shader Loader/Compiler** | GLSL ES compile, cache (binary), includes | ✅ 100% |
| **Resource Manager GL** | Texture/Buffer/Shader/State creation GL | ✅ 100% |
| **Render Target Manager** | RT create/destroy/bind, MRT support | ✅ 100% |
| **State Cache** | Blend/Depth/Stencil/Rasterizer state cache | ✅ 100% |
| **Command Buffer** | Draw call recording, submit | ✅ 100% |
| **Constant/Uniform Buffer** | UBO/SSBO management | ✅ 100% |
| **Query Pool** | Occlusion/timestamp queries | ✅ 100% |
| **Debug/Profiler** | GL_DEBUG_OUTPUT, Tracy integration | ✅ 100% |

---

## 8. Estimativa KLOC por Degrau

| Componente | KLOC Estimado | Notas |
|------------|---------------|-------|
| **(a) Backend GL Fino Comum** | **~8-10 KLOC** | |
|   GL Context/HW Caps | 1.5 | glHW.cpp, glHWCaps.cpp base |
|   Shader Loader/Compiler/Cache | 2.5 | rgl_shaders.cpp simplificado |
|   Resource Manager GL | 2.0 | glTexture.cpp, glBufferUtils.cpp, glState.cpp |
|   Render Target Manager | 1.0 | gl_rendertarget.h simplificado |
|   Command Buffer/State Cache | 1.5 | glR_Backend_Runtime.h |
|   Debug/Profiler | 0.5 | |
| **(b) r1_gl Específico** | **~4-5 KLOC** | |
|   CRender_R1 (IRender impl) | 2.0 | Subset de CRender (R_R1 paths) |
|   Forward Pipeline | 1.0 | Single pass, lightmap sampling |
|   Shaders GLSL ES (core 13) | 1.5 | ~1.5 KLOC GLSL |
|   RendererModule r1_gl | 0.5 | ObtainSupportedModes, SetupEnv |
| **(c) r2_gl Específico (acréscimo sobre r1_gl)** | **~6-8 KLOC** | |
|   G-Buffer/MRT Pipeline | 2.0 | MRT setup, formats |
|   Shadow Map (Sun Cascades) | 1.5 | Cascade setup, minmax SM |
|   Deferred Lighting Passes | 2.0 | accum_direct, spot, omni |
|   Combine + Tonemap (HDR-lite) | 1.0 | Final combine, simple tonemap |
|   Shaders GLSL ES (18-24) | 2.5 | ~5-6 KLOC GLSL |
| **TOTAL r1_gl** | **~12-15 KLOC** | |
| **TOTAL r1_gl + r2_gl** | **~18-23 KLOC** | |
| **Comparação: r3-GL atual** | **~40 KLOC** (C++) + 40 KLOC (GLSL) | r1_gl+r2_gl = ~50% do código C++, ~15% shaders |

---

## 9. Riscos Top-3

| # | Risco | Probabilidade | Impacto | Mitigação |
|---|-------|---------------|---------|-----------|
| **1** | **Shader Compilation/Linking no GLES 3.0** — Drivers móveis (Adreno/Mali) são menos tolerantes a GLSL complexo, uniform blocks, texture arrays. Shader cache binário pode falhar entre dispositivos. | **ALTA** | **ALTO** — Falha silenciosa (shader não compila, fallback para wireframe/error) | • Testar em 3+ GPUs (Adreno, Mali, PowerVR) via CI device farm<br>• Validar GLSL com glslangValidator offline<br>• Fallback para shaders "safe" (sem uniform blocks, sem texture arrays)<br>• Log completo de compile/link errors no device |
| **2** | **Performance Bandwidth no Mobile** — Mesmo r2 enxuto faz 3-4 RTs full-screen (G-buffer) + shadow map + combine. Em 1080p Adreno 616: ~200-300 MB/frame bandwidth. Pode engasgar se não otimizar (tile-based deferred, resolve otimizado). | **MÉDIA** | **ALTO** — FPS < 30 em cenas complexas | • Implementar tile-based deferred (via subpasses/framebuffer fetch se GLES 3.1+)<br>• Usar RGB10A2 para G-buffer (64bpp vs 128bpp FP16)<br>• Depth-only pre-pass para early-Z<br>• Profile com `adb shell dumpsys gfxinfo` / Adreno Profiler |
| **3** | **Divergência de Comportamento Visual vs DX9 Original** — R1/R2 clássico dependem de quirks DX9 (half-pixel offset, MRT blend behavior, sRGB handling, depth compare). GLES tem convenções diferentes (UV origin, NDC Z, sRGB). Diferenças sutis quebram lighting/ssao/shadows. | **MÉDIA** | **MÉDIO** — Artefatos visuais, shadows erradas, gamma errado | • Testes de regressão visual (screenshots de referência DX9 via Winlator)<br>• Validar: half-pixel offset (GL: +0.5/width), NDC Z (0..1 vs -1..1), sRGB write/read<br>• Unit tests para math de shadow mapping (cascade splits, PCF) |

---

## 10. Plano de Execução Sugerido

| Fase | Entregável | Esforço | Dependência |
|------|------------|---------|-------------|
| **0** | Backend GL Fino Comum (contexto, shader loader, RT manager, state cache) | 3-4 semanas | Nenhuma |
| **1** | r1_gl RendererModule + CRender_R1 (subset IRender) + 13 shaders core | 3-4 semanas | Fase 0 |
| **2** | Validação r1_gl no device (static lighting, details, models, UI) | 1-2 semanas | Fase 1 |
| **3** | r2_gl: G-Buffer + Shadow Map + Sun Lighting + Combine | 4-5 semanas | Fase 2 |
| **4** | r2_gl: Spot/Omni + Tonemap HDR-lite + Polish | 2-3 semanas | Fase 3 |
| **5** | Otimização Mobile (tile-based, bandwidth, driver workarounds) | 2-3 semanas | Fase 4 |

**Total estimado:** 15-21 semanas para r1_gl + r2_gl funcionais e otimizados.

---

## Conclusão

A adaptação é **técnica e economicamente viável**:

1. **~70% do código (xrRender/ shared) é reaproveitável diretamente** — sem DX deps.
2. **Backend GL fino comum** (~10 KLOC) serve aos dois degraus.
3. **r1_gl é "baixo risco"** — forward, 2 RTs, shaders simples, valida hardware.
4. **r2_gl enxuto é "médio risco"** — deferred leve (7-8 RTs), 18-24 shaders, mas reaproveita toda infra do r1_gl.
5. **Evita o excesso do r3-GL** (15 RTs, 359 shaders, HDR full, compute shaders, minmax SM complexo).

**Recomendação:** Prosseguir com Fase 0-1 imediatamente. r1_gl sozinho já desbloqueia R1/R2.5 no Android com performance nativa GLES 3.0. r2_gl vem como evolução natural reutilizando 90% da infra.