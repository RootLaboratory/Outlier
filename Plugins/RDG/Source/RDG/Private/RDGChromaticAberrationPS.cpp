#include "RDGChromaticAberrationPS.h"

bool FRGDChromaticAberrationPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRGDChromaticAberrationPS, "/Plugin/RDG/UIChromatic.usf", "MainPS", SF_Pixel);
