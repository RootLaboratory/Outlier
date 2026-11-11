#pragma once

#include "CoreMinimal.h"
#include "Perception/AISightTargetInterface.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtr.h"

class USkeletalMeshComponent;
class USkeletalMesh;
struct FOutlierSightBoneCache;

// 호출 대상이 명시한 역할만 사용한다. Partner/VEC 등 기존 호출은 머리 예외를 받지 않는다.
struct FOutlierSightBoneRoles
{
	FName Head = NAME_None;
	TConstArrayView<FName> Body;
};

struct OUTLIER_API FOutlierSightObservation
{
	TArray<FVector, TInlineAllocator<8>> BodyPoints;
	TArray<FVector, TInlineAllocator<8>> HeadPoints;
	TArray<FVector, TInlineAllocator<8>> OtherPoints;
	TArray<int32, TInlineAllocator<8>> BodyBoneIndices;
	TArray<int32, TInlineAllocator<8>> HeadBoneIndices;
	TArray<int32, TInlineAllocator<8>> OtherBoneIndices;
	// Sight가 인지 위치로 반환한 실제 TP 본. 조준도 이 관측 부위부터 이어받는다.
	int32 SeenBoneIndex = INDEX_NONE;
	double ObservedAt = -1.0;
	double PoseAt = -1.0;
	uint32 Epoch = 0;
	bool bSightVisible = false;
};

// 대상마다 포즈 lease 하나를 공유하되, 노출 결과는 실제 관측자별로 분리한다.
struct OUTLIER_API FOutlierBoneSight
{
	UAISense_Sight::EVisibilityResult Query(AActor& Target, USkeletalMeshComponent* Mesh,
		const FOutlierSightBoneCache& Registration, const FCanBeSeenFromContext& Context,
		FVector& SeenLocation, int32& SyncChecks, int32& AsyncChecks, float& Strength,
		TFunction<void()> RefreshPosePolicy = {}, const FOutlierSightBoneRoles* Roles = nullptr);
	bool GetObservation(const AActor* Observer, const USkeletalMeshComponent* Mesh,
		const FOutlierSightBoneCache& Registration, FOutlierSightObservation& Out) const;
	bool IsPoseRefreshRequested() const { return bPoseRefreshRequested; }
	void Shutdown();

private:
	struct FObserverSnapshot
	{
		FOutlierSightObservation Observation;
		TWeakObjectPtr<USkeletalMesh> MeshAsset;
		uint32 RegistrationRevision = 0;
		uint32 ObserverSerial = 0;
	};
	void MarkPoseReady();
	TMap<TWeakObjectPtr<const AActor>, FObserverSnapshot> Observations;
	uint32 ObservationEpoch = 0;
	uint32 ObservationRegistrationRevision = 0;
	TWeakObjectPtr<USkeletalMesh> ObservationAsset;
	TWeakObjectPtr<USkeletalMeshComponent> ActiveMesh;
	TWeakObjectPtr<USkeletalMesh> PoseAsset;
	FDelegateHandle PoseFinalizedHandle;
	FTimerHandle LeaseTimer;
	TFunction<void()> RefreshPolicy;
	double PoseTime = -1.0;
	uint8 OriginalTickOption = 0;
	int32 OriginalForcedLOD = 0;
	bool bOriginalUpdateRateOptimizations = false;
	bool bPoseRefreshRequested = false;
	bool bWarnedInvalidTransform = false;
};
