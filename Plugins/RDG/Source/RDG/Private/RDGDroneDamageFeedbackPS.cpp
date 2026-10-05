#include "RDGDroneDamageFeedbackPS.h"

bool FRDGDroneDamageFeedbackPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDroneDamageFeedbackPS, "/Plugin/RDG/DroneDamageFeedback.usf", "MainPS", SF_Pixel);
