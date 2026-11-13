#include "Enemy/EnemyTargetRules.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
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

		// 감지 주체의 Perception 비활성화만으로는 다른 AI의 타깃 획득을 막지 못한다.
		// 공통 자격 검사에서 전개 완료된 터렛만 허용해 감지·추적·공유에 같은 기준을 적용한다.
		if (const AAutoTurret* Turret = Cast<AAutoTurret>(TargetActor))
		{
			if (Turret->GetTurretLifecycleState() != EAutoTurretLifecycleState::Active)
			{
				return true;
			}
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
