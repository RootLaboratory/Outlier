#pragma once

#include "CoreMinimal.h"
#include "Save/OutlierLoadoutSnapshot.h"
#include "OutlierCheckpointSnapshot.generated.h"

class USkeletalMesh;

UENUM(BlueprintType)
enum class EOutlierWorldProgressType : uint8
{
	CollectedNode,
	HackedObject,
	OpenedDoor,
	ActivatedSwitch,
	CompletedEncounter,
	ExplodedProp
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierWorldProgressSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TSet<FName> CollectedNodeIds;

	UPROPERTY()
	TSet<FName> HackedObjectIds;

	UPROPERTY()
	TSet<FName> OpenedDoorIds;

	UPROPERTY()
	TSet<FName> ActivatedSwitchIds;

	UPROPERTY()
	TSet<FName> CompletedEncounterIds;

	UPROPERTY()
	TSet<FName> ExplodedPropIds;

	TSet<FName>& GetIds(EOutlierWorldProgressType Type)
	{
		switch (Type)
		{
		case EOutlierWorldProgressType::CollectedNode:
			return CollectedNodeIds;
		case EOutlierWorldProgressType::HackedObject:
			return HackedObjectIds;
		case EOutlierWorldProgressType::OpenedDoor:
			return OpenedDoorIds;
		case EOutlierWorldProgressType::ActivatedSwitch:
			return ActivatedSwitchIds;
		case EOutlierWorldProgressType::CompletedEncounter:
			return CompletedEncounterIds;
		case EOutlierWorldProgressType::ExplodedProp:
		default:
			return ExplodedPropIds;
		}
	}

	const TSet<FName>& GetIds(EOutlierWorldProgressType Type) const
	{
		switch (Type)
		{
		case EOutlierWorldProgressType::CollectedNode:
			return CollectedNodeIds;
		case EOutlierWorldProgressType::HackedObject:
			return HackedObjectIds;
		case EOutlierWorldProgressType::OpenedDoor:
			return OpenedDoorIds;
		case EOutlierWorldProgressType::ActivatedSwitch:
			return ActivatedSwitchIds;
		case EOutlierWorldProgressType::CompletedEncounter:
			return CompletedEncounterIds;
		case EOutlierWorldProgressType::ExplodedProp:
		default:
			return ExplodedPropIds;
		}
	}

	void Reset()
	{
		CollectedNodeIds.Reset();
		HackedObjectIds.Reset();
		OpenedDoorIds.Reset();
		ActivatedSwitchIds.Reset();
		CompletedEncounterIds.Reset();
		ExplodedPropIds.Reset();
	}
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierRoleProgressSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NodeCount = 0;

	UPROPERTY()
	TArray<FName> ActivatedUpgradeNodeIds;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierSuitSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	bool bAcquired = false;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> FirstPersonMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> ThirdPersonMesh;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierCheckpointSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	FName CheckpointId = NAME_None;

	UPROPERTY()
	bool bInitialSnapshot = false;

	UPROPERTY()
	FTransform ShooterSpawnTransform = FTransform::Identity;

	UPROPERTY()
	FTransform PartnerSpawnTransform = FTransform::Identity;

	UPROPERTY()
	FOutlierRoleProgressSnapshot ShooterProgress;

	UPROPERTY()
	FOutlierRoleProgressSnapshot PartnerProgress;

	UPROPERTY()
	FOutlierLoadoutSnapshot LoadoutSnapshot;

	UPROPERTY()
	FOutlierSuitSnapshot SuitSnapshot;

	UPROPERTY()
	FOutlierWorldProgressSnapshot WorldProgress;

	bool IsValid() const
	{
		return bInitialSnapshot || !CheckpointId.IsNone();
	}
};
