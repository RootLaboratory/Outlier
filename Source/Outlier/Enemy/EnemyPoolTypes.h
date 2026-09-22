#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "EnemyPoolTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyPoolState : uint8
{
	Unmanaged,
	Idle,
	SpawnPresentation,
	CombatActive,
	DeathPresentation
};

USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyPoolLeaseContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Pool")
	FGameplayTag RoomTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Pool")
	int32 CombatPhaseIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Pool")
	int32 WaveIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Pool")
	int32 GameplayGeneration = 0;
};
