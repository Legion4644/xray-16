#pragma once

namespace LightDiag {
    void Open();
    void Close();
    void LogIdentity();
    void LogSamplerRequest(const char* name, int stage, int wrap_mode);
    void LogSamplerBinding(int stage, int sampler_id);
    void LogVertexAttribute(const char* semantic, int location, int type, bool normalized);
    void LogLightStatic(bool is_static, bool is_point);
}
