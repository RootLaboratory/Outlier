#include "Damage/OutlierDamageReceiver.h"

#include "GenericTeamAgentInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "Outlier.h"
#include "Team/OutlierTeamIds.h"

namespace
{
	FGenericTeamId ResolveActorTeam(const AActor* Actor)
	{
		return IsValid(Actor)
			? FGenericTeamId::GetTeamIdentifier(Actor)
			: FGenericTeamId::NoTeam;
	}

	FGenericTeamId ResolveDamageSourceTeam(const FOutlierDamageRequest& Request)
	{
		if (const APawn* InstigatorPawn = Request.EventInstigator
			? Request.EventInstigator->GetPawn()
			: nullptr)
		{
			const FGenericTeamId PawnTeam = ResolveActorTeam(InstigatorPawn);
			if (PawnTeam.GetId() != FGenericTeamId::NoTeam.GetId())
			{
				return PawnTeam;
			}
		}

		const FGenericTeamId ControllerTeam = ResolveActorTeam(Request.EventInstigator);
		if (ControllerTeam.GetId() != FGenericTeamId::NoTeam.GetId())
		{
			return ControllerTeam;
		}

		const FGenericTeamId CauserTeam = ResolveActorTeam(Request.DamageCauser);
		if (CauserTeam.GetId() != FGenericTeamId::NoTeam.GetId())
		{
			return CauserTeam;
		}

		// Weapons and attached damage actors inherit the faction of their direct owner.
		return ResolveActorTeam(IsValid(Request.DamageCauser) ? Request.DamageCauser->GetOwner() : nullptr);
	}

	bool IsEnemyFriendlyFire(const AActor* TargetActor, const FOutlierDamageRequest& Request)
	{
		if (Request.bReflectedDamage)
		{
			return false;
		}

		const uint8 EnemyTeamId = OutlierTeamIds::Enemy;
		return ResolveActorTeam(TargetActor).GetId() == EnemyTeamId
			&& ResolveDamageSourceTeam(Request).GetId() == EnemyTeamId;
	}
}

float OutlierDamage::Apply(AActor* TargetActor, const FOutlierDamageRequest& Request)
{
	if (!IsValid(TargetActor) || Request.DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	// Target selection alone cannot prevent stray projectiles or explosions from
	// reaching another Enemy. Enforce the faction rule at the shared damage gate.
	if (IsEnemyFriendlyFire(TargetActor, Request))
	{
		return 0.0f;
	}

	if (IOutlierDamageReceiver* Receiver = Cast<IOutlierDamageReceiver>(TargetActor))
	{
		return Receiver->ReceiveOutlierDamage(Request);
	}

	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[Damage] Rejected damage for actor without IOutlierDamageReceiver. Target=%s Damage=%.2f Tag=%s"),
		*GetNameSafe(TargetActor),
		Request.DamageAmount,
		*Request.DamageTag.ToString());
	return 0.0f;
}
