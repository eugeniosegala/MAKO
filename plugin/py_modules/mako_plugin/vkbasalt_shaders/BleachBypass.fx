// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MAKO contributors
//
// A compact bleach-bypass-inspired finishing effect authored for MAKO.

uniform float Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_tooltip = "Blends the bleach-bypass look with the original image.";
> = 0.72;

#include "ReShade.fxh"

float3 BleachBypassPass(float4 position : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
    float3 inputColor = tex2D(ReShade::BackBuffer, texcoord).rgb;
    float luma = dot(inputColor, float3(0.2126, 0.7152, 0.0722));
    float3 muted = lerp(luma.xxx, inputColor, 0.38);
    float3 contrasted = muted * muted * (3.0 - 2.0 * muted);
    float3 processed = lerp(muted, contrasted, 0.68);
    return lerp(inputColor, saturate(processed), Strength);
}

technique BleachBypass
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = BleachBypassPass;
    }
}
