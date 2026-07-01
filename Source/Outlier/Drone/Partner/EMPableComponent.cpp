#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "Net/UnrealNetwork.h"

UEMPableComponent::UEMPableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UEMPableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UEMPableComponent, EMPTags);
}

bool UEMPableComponent::IsEMPTargetType() const
{
	return EMPTags.HasTag(EMPGameplayTags::Target::EMPable());
}

bool UEMPableComponent::CanBeEMPTarget(const FGameplayTagContainer& RequiredTags, const FGameplayTagContainer& BlockedTags) const
{
	if (!GetOwner())
	{
		return false;
	}

	if (RequiredTags.Num() > 0 && !EMPTags.HasAll(RequiredTags))
	{
		return false;
	}

	if (BlockedTags.Num() > 0 && EMPTags.HasAny(BlockedTags))
	{
		return false;
	}

	return true;
}

bool UEMPableComponent::HasEMPTag(FGameplayTag Tag) const
{
	return EMPTags.HasTag(Tag);
}

void UEMPableComponent::AddEMPTag(FGameplayTag Tag)
{
	EMPTags.AddTag(Tag);
}

void UEMPableComponent::RemoveEMPTag(FGameplayTag Tag)
{
	EMPTags.RemoveTag(Tag);
}
