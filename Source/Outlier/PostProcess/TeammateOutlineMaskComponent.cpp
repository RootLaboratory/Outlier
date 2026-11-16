#include "PostProcess/TeammateOutlineMaskComponent.h"

#include "Engine/SkeletalMesh.h"
#include "PSOPrecache.h"
#include "Rendering/CustomRenderPass.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneView.h"
#include "SkeletalMeshSceneProxy.h"
#include "SkeletalRenderPublic.h"

namespace
{
	class FTeammateOutlineMaskSceneProxy final : public FSkeletalMeshSceneProxy
	{
	public:
		FTeammateOutlineMaskSceneProxy(const USkinnedMeshComponent* Component, FSkeletalMeshRenderData* RenderData)
			: FSkeletalMeshSceneProxy(Component, RenderData)
		{
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			// CaptureOnly만 설정하면 다른 SceneCapture에도 해치가 중복된다.
			// 예약한 제외 마스크 View에서만 그려 반사·일반 깊이·다른 캡처에 섞이지 않게 한다.
			if (!View->CustomRenderPass || View->CustomRenderPass->GetDebugName() != TEXT("OutlierTurretExcludedMask"))
			{
				return FPrimitiveViewRelevance();
			}
			return FSkeletalMeshSceneProxy::GetViewRelevance(View);
		}
	};
}

UTeammateOutlineMaskComponent::UTeammateOutlineMaskComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	SetRenderCustomDepth(false);
	SetVisibleInRayTracing(false);
	SetAffectDynamicIndirectLighting(false);
	SetAffectDistanceFieldLighting(false);
	SetReceivesDecals(false);
	SetVisibleInSceneCaptureOnly(true);
	ComponentTags.Add(TEXT("Outlier.StencilExclude"));
	// 별도 포즈 평가 없이 Leader의 본 변환·LOD·bounds를 그대로 사용한다.
	bUseBoundsFromLeaderPoseComponent = true;
	bSyncAttachParentLOD = true;
	bIgnoreLeaderPoseComponentLOD = false;
}

bool UTeammateOutlineMaskComponent::InitializeFrom(USkeletalMeshComponent* Source)
{
	if (IsRegistered() || !IsValid(Source) || !Source->GetSkeletalMeshAsset()) return false;

	SetupAttachment(Source);
	SetRelativeTransform(FTransform::Identity);
	SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
	SetLeaderPoseComponent(Source, true, false);
	SetOnlyOwnerSee(Source->bOnlyOwnerSee);
	SetOwnerNoSee(Source->bOwnerNoSee);
	for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
	{
		SetMaterial(Index, Source->GetMaterial(Index));
	}
	return true;
}

FPrimitiveSceneProxy* UTeammateOutlineMaskComponent::CreateSceneProxy()
{
	FSkeletalMeshRenderData* RenderData = GetSkeletalMeshRenderData();
	if (CheckPSOPrecachingAndBoostPriority()
		&& GetPSOPrecacheProxyCreationStrategy() == EPSOPrecacheProxyCreationStrategy::DelayUntilPSOPrecached)
	{
		return nullptr;
	}
	// 준비되지 않은 스킨 자원으로 생성하지 않는다. 실패 시 해당 터렛의 팀원 출력만 보류한다.
	// Nanite 전용 프록시를 일반 스킨 프록시로 잘못 해석하는 것도 방지한다.
	if (!RenderData || !RenderData->LODRenderData.IsValidIndex(GetPredictedLODLevel())
		|| bHideSkin || !MeshObject || MeshObject->IsNaniteMesh())
	{
		return nullptr;
	}
	return new FTeammateOutlineMaskSceneProxy(this, RenderData);
}
