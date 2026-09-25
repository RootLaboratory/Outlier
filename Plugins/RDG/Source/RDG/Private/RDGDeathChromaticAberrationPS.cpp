#include "RDGDeathChromaticAberrationPS.h"

bool FRDGDeathChromaticAberrationPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDeathChromaticAberrationPS, "/Plugin/RDG/DeathChromatic.usf", "MainPS", SF_Pixel);
