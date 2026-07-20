#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGADSWeaponMaskCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGADSWeaponMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FRDGADSWeaponMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HardMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutMask)
		SHADER_PARAMETER(FIntPoint, TextureExtent)
		SHADER_PARAMETER(FIntPoint, SourceViewRectMin)
		SHADER_PARAMETER(uint32, WeaponStencilValue)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(uint32, PassMode)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
