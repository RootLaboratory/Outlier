#include "RDGPartnerKnockDownPS.h"

bool FRDGPartnerKnockDownMaskPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

bool FRDGPartnerKnockDownPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGPartnerKnockDownMaskPS, "/Plugin/RDG/PartnerKnockDownMask.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FRDGPartnerKnockDownPS, "/Plugin/RDG/PartnerKnockDown.usf", "MainPS", SF_Pixel);
