#include "Enemy/EnemyRoomSubsystem.h"

#include "Enemy/EnemyBase.h"
#include "Engine/Level.h"
#include "Network/OutlierArenaPoolSubsystem.h"

void UEnemyRoomSubsystem::NotifyRoomCombat(int32 ArenaId, FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy)
{
	if (ArenaId == INDEX_NONE || !RoomTag.IsValid())
	{
		return;
	}

	UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>();
	if (!ArenaPool)
	{
		return;
	}

	ULevel* ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
	if (!ArenaLevel)
	{
		return;
	}

	for (AActor* Actor : ArenaLevel->Actors)
	{
		AEnemyBase* Enemy = Cast<AEnemyBase>(Actor);
		if (!Enemy || Enemy == ExcludeEnemy || Enemy->GetRoomTag() != RoomTag)
		{
			continue;
		}

		Enemy->EnterCombatInArena(PlayerLocation, ArenaId, false);
	}
}
