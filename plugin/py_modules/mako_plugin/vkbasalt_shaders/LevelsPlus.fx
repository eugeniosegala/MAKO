// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MAKO contributors
// MAKO's lightweight levels effect; independently implemented.

uniform float BlackPoint <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 0.25;
    ui_tooltip = "Input value mapped to black.";
> = 0.02;

uniform float WhitePoint <
    ui_type = "slider";
    ui_min = 0.75; ui_max = 1.0;
    ui_tooltip = "Input value mapped to white.";
> = 0.98;

uniform float Gamma <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 2.0;
    ui_tooltip = "Adjusts midtones after setting black and white points.";
> = 1.0;

#include "ReShade.fxh"

float3 LevelsPlusPass(float4 position : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
    float3 color = tex2D(ReShade::BackBuffer, texcoord).rgb;
    float range = max(WhitePoint - BlackPoint, 0.01);
    return pow(saturate((color - BlackPoint) / range), 1.0 / Gamma);
}

technique LevelsPlus
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = LevelsPlusPass;
    }
}
