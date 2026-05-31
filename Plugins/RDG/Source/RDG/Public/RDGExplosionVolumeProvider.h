#pragma once

#include "CoreMinimal.h"
#include "FRDGExplosionVolumePass.h"
#include "RenderGraphFwd.h"
#include "RendererInterface.h"

struct FRDGExplosionVolumeFrameData
{
	TRefCountPtr<IPooledRenderTarget> Texture;
	FIntVector TextureSize = FIntVector::ZeroValue;
	float MaxDepth = RDGExplosionVolume::MaxDepth;
};

class RDG_API FRDGExplosionVolumeProvider
{
public:
	static void QueueExtraction(FRDGBuilder& GraphBuilder, FRDGTextureRef VelocityVolume);
	static FRDGExplosionVolumeFrameData Get_RenderThread();
	static void Release_RenderThread();
};
