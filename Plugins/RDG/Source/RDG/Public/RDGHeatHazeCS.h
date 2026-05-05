#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGHeatHazeFieldCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGHeatHazeFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FRDGHeatHazeFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FHeatHazeSourceShaderData>, SourceDataBuffer)
		SHADER_PARAMETER(uint32, SourceCount)
		SHADER_PARAMETER(FVector2f, OutputExtent)
		SHADER_PARAMETER(float, Time)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutRefractionTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
