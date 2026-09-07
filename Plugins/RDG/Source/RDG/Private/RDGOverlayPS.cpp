#include "RDGOverlayPS.h"

bool FRDGOverlayPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGOverlayPS, "/Plugin/RDG/Overlay.usf", "MainPS", SF_Pixel);
