#include "RDGDeathColorLerpPS.h"

bool FRDGDeathColorLerpPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDeathColorLerpPS, "/Plugin/RDG/DeathColorLerp.usf", "MainPS", SF_Pixel);
