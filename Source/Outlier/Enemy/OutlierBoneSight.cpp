#include "Enemy/OutlierBoneSight.h"

#include "Enemy/OutlierSightBoneCache.h"
#include "Enemy/EnemySightAwareness.h"
#include "Enemy/EnemyBase.h"
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
	Observations.Reset();
	++ObservationEpoch;
	ObservationRegistrationRevision = 0;
	ObservationAsset.Reset();
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
	int32& AsyncChecks, float& Strength, TFunction<void()> RefreshPosePolicy,
	const FOutlierSightBoneRoles* Roles)
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

	// 성공/실패와 관계없이 이번 관측으로 교체한다. 가려진 뒤 옛 노출 점이 남지 않는다.
	if (ObservationRegistrationRevision != Registration.GetRevision()
		|| ObservationAsset.Get() != Mesh->GetSkeletalMeshAsset())
	{
		Observations.Reset();
		++ObservationEpoch;
		ObservationRegistrationRevision = Registration.GetRevision();
		ObservationAsset = Mesh->GetSkeletalMeshAsset();
	}
	for (auto It = Observations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || World->GetTimeSeconds() - It.Value().Observation.ObservedAt > 0.25)
			It.RemoveCurrent();
	}
	FObserverSnapshot* Snapshot = nullptr;
	if (Roles && IsValid(Context.IgnoreActor))
	{
		const TWeakObjectPtr<const AActor> Key(Context.IgnoreActor);
		Snapshot = Observations.Find(Key);
		// 대상당 저장량도 제한한다. 상한에서는 새 관측자의 사격을 보류한다.
		if (!Snapshot && Observations.Num() < 64) Snapshot = &Observations.Add(Key);
		if (Snapshot)
		{
			*Snapshot = FObserverSnapshot();
			Snapshot->RegistrationRevision = Registration.GetRevision();
			if (const AEnemyBase* EnemyObserver = Cast<AEnemyBase>(Context.IgnoreActor))
				Snapshot->ObserverSerial = EnemyObserver->GetCombatAimObservationSerial();
			Snapshot->MeshAsset = Mesh->GetSkeletalMeshAsset();
			Snapshot->Observation.ObservedAt = World->GetTimeSeconds();
			Snapshot->Observation.Epoch = ObservationEpoch;
		}
	}

	// 최초 조회와 메시 교체 직후에는 새 포즈 완료를 기다린다. 오래된 본/기준 자세로 성공하지 않는다.
	const double Age = World->GetTimeSeconds() - PoseTime;
	if (PoseAsset.Get() != Mesh->GetSkeletalMeshAsset() || PoseTime < 0.0 || Age < 0.0 || Age > 0.25)
	{
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}
	const TConstArrayView<int32> Bones = Registration.GetBoneIndices();
	int32 HeadPosition = INDEX_NONE;
	TArray<int32, TInlineAllocator<8>> BodyPositions;
	if (Roles)
	{
		// 역할표와 실제 등록 목록의 교집합만 사용한다. 본 목록 override에도 누락 역할을 임의로 생성하지 않는다.
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const FName Name = Mesh->GetBoneName(Bones[Index]);
			if (!Roles->Head.IsNone() && Name == Roles->Head) HeadPosition = Index;
			if (Roles->Body.Contains(Name)) BodyPositions.Add(Index);
		}
		if (BodyPositions.Contains(HeadPosition))
			return UAISense_Sight::EVisibilityResult::NotVisible;
	}
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
	bool bHeadVisible = false;
	for (int32 Index = 0; Index < Locations.Num(); ++Index)
	{
		const FVector& Location = Locations[Index];
		FHitResult Hit;
		++SyncChecks;
		INC_DWORD_STAT(STAT_OutlierBoneSightTraces);
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Context.ObserverLocation, Location, ECC_Visibility, Params);
		if (!bBlocked || Hit.GetActor() == &Target)
		{
			if (Visible == 0)
			{
				SeenLocation = Location;
				if (Snapshot) Snapshot->Observation.SeenBoneIndex = Bones[Index];
			}
			++Visible;
			bHeadVisible |= Index == HeadPosition;
			if (Snapshot)
			{
				if (Index == HeadPosition)
				{
					Snapshot->Observation.HeadPoints.Add(Location);
					Snapshot->Observation.HeadBoneIndices.Add(Bones[Index]);
				}
				else if (BodyPositions.Contains(Index))
				{
					Snapshot->Observation.BodyPoints.Add(Location);
					Snapshot->Observation.BodyBoneIndices.Add(Bones[Index]);
				}
				else
				{
					Snapshot->Observation.OtherPoints.Add(Location);
					Snapshot->Observation.OtherBoneIndices.Add(Bones[Index]);
				}
			}
		}
	}
	const bool bVisible = OutlierEnemySight::MeetsExposureThreshold(Bones.Num(), Visible, bHeadVisible);
	if (Snapshot)
	{
		Snapshot->Observation.PoseAt = PoseTime;
		Snapshot->Observation.bSightVisible = bVisible;
	}
	// 전체 본을 검사해 Strength도 실제 표본 비율로 보고한다. 기준 이상에서의 발견 속도는 별도 계약이다.
	if (bVisible)
	{
		Strength = static_cast<float>(Visible) / Bones.Num();
		return UAISense_Sight::EVisibilityResult::Visible;
	}
	SeenLocation = FVector::ZeroVector;
	return UAISense_Sight::EVisibilityResult::NotVisible;
}

bool FOutlierBoneSight::GetObservation(const AActor* Observer, const USkeletalMeshComponent* Mesh,
	const FOutlierSightBoneCache& Registration, FOutlierSightObservation& Out) const
{
	Out = FOutlierSightObservation();
	if (!IsValid(Observer) || !Mesh || !Registration.IsValid() || !bPoseRefreshRequested
		|| ActiveMesh.Get() != Mesh || PoseAsset.Get() != Mesh->GetSkeletalMeshAsset()) return false;
	const FObserverSnapshot* Snapshot = Observations.Find(TWeakObjectPtr<const AActor>(Observer));
	const AEnemyBase* EnemyObserver = Cast<AEnemyBase>(Observer);
	if (!Snapshot || Snapshot->RegistrationRevision != Registration.GetRevision()
		|| (EnemyObserver && Snapshot->ObserverSerial != EnemyObserver->GetCombatAimObservationSerial())
		|| Snapshot->MeshAsset.Get() != Mesh->GetSkeletalMeshAsset()
		|| Snapshot->Observation.Epoch != ObservationEpoch || !Mesh->GetWorld()) return false;
	const double Now = Mesh->GetWorld()->GetTimeSeconds();
	const FOutlierSightObservation& Observation = Snapshot->Observation;
	// 새 포즈가 완료됐어도 옛 관측 좌표의 수명은 연장하지 않는다. 조회는 Trace/본 평가를 하지 않는다.
	if (Observation.ObservedAt < 0.0 || Observation.PoseAt < 0.0
		|| Now < Observation.ObservedAt || Now < Observation.PoseAt
		|| Now - Observation.ObservedAt > 0.25 || Now - Observation.PoseAt > 0.25) return false;
	Out = Observation;
	return true;
}
