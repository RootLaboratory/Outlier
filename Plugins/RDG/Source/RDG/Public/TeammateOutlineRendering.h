#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class UPrimitiveComponent;
class FSceneView;
class FSceneViewFamily;
struct FScreenPassTexture;

DECLARE_DELEGATE_OneParam(FOnTeammateOutlineMasksQueued, bool);

/** 게임 모듈이 소유 관계를 판정하고, 렌더러는 캐시된 메시만 사용한다. */
struct RDG_API FTeammateOutlineViewData
{
	TArray<TWeakObjectPtr<UPrimitiveComponent>> TargetMeshes;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> FirstPersonMeshes;
	// GT에서 해당 View의 마스크 등록 결과를 알린다. 렌더 스레드에는 전달하지 않는다.
	FOnTeammateOutlineMasksQueued OnMasksQueued;
	FLinearColor Color = FLinearColor(0.1f, 0.8f, 1.0f);
	float VisibleThickness = 1.5f;
	float OccludedThickness = 1.0f;
	float VisibleStrength = 0.8f;
	float OccludedStrength = 0.3f;
	bool bEnabled = false;
	int32 DebugMode = 0;
};

struct FTeammateOutlineViewResources;
using FTeammateOutlineResources = TSharedPtr<FTeammateOutlineViewResources, ESPMode::ThreadSafe>;

class RDG_API FRDGTeammateOutlineRendering
{
public:
	/** Game thread: 본 View 생성 중 깊이 패스를 예약한다. UObject는 여기서만 읽는다. */
	static FTeammateOutlineResources QueueMasks(
		FSceneViewFamily& Family, const FSceneView& View, const FTeammateOutlineViewData& Data);

	/** Render thread: 같은 RDG 그래프에서 만든 마스크만 합성한다. */
	static FScreenPassTexture Composite(
		FRDGBuilder& GraphBuilder, const FSceneView& View, FScreenPassTexture SceneColor,
		FRDGTextureRef SceneDepth, const FTeammateOutlineResources& Resources);
};
