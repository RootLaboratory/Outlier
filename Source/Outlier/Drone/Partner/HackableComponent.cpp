#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"


UHackableComponent::UHackableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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
	FHackProcessContext ProcessContext;
	ProcessContext.InstigatorActor = Context.InstigatorActor;
	ProcessContext.TargetActor = GetOwner();
	ProcessContext.HackProcess = EHackProcess::Start;

	SetHackProcess(ProcessContext);
	OnHackStarted.Broadcast(Context);

	UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] BeginHack Owner=%s Instigator=%s Tags=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Context.InstigatorActor.Get()),
		*HackTags.ToStringSimple());
}

void UHackableComponent::CompleteHack(const FHackResultContext& Context)
{
	OnHackCompleted.Broadcast(Context);

	UE_LOG(LogTemp, Warning, TEXT("[HackableDebug] CompleteHack Owner=%s Instigator=%s Result=%d ResultTags=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Context.InstigatorActor.Get()),
		static_cast<int32>(Context.Result),
		*Context.ResultTags.ToStringSimple());

	if (Context.Result == EHackResult::Success)
	{
		HackTags.AddTag(HackGameplayTags::State::HackedOnce());

		for (const FGameplayTag& Tag : HackTags)
		{
			if (Tag.MatchesTag(HackGameplayTags::Effect::Root()))
			{
				OnHackEffectTriggered.Broadcast(Tag, Context);
			}
		}
	}

	FHackProcessContext ProcessContext;
	ProcessContext.InstigatorActor = Context.InstigatorActor;
	ProcessContext.TargetActor = Context.TargetActor.Get() ? Context.TargetActor.Get() : GetOwner();
	ProcessContext.HackProcess = EHackProcess::Done;
	SetHackProcess(ProcessContext);
}

void UHackableComponent::SetHackProcess(const FHackProcessContext& Context)
{
	bProjectWorldLocationToScreen = Context.HackProcess == EHackProcess::Try;
	LastProjectedScreenLocation = Context.ScreenLocation;
	OnHackProcessChanged.Broadcast(Context);
}

void UHackableComponent::SetProjectedScreenLocation(const FVector2D& ScreenLocation)
{
	LastProjectedScreenLocation = ScreenLocation;
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
