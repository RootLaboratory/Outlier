#pragma once

#include "CoreMinimal.h"
#include "Save/OutlierCheckpointSnapshot.h"
#include "ArenaTransitionPlacement.generated.h"

class AActor;
class APawn;
class ULevel;
class UWorld;

USTRUCT()
struct FArenaTransitionPlacementTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Placement")
	FName LevelPackage;

	UPROPERTY(EditAnywhere, Category = "Placement")
	FName ActorTag;

	const AActor* ResolveActor(const UWorld& World, FString& OutPendingTarget) const;

private:
	mutable TWeakObjectPtr<ULevel> CachedLevel;
	mutable TWeakObjectPtr<AActor> CachedActor;
	mutable int32 CachedLevelActorCount = INDEX_NONE;
};

USTRUCT()
struct FArenaTransitionPlacementBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Placement")
	TArray<FName> PresetIds;

	UPROPERTY(EditAnywhere, Category = "Placement")
	TArray<FName> CheckpointIds;

	UPROPERTY(EditAnywhere, Category = "Placement")
	TArray<FGameplayTag> RoomTags;

	// 비어 있으면 해당 방의 모든 전투 자동저장에 적용한다.
	UPROPERTY(EditAnywhere, Category = "Placement")
	TArray<EOutlierCheckpointSaveReason> RoomSaveReasons;

	UPROPERTY(EditAnywhere, Category = "Placement")
	TArray<FArenaTransitionPlacementTarget> Targets;
};

USTRUCT()
struct FArenaTransitionPlacementPart
{
	GENERATED_BODY()

	UPROPERTY()
	FString Path;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	uint8 PawnCollision = 0;
};

USTRUCT()
struct FArenaTransitionPlacementSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Revision = 0;

	// 이어하기 로그에서 서버와 클라이언트가 같은 저장과 배치 대상을 확인할 수 있게 한다.
	UPROPERTY()
	FName CheckpointId;

	UPROPERTY()
	FName SaveReason;

	UPROPERTY()
	FGameplayTag RoomTag;

	UPROPERTY()
	TArray<FArenaTransitionPlacementTarget> Targets;

	UPROPERTY()
	TArray<FArenaTransitionPlacementPart> Parts;
};

namespace OutlierArenaTransitionPlacement
{
	bool ResolveTargets(TConstArrayView<FArenaTransitionPlacementBinding> Bindings, FName PresetId,
		const FOutlierCheckpointSnapshot* RestoreSnapshot,
		TArray<FArenaTransitionPlacementTarget>& OutTargets, FString& OutReason);
	bool ReadTargets(const UWorld& World, const TArray<FArenaTransitionPlacementTarget>& Targets,
		TArray<FArenaTransitionPlacementPart>& OutParts, FString& OutPendingTarget,
		TArray<TWeakObjectPtr<AActor>>* OutInstances = nullptr);
	bool Matches(const TArray<FArenaTransitionPlacementPart>& Expected,
		const TArray<FArenaTransitionPlacementPart>& Actual, FString& OutPendingTarget);
	bool IsSpawnCapsuleReady(const APawn& Pawn, const FVector& Position);
}
