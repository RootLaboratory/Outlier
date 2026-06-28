#include "Drone/Partner/HackableComponent.h"
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

bool UHackableComponent::CanBeHackTarget(const FHackQueryContext& Context) const
{
	if (!GetOwner())
	{
		UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] CanBeHackTarget failed: no owner Component=%s"),
			*GetNameSafe(this));
		return false;
	}

	if (Context.RequiredTags.Num() > 0 && !HackTags.HasAll(Context.RequiredTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] CanBeHackTarget failed: missing required tags Owner=%s Required=%s Tags=%s"),
			*GetNameSafe(GetOwner()),
			*Context.RequiredTags.ToStringSimple(),
			*HackTags.ToStringSimple());
		return false;
	}

	if (Context.BlockedTags.Num() > 0 && HackTags.HasAny(Context.BlockedTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] CanBeHackTarget failed: blocked tag Owner=%s Blocked=%s Tags=%s"),
			*GetNameSafe(GetOwner()),
			*Context.BlockedTags.ToStringSimple(),
			*HackTags.ToStringSimple());
		return false;
	}


	return true;
}

bool UHackableComponent::MatchesHackQuery(const FGameplayTagQuery& Query) const
{
	return Query.IsEmpty() || Query.Matches(HackTags);
}

void UHackableComponent::BeginHack(const FHackQueryContext& Context)
{
	OnHackStarted.Broadcast(Context);

	UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] BeginHack Owner=%s Instigator=%s Tags=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Context.InstigatorActor.Get()),
		*HackTags.ToStringSimple());
}

void UHackableComponent::CompleteHack(const FHackResultContext& Context)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	const FGameplayTagContainer EffectTags = ResolveHackEffectTags(Context.Result);

	MulticastTriggerHackEffects(EffectTags, Context);

	OnHackCompleted.Broadcast(Context);

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

	UE_LOG(LogTemp, Error, TEXT("MuliticastTriggerHackEffect Done"));

}
