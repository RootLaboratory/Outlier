#pragma once

#include "CoreMinimal.h"

class FRDGADSWeaponMaskPass
{
public:
	static FRDGTextureRef AddBuildMaskPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureSRVRef CustomStencilTexture,
		FIntPoint TextureExtent,
		uint32 WeaponStencilValue);

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
	static FRDGTextureRef AddMaskPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureSRVRef CustomStencilTexture,
		FRDGTextureRef InputMask,
		FRDGTextureRef HardMask,
		FIntPoint TextureExtent,
		uint32 WeaponStencilValue,
		float Radius,
		uint32 PassMode,
		const TCHAR* TextureName,
		const TCHAR* EventName);
};
