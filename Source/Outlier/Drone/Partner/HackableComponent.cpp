#include "Drone/Partner/HackableComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/HackableInterface.h"
#include "Net/UnrealNetwork.h"
#include "Drone/Partner/HackGameplayTags.h"


UHackableComponent::UHackableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UHackableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHackableComponent, HackTags);
}

void UHackableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnHackTargetInvalidated.Broadcast(this, EndPlayReason);
	OnHackTargetInvalidated.Clear();

	Super::EndPlay(EndPlayReason);
}

bool UHackableComponent::CanBeHackTarget(const FHackQueryContext& Context) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed())
	{
		UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] CanBeHackTarget failed: no owner Component=%s"),
			*GetNameSafe(this));
		return false;
	}

	if (Context.RequiredTags.Num() > 0 && !HackTags.HasAll(Context.RequiredTags))
	{
		return false;
	}

	if (Context.BlockedTags.Num() > 0 && HackTags.HasAny(Context.BlockedTags))
	{
		return false;
	}

	if (HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()))
	{
		return Context.HackMultiUseTags.Num() > 0
			&& HackTags.HasAny(Context.HackMultiUseTags);
	}

	return true;
}

bool UHackableComponent::MatchesHackQuery(const FGameplayTagQuery& Query) const
{
	return Query.IsEmpty() || Query.Matches(HackTags);
}

void UHackableComponent::CompleteHack(const FHackResultContext& Context)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	const FGameplayTagContainer EffectTags = ResolveHackEffectTags(Context.Result);

	MulticastTriggerHackEffects(EffectTags, Context);
}

bool UHackableComponent::HasHackTag(FGameplayTag Tag) const
{
	return HackTags.HasTag(Tag);
}

bool UHackableComponent::IsHackTargetType() const
{
	return HackTags.HasTag(HackGameplayTags::Target::Possessable())
		|| HackTags.HasTag(HackGameplayTags::Target::NonPossessable());
}

void UHackableComponent::MarkAsHackedOnce()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	HackTags.AddTag(OutlierGameplayTags::State::HackedOnce());
}

const FGameplayTagContainer& UHackableComponent::ResolveHackEffectTags(EHackResult Result) const
{
	static const FGameplayTagContainer EmptyTags;

	switch (Result)
	{
	case EHackResult::Success:
		return SuccessEffectTags;

	case EHackResult::Fail:
		return FailEffectTags;

	case EHackResult::Cancelled:
	default:
		return EmptyTags;
	}
}

void UHackableComponent::MulticastTriggerHackEffects_Implementation(const FGameplayTagContainer& EffectTags, const FHackResultContext& Context)
{

	AActor* Owner = GetOwner();

	if (!Owner || !Owner->GetClass()->ImplementsInterface(UHackableInterface::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("MulticastTriggerHackEffect Owner Interface Invalid"));
		return;
	}

	IHackableInterface* Handler = Cast<IHackableInterface>(Owner);
	if (!Handler)
	{
		UE_LOG(LogTemp, Error, TEXT("MulticastTriggerHackEffect Owner Handler Invalid"));
		return;
	}

	for (const FGameplayTag& EffectTag : EffectTags)
	{
		Handler->HandleHackEffect(EffectTag, Context);
	}

}
