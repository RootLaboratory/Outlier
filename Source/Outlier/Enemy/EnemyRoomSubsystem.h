#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyRoomSubsystem.generated.h"

class AEnemyBase;

UCLASS()
class OUTLIER_API UEnemyRoomSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void NotifyRoomCombat(int32 ArenaId, FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy);
};
