#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "UObject/WeakObjectPtr.h"

class UPrimitiveComponent;
class AActor;
class FSceneView;
class FSceneViewFamily;
struct FScreenPassTexture;

DECLARE_DELEGATE_OneParam(FOnTeammateOutlineMasksQueued, bool);
DECLARE_DELEGATE_RetVal(bool, FIsTeammateOutlineTargetEligible);
DECLARE_DELEGATE_TwoParams(FOnTurretOutlineMasksQueued, const TArray<TWeakObjectPtr<AActor>>&, bool);

/** 대상별 소유권은 GT에서 검증한다. 제외 깊이는 같은 대상의 전체 깊이와만 비교한다. */
struct RDG_API FTeammateOutlineTargetData
{
	TWeakObjectPtr<AActor> Owner;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Meshes;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ExcludedMeshes;
	FIsTeammateOutlineTargetEligible IsEligible;
	bool bHasExclusions = false;
	bool bExclusionResourcesReady = false;
};

/** 게임 모듈이 소유 관계를 판정하고, 렌더러는 캐시된 메시만 사용한다. */
struct RDG_API FTeammateOutlineViewData
{
	TArray<TWeakObjectPtr<UPrimitiveComponent>> TargetMeshes;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> FirstPersonMeshes;
	TArray<FTeammateOutlineTargetData> TurretTargets;
	// GT에서 해당 View의 마스크 등록 결과를 알린다. 렌더 스레드에는 전달하지 않는다.
	FOnTeammateOutlineMasksQueued OnMasksQueued;
	FOnTurretOutlineMasksQueued OnTurretMasksQueued;
	FLinearColor Color = FLinearColor(0.1f, 0.8f, 1.0f);
	float VisibleThickness = 1.5f;
	float OccludedThickness = 1.0f;
	float VisibleStrength = 0.8f;
	float OccludedStrength = 0.3f;
	// 기존 머티리얼과 같은 샘플 범위로 Guide/Melee 선을 보호한다.
	float LegacyThickness = 1.0f;
	float LegacyDepthTolerance = 1.0f;
	float LegacyDepthEdgeThreshold = 1.0f;
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
		FSceneViewFamily& Family, const FSceneView& View, const FTeammateOutlineViewData& Data,
		TArray<TWeakObjectPtr<AActor>>* OutSuccessfulTurrets = nullptr);
	static bool HasPairMasks(const FTeammateOutlineResources& Resources);

	/** Render thread: 같은 RDG 그래프에서 만든 마스크만 합성한다. */
	static FScreenPassTexture Composite(
		FRDGBuilder& GraphBuilder, const FSceneView& View, FScreenPassTexture SceneColor,
		FRDGTextureRef SceneDepth, const FTeammateOutlineResources& Resources,
		FRDGTextureRef CustomDepth = nullptr, FRDGTextureSRVRef CustomStencil = nullptr);
};
