#pragma once

#include "CoreMinimal.h"
#include "Perception/AISightTargetInterface.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtr.h"

class USkeletalMeshComponent;
class USkeletalMesh;
struct FOutlierSightBoneCache;

// 대상마다 하나만 소유한다. 관측자별 가시성은 공유하지 않고 메시 갱신 요구만 공유한다.
struct OUTLIER_API FOutlierBoneSight
{
	UAISense_Sight::EVisibilityResult Query(AActor& Target, USkeletalMeshComponent* Mesh,
		const FOutlierSightBoneCache& Registration, const FCanBeSeenFromContext& Context,
		FVector& SeenLocation, int32& SyncChecks, int32& AsyncChecks, float& Strength,
		TFunction<void()> RefreshPosePolicy = {});
	bool IsPoseRefreshRequested() const { return bPoseRefreshRequested; }
	void Shutdown();

private:
	void MarkPoseReady();
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
