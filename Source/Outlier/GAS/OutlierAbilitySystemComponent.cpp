#include "GAS/OutlierAbilitySystemComponent.h"

#include "GameFramework/Pawn.h"
#include "Outlier.h"

UOutlierAbilitySystemComponent::UOutlierAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOutlierAbilitySystemComponent::InitializeForPawn(APawn* Pawn)
{
	if (!ensure(Pawn) || GetOwner() != Pawn)
	{
		return;
	}

	if (GetOwnerActor() != Pawn || GetAvatarActor() != Pawn)
	{
		InitAbilityActorInfo(Pawn, Pawn);
	}
	else
	{
		RefreshAbilityActorInfo();
	}

	UE_LOG(
		LogOutlier,
		Verbose,
		TEXT("[GAS.Init] Actor=%s Owner=%s Avatar=%s Role=%d RepMode=%d"),
		*GetNameSafe(Pawn),
		*GetNameSafe(GetOwnerActor()),
		*GetNameSafe(GetAvatarActor()),
		static_cast<int32>(Pawn->GetLocalRole()),
		static_cast<int32>(ReplicationMode));
}

void UOutlierAbilitySystemComponent::ClearForPawn(const APawn* Pawn)
{
	if (GetOwnerActor() == Pawn || GetAvatarActor() == Pawn)
	{
		ClearActorInfo();
	}
}
