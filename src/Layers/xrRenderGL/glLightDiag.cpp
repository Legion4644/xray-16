#include "stdafx.h"
#include "glLightDiag.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace LightDiag {

static FILE* g_log = nullptr;
static bool g_identity_logged = false;

void Open() {
    if (g_log) return;
    
    // Check for enable flag
    FILE* flag = fopen("/sdcard/XRay/enable_lightdiag.txt", "r");
    if (!flag) return;
    fclose(flag);
    
    g_log = fopen("/sdcard/XRay/lightdiag_d0.txt", "w");
    if (g_log) {
        fprintf(g_log, "=== LightDiag D0 Started ===\n");
        fflush(g_log);
    }
}

void Close() {
    if (!g_log) return;
    fprintf(g_log, "=== LightDiag D0 Closed ===\n");
    fclose(g_log);
    g_log = nullptr;
}

void LogIdentity() {
    if (!g_log || g_identity_logged) return;
    
    GLenum err_before = glGetError();
    while (glGetError() != GL_NO_ERROR) {}
    
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    
    fprintf(g_log, "[IDENTITY] version='%s' renderer='%s' vendor='%s'\n",
            version ? version : "null",
            renderer ? renderer : "null",
            vendor ? vendor : "null");
    
    GLenum err_after = glGetError();
    while (glGetError() != GL_NO_ERROR) {}
    
    g_identity_logged = true;
    fflush(g_log);
}

void LogSamplerRequest(const char* name, int stage, int wrap_mode) {
    if (!g_log) return;
    if (name && strcmp(name, "s_lmap") == 0) {
        fprintf(g_log, "[SAMPLER_REQ] name='%s' stage=%d wrap_mode=%d\n", name, stage, wrap_mode);
        fflush(g_log);
    }
}

void LogSamplerBinding(int stage, int sampler_id) {
    if (!g_log) return;
    LogIdentity(); // Ensure identity is logged first
    
    GLint wrap_s = 0, wrap_t = 0;
    GLenum err_before = glGetError();
    while (glGetError() != GL_NO_ERROR) {}
    
    glGetSamplerParameteriv(sampler_id, GL_TEXTURE_WRAP_S, &wrap_s);
    glGetSamplerParameteriv(sampler_id, GL_TEXTURE_WRAP_T, &wrap_t);
    
    GLenum err_after = glGetError();
    while (glGetError() != GL_NO_ERROR) {}
    
    fprintf(g_log, "[SAMPLER_GPU] stage=%d sampler_id=%d wrap_s=0x%x wrap_t=0x%x\n",
            stage, sampler_id, wrap_s, wrap_t);
    fflush(g_log);
}

void LogVertexAttribute(const char* semantic, int location, int type, bool normalized) {
    if (!g_log) return;
    LogIdentity();
    
    // Focus on TEXCOORD1 (lmh)
    if (semantic && strcmp(semantic, "TEXCOORD1") == 0) {
        fprintf(g_log, "[VERTEX] semantic='%s' location=%d type=%d normalized=%s\n",
                semantic, location, type, normalized ? "true" : "false");
        fflush(g_log);
    }
}

void LogLightStatic(bool is_static, bool is_point) {
    if (!g_log) return;
    LogIdentity();
    
    fprintf(g_log, "[LIGHT] is_static=%s is_point=%s\n",
            is_static ? "true" : "false",
            is_point ? "true" : "false");
    fflush(g_log);
}

} // namespace LightDiag
