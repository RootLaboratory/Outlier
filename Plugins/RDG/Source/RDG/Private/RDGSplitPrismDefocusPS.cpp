#include "RDGSplitPrismDefocusPS.h"

bool FRDGSplitPrismDefocusPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGSplitPrismDefocusPS, "/Plugin/RDG/SplitPrismDefocus.usf", "MainPS", SF_Pixel);
