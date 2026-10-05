#include "RDGDeathBlackPS.h"

bool FRDGDeathBlackPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDeathBlackPS, "/Plugin/RDG/DeathBlack.usf", "MainPS", SF_Pixel);
