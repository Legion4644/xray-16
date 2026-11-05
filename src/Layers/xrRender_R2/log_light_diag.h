#pragma once

namespace {
struct PLightSig { const void* ptr; float px, py, pz, range; };
inline PLightSig g_ld_sig[128];
inline int g_ld_count = 0;
inline bool g_ld_checked = false;
inline bool g_ld_enabled = false;
inline u32 g_ld_calls = 0;

inline void log_light_diag_impl(const char* tag, int type, float px, float py, float pz, float range,
                                 float cr, float cg, float cb, int shadow, u32 frame)
{
    if (!g_ld_checked)
    {
        g_ld_checked = true;
        FILE* f = fopen("/storage/emulated/0/XRay/render_diag.txt", "rb");
        if (f)
        {
            fclose(f);
            g_ld_enabled = true;
        }
        else
            g_ld_enabled = false;
    }
    if (!g_ld_enabled || g_ld_calls >= 400)
        return;
    
    int slot = -1;
    for (int i = 0; i < g_ld_count; ++i)
    {
        if (g_ld_sig[i].ptr == nullptr)
        {
            slot = i;
            break;
        }
    }
    const bool isNew = (slot < 0);
    if (isNew)
    {
        if (g_ld_count >= 128)
            return;
        slot = g_ld_count++;
        g_ld_sig[slot].ptr = nullptr;
        g_ld_sig[slot].px = 1e30f;
        g_ld_sig[slot].py = 1e30f;
        g_ld_sig[slot].pz = 1e30f;
        g_ld_sig[slot].range = -1.f;
    }
    PLightSig& s = g_ld_sig[slot];
    const bool moved = _abs(s.px - px) > 0.1f || _abs(s.py - py) > 0.1f || _abs(s.pz - pz) > 0.1f || _abs(s.range - range) > 0.1f;
    if (!moved)
        return;
    s.px = px;
    s.py = py;
    s.pz = pz;
    s.range = range;
    ++g_ld_calls;
    Msg("[LIGHTDIAG] %s tag=%s type=%d pos=(%.2f,%.2f,%.2f) range=%.2f color=(%.2f,%.2f,%.2f) shadow=%d frame=%u",
        isNew ? "NEW" : "MOVE", tag, type, px, py, pz, range, cr, cg, cb, shadow, frame);
}
} // namespace
