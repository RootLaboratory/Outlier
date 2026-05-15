#include "RDGHeatHazeCompositePS.h"

bool FRDGHeatHazeCompositePS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGHeatHazeCompositePS, "/Plugin/RDG/HeatHazeComposite.usf", "MainPS", SF_Pixel);
