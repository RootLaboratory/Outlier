#include "Enemy/OutlierBoneSight.h"

#include "Enemy/OutlierSightBoneCache.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("Outlier Sight"), STATGROUP_OutlierSight, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Bone visibility query"), STAT_OutlierBoneSightQuery, STATGROUP_OutlierSight);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Bone traces"), STAT_OutlierBoneSightTraces, STATGROUP_OutlierSight);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Observed pose finalizations"), STAT_OutlierSightPoses, STATGROUP_OutlierSight);

void FOutlierBoneSight::MarkPoseReady()
{
	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		PoseAsset = Mesh->GetSkeletalMeshAsset();
		PoseTime = Mesh->GetWorld()->GetTimeSeconds();
		INC_DWORD_STAT(STAT_OutlierSightPoses);
	}
}

void FOutlierBoneSight::Shutdown()
{
	if (USkeletalMeshComponent* Mesh = ActiveMesh.Get())
	{
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(PoseFinalizedHandle);
		if (UWorld* World = Mesh->GetWorld()) World->GetTimerManager().ClearTimer(LeaseTimer);
		Mesh->SetForcedLOD(OriginalForcedLOD);
		Mesh->bEnableUpdateRateOptimizations = bOriginalUpdateRateOptimizations;
		bPoseRefreshRequested = false;
		if (RefreshPolicy) RefreshPolicy();
		else Mesh->VisibilityBasedAnimTickOption = static_cast<EVisibilityBasedAnimTickOption>(OriginalTickOption);
	}
	bPoseRefreshRequested = false;
	ActiveMesh.Reset();
	PoseAsset.Reset();
	PoseTime = -1.0;
	RefreshPolicy = {};
	PoseFinalizedHandle.Reset();
}

UAISense_Sight::EVisibilityResult FOutlierBoneSight::Query(AActor& Target,
	USkeletalMeshComponent* Mesh, const FOutlierSightBoneCache& Registration,
	const FCanBeSeenFromContext& Context, FVector& SeenLocation, int32& SyncChecks,
	int32& AsyncChecks, float& Strength, TFunction<void()> RefreshPosePolicy)
{
	SCOPE_CYCLE_COUNTER(STAT_OutlierBoneSightQuery);
	AsyncChecks = 0;
	Strength = 0.0f;
	SeenLocation = FVector::ZeroVector;
	UWorld* World = Target.GetWorld();
	if (!Target.HasAuthority() || !World || !Mesh || !Registration.IsValid())
	{
		Shutdown();
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}

	if (ActiveMesh.Get() != Mesh || !bPoseRefreshRequested)
	{
		Shutdown();
		ActiveMesh = Mesh;
		OriginalTickOption = static_cast<uint8>(Mesh->VisibilityBasedAnimTickOption);
		OriginalForcedLOD = Mesh->GetForcedLOD();
		bOriginalUpdateRateOptimizations = Mesh->bEnableUpdateRateOptimizations;
		RefreshPolicy = MoveTemp(RefreshPosePolicy);
		bPoseRefreshRequested = true;
		PoseFinalizedHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateWeakLambda(&Target, [this] { MarkPoseReady(); }));
		// 정책 변경은 활성화/해제 때만 한다. 관측자마다 그림자·무기 표시 설정을 다시 적용하지 않는다.
		if (RefreshPolicy) RefreshPolicy();
		else Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		// 화면 거리 LOD나 URO가 등록 본을 기준 자세로 남기지 않도록 활성 조회 기간에만 제한한다.
		Mesh->SetForcedLOD(1);
		Mesh->bEnableUpdateRateOptimizations = false;
	}
	// 애니메이션은 기존 메시 Tick에서만 진행한다. 여러 적의 Sight 조회가 시간을 중복 진행하지 않는다.
	World->GetTimerManager().SetTimer(LeaseTimer,
		FTimerDelegate::CreateWeakLambda(&Target, [this] { Shutdown(); }), 1.0f, false);

	// 최초 조회와 메시 교체 직후에는 새 포즈 완료를 기다린다. 오래된 본/기준 자세로 성공하지 않는다.
	const double Age = World->GetTimeSeconds() - PoseTime;
	if (PoseAsset.Get() != Mesh->GetSkeletalMeshAsset() || PoseTime < 0.0 || Age < 0.0 || Age > 0.25)
	{
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}
	const TConstArrayView<int32> Bones = Registration.GetBoneIndices();
	TArray<FVector, TInlineAllocator<16>> Locations;
	for (const int32 Bone : Bones)
	{
		// GetBoneTransform은 범위 밖 인덱스에서 컴포넌트 위치를 반환한다. 호출 전에 명시적으로 거부한다.
		if (!Mesh->GetComponentSpaceTransforms().IsValidIndex(Bone)) return UAISense_Sight::EVisibilityResult::NotVisible;
		const FTransform Transform = Mesh->GetBoneTransform(Bone);
		if (Transform.ContainsNaN())
		{
			if (!bWarnedInvalidTransform)
			{
				UE_LOG(LogTemp, Warning, TEXT("Invalid sight bone transform Target=%s Bone=%d"), *Target.GetName(), Bone);
				bWarnedInvalidTransform = true;
			}
			return UAISense_Sight::EVisibilityResult::NotVisible;
		}
		Locations.Add(Transform.GetLocation());
	}
	bWarnedInvalidTransform = false;
	if (Context.ObserverLocation.ContainsNaN() || Bones.IsEmpty()) return UAISense_Sight::EVisibilityResult::NotVisible;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OutlierBoneSight), true, Context.IgnoreActor);
	int32 Visible = 0;
	for (const FVector& Location : Locations)
	{
		FHitResult Hit;
		++SyncChecks;
		INC_DWORD_STAT(STAT_OutlierBoneSightTraces);
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Context.ObserverLocation, Location, ECC_Visibility, Params);
		if (!bBlocked || Hit.GetActor() == &Target)
		{
			if (Visible == 0) SeenLocation = Location;
			++Visible;
		}
	}
	// 전체 본을 검사해 Strength도 실제 표본 비율로 보고한다. 기준 이상에서의 발견 속도는 별도 계약이다.
	if (Visible >= Bones.Num() / 2 + Bones.Num() % 2)
	{
		Strength = static_cast<float>(Visible) / Bones.Num();
		return UAISense_Sight::EVisibilityResult::Visible;
	}
	SeenLocation = FVector::ZeroVector;
	return UAISense_Sight::EVisibilityResult::NotVisible;
}
