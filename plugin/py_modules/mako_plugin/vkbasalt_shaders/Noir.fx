// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MAKO contributors
//
// A high-contrast monochrome look with subtle warm toning and vignetting.

uniform float Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_tooltip = "Blends the noir look with the original image.";
> = 1.0;

#include "ReShade.fxh"

float3 NoirPass(float4 position : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
    float3 inputColor = tex2D(ReShade::BackBuffer, texcoord).rgb;
    float luma = dot(inputColor, float3(0.2126, 0.7152, 0.0722));
    float monochrome = saturate((luma - 0.5) * 1.32 + 0.5);
    float2 centered = texcoord * 2.0 - 1.0;
    float vignette = saturate(1.0 - dot(centered, centered) * 0.18);
    float3 processed = monochrome * vignette * float3(1.02, 1.0, 0.96);
    return lerp(inputColor, saturate(processed), Strength);
}

technique Noir
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = NoirPass;
    }
}
