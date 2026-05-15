#pragma once
#include "RDGDatamoshingPS.h"

 bool FRDGDatamoshingPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDatamoshingPS, "/Plugin/RDG/Datamoshing.usf", "MainPS", SF_Pixel);
