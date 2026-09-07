#pragma once

#include "CoreMinimal.h"

struct FADSBlurParameters;

class FRDGAdsSightMaskPass
{
public:
	static FRDGTextureRef AddBuildMaskPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureSRVRef CustomStencilTexture,
		FRDGTextureRef CustomDepthTexture,
		FIntPoint TextureExtent,
		const FADSBlurParameters& Parameters);

	static FRDGTextureRef AddDilatePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef InputMask,
		FIntPoint TextureExtent,
		float Radius);

	static FRDGTextureRef AddSoftenPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef InputMask,
		FRDGTextureRef HardMask,
		FIntPoint TextureExtent,
		float Radius);

private:
	static FRDGTextureRef AddIntegerMaskPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureSRVRef CustomStencilTexture,
		FRDGTextureRef CustomDepthTexture,
		FRDGTextureRef InputMask,
		FIntPoint TextureExtent,
		uint32 WeaponStencilValue,
		float FocusDistanceWorld,
		float SightDistanceThreshold,
		float Radius,
		uint32 PassMode,
		const TCHAR* TextureName,
		const TCHAR* EventName);

	static FRDGTextureRef AddSoftenMaskPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef InputMask,
		FRDGTextureRef HardMask,
		FIntPoint TextureExtent,
		float Radius,
		const TCHAR* TextureName,
		const TCHAR* EventName);
};
