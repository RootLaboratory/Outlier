#include "RDGSplitPrismPS.h"

bool FRDGSplitPrismPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGSplitPrismPS, "/Plugin/RDG/SplitPrism.usf", "MainPS", SF_Pixel);
