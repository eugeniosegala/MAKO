// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MAKO contributors
// MAKO's lightweight local-contrast effect; independently implemented.

uniform float Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_tooltip = "Adds local contrast to midtone detail.";
> = 0.4;

#include "ReShade.fxh"

float3 ClarityPass(float4 position : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
    float3 center = tex2D(ReShade::BackBuffer, texcoord).rgb;
    float2 offset = BUFFER_PIXEL_SIZE * 2.0;
    float3 surround = (
        tex2D(ReShade::BackBuffer, texcoord + float2(offset.x, 0.0)).rgb +
        tex2D(ReShade::BackBuffer, texcoord - float2(offset.x, 0.0)).rgb +
        tex2D(ReShade::BackBuffer, texcoord + float2(0.0, offset.y)).rgb +
        tex2D(ReShade::BackBuffer, texcoord - float2(0.0, offset.y)).rgb
    ) * 0.25;
    float detail = dot(center - surround, float3(0.2126, 0.7152, 0.0722));
    float midtone = 1.0 - abs(dot(center, float3(0.2126, 0.7152, 0.0722)) * 2.0 - 1.0);
    return saturate(center + detail * midtone * Strength);
}

technique Clarity
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = ClarityPass;
    }
}
