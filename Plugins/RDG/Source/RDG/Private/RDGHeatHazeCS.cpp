#include "RDGHeatHazeCS.h"

bool FRDGHeatHazeFieldCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGHeatHazeFieldCS, "/Plugin/RDG/HeatHazeField.usf", "MainCS", SF_Compute);
