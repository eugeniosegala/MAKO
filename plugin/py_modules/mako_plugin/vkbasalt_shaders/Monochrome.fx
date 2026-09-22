/*------------------.
| :: Description :: |
'-------------------/

	Monochrome (version 1.1)

	Author: CeeJay.dk
	License: MIT

	About:
	Removes color making everything monochrome.
*/

#include "ReShade.fxh"

uniform int Monochrome_preset <
	ui_type = "combo";
	ui_label = "Preset";
	ui_tooltip = "Choose a preset";
	ui_items = "Custom\0"
	"Monitor or modern TV\0"
	"Equal weight\0"
	"Agfa 200X\0"
	"Agfapan 25\0"
	"Agfapan 100\0"
	"Agfapan 400\0"
	"Ilford Delta 100\0"
	"Ilford Delta 400\0"
	"Ilford Delta 400 Pro & 3200\0"
	"Ilford FP4\0"
	"Ilford HP5\0"
	"Ilford Pan F\0"
	"Ilford SFX\0"
	"Ilford XP2 Super\0"
	"Kodak Tmax 100\0"
	"Kodak Tmax 400\0"
	"Kodak Tri-X\0";
> = 0;

uniform float3 Monochrome_conversion_values <
	ui_type = "color";
	ui_label = "Custom Conversion values";
> = float3(0.21, 0.72, 0.07);

uniform float Monochrome_color_saturation <
	ui_type = "slider";
	ui_label = "Saturation";
	ui_min = 0.0; ui_max = 1.0;
> = 0.0;

float3 MonochromePass(float4 vpos : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
	float3 color = tex2D(ReShade::BackBuffer, texcoord).rgb;

	float3 Coefficients_array[18] =
	{
		Monochrome_conversion_values,
		float3(0.21, 0.72, 0.07),
		float3(0.3333333, 0.3333334, 0.3333333),
		float3(0.18, 0.41, 0.41),
		float3(0.25, 0.39, 0.36),
		float3(0.21, 0.40, 0.39),
		float3(0.20, 0.41, 0.39),
		float3(0.21, 0.42, 0.37),
		float3(0.22, 0.42, 0.36),
		float3(0.31, 0.36, 0.33),
		float3(0.28, 0.41, 0.31),
		float3(0.23, 0.37, 0.40),
		float3(0.33, 0.36, 0.31),
		float3(0.36, 0.31, 0.33),
		float3(0.21, 0.42, 0.37),
		float3(0.24, 0.37, 0.39),
		float3(0.27, 0.36, 0.37),
		float3(0.25, 0.35, 0.40)
	};

	float3 grey = dot(Coefficients_array[Monochrome_preset], color);
	color = lerp(grey, color, Monochrome_color_saturation);
	return saturate(color);
}

technique Monochrome
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = MonochromePass;
	}
}
