#pragma once
#include "RDGExplosionVolumeCS.h"

bool FExplosionVolumeCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FExplosionVolumeCS, "/Plugin/RDG/ExplosionVolume.usf", "MainCS", SF_Compute);

