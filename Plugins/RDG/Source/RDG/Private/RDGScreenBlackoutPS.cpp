#include "RDGScreenBlackoutPS.h"

bool FRDGScreenBlackoutPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGScreenBlackoutPS, "/Plugin/RDG/ScreenBlackout.usf", "MainPS", SF_Pixel);
