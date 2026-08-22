#include "Enemy/EnemyTargetRules.h"

#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "GameFramework/Actor.h"

namespace OutlierEnemyTargetRules
{
	bool IsUnavailable(const AActor* TargetActor)
	{
		if (!IsValid(TargetActor))
		{
			return true;
		}

		const IGameplayTagProviderInterface* TagProvider =
			Cast<IGameplayTagProviderInterface>(TargetActor);
		if (!TagProvider)
		{
			return false;
		}

		const FGameplayTagContainer QueryTags = TagProvider->GetOwnedGameplayTagsForQuery();
		return QueryTags.HasTagExact(OutlierGameplayTags::State::Stealthed())
			|| QueryTags.HasTagExact(OutlierGameplayTags::State::Rebooting());
	}
}
