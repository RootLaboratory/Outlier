#include "RDGDeathNoisePS.h"

bool FRDGDeathNoisePS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDeathNoisePS, "/Plugin/RDG/DeathNoise.usf", "MainPS", SF_Pixel);
