#include "Enemy/EnemyTargetRules.h"

#include "Enemy/EnemyBase.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Team/OutlierTeamIds.h"

#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "GameFramework/Actor.h"

namespace OutlierEnemyTargetRules
{
	ETargetPriority GetPriority(const AActor* TargetActor)
	{
		if (!IsValid(TargetActor)) return ETargetPriority::Invalid;
		if (const AEnemyBase* Enemy = Cast<AEnemyBase>(TargetActor))
		{
			if (Enemy->GetGenericTeamId() == FGenericTeamId(OutlierTeamIds::Player))
			{
				return Enemy->GetRuntimeStat().Type == EEnemyType::Turret
					? ETargetPriority::HackedTurret : ETargetPriority::PossessedDrone;
			}
			return ETargetPriority::EnemyUnit;
		}
		if (TargetActor->IsA<AShooterCharacter>()) return ETargetPriority::Shooter;
		if (TargetActor->IsA<APartnerCharacter>()) return ETargetPriority::Partner;
		return ETargetPriority::Invalid;
	}

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
