#include "RDGExplosionVolumeVisualizePS.h"

bool FRDGExplosionVolumeVisualizePS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGExplosionVolumeVisualizePS, "/Plugin/RDG/ExplosionVolumeVisualize.usf", "MainPS", SF_Pixel);
