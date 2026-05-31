#include "RDGExplosionVolumeProvider.h"

#include "RenderGraphBuilder.h"

namespace
{
	TRefCountPtr<IPooledRenderTarget> GExplosionVolumeTexture;
}

void FRDGExplosionVolumeProvider::QueueExtraction(FRDGBuilder& GraphBuilder, FRDGTextureRef VelocityVolume)
{
	if (!VelocityVolume)
	{
		GExplosionVolumeTexture.SafeRelease();
		return;
	}

	GraphBuilder.QueueTextureExtraction(VelocityVolume, &GExplosionVolumeTexture, ERHIAccess::SRVMask);
}

void FRDGExplosionVolumeProvider::Release_RenderThread()
{
	GExplosionVolumeTexture.SafeRelease();
}

FRDGExplosionVolumeFrameData FRDGExplosionVolumeProvider::Get_RenderThread()
{
	FRDGExplosionVolumeFrameData FrameData;
	FrameData.Texture = GExplosionVolumeTexture;
	FrameData.MaxDepth = RDGExplosionVolume::MaxDepth; 
	if (FrameData.Texture.IsValid())
	{
		FrameData.TextureSize = FIntVector(
			RDGExplosionVolume::VolumeSize,
			RDGExplosionVolume::VolumeSize,
			RDGExplosionVolume::VolumeSize);
	}
	return FrameData;
}
