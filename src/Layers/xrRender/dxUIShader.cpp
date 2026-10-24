#include "stdafx.h"
#include "dxUIShader.h"

namespace xray::render::RENDER_NAMESPACE
{
void dxUIShader::Copy(IUIShader& _in) { *this = *((dxUIShader*)&_in); }
void dxUIShader::create(LPCSTR sh, LPCSTR tex) { hShader.create(sh, tex); }
void dxUIShader::destroy() { hShader.destroy(); }

bool dxUIShader::operator==(const IUIShader& other) const
{
    return hShader == static_cast<const dxUIShader&>(other).hShader;
}

CTexture* dxUIShader::GetBaseTexture() const
{
    if (!hShader)
        return nullptr;

    if (!hShader->E[0])
        return nullptr;

    if (hShader->E[0]->passes.empty())
        return nullptr;

    const auto& pass_ref = hShader->E[0]->passes[0];
    if (!pass_ref)
        return nullptr;

    const SPass& pass = *pass_ref;
    if (!pass.T)
        return nullptr;

    const STextureList& textures = *pass.T;
    if (textures.empty())
        return nullptr;

    if (!pass.constants)
        return nullptr;

    const R_constant* sbase = pass.constants->get(baseTexture)._get();
    const u32 index = sbase ? sbase->samp.index : 0;
    if (index >= textures.size())
        return nullptr;

    return textures[index].second._get();
}

xrImTextureData dxUIShader::GetImGuiTextureId()
{
    const auto texture = GetBaseTexture();
    if (!texture)
        return {};

    return
    {
        texture->GetImTextureID(),
        {
            (float)texture->get_Width(),
            (float)texture->get_Height()
        }
    };
}

bool dxUIShader::GetBaseTextureResolution(Fvector2& res)
{
    const auto texture = GetBaseTexture();
    if (!texture)
    {
        res = {};
        return false;
    }

    res = { float(texture->get_Width()), float(texture->get_Height()) };
    return true;
}
} // namespace xray::render::RENDER_NAMESPACE
