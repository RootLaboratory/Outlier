#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/EMPableInterface.h"
#include "Net/UnrealNetwork.h"

UEMPableComponent::UEMPableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UEMPableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		for (TPair<FGameplayTag, FTimerHandle>& TimerPair : EMPDurationTimerHandles)
		{
			TimerManager.ClearTimer(TimerPair.Value);
		}
	}

	EMPDurationTimerHandles.Empty();
	Super::EndPlay(EndPlayReason);
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
	if (Tag == OutlierGameplayTags::State::Stunned())
	{
		ensureAlwaysMsgf(false, TEXT("State.Stunned is ASC-owned and cannot be written to EMPTags."));
		return;
	}

	EMPTags.AddTag(Tag);
}

void UEMPableComponent::ApplyEMPTagForDuration(FGameplayTag Tag, float Duration)
{
	if (Tag == OutlierGameplayTags::State::Stunned())
	{
		ensureAlwaysMsgf(false, TEXT("State.Stunned duration must be applied through a GameplayEffect."));
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	AddEMPTag(Tag);
	ClearEMPDurationTimer(Tag);

	if (Duration <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerDelegate DurationExpiredDelegate;
	DurationExpiredDelegate.BindUObject(this, &UEMPableComponent::HandleEMPTagDurationExpired, Tag);

	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(
		TimerHandle,
		DurationExpiredDelegate,
		Duration,
		false
	);
	EMPDurationTimerHandles.Add(Tag, TimerHandle);
}

void UEMPableComponent::RemoveEMPTag(FGameplayTag Tag)
{
	if (Tag == OutlierGameplayTags::State::Stunned())
	{
		ensureAlwaysMsgf(false, TEXT("State.Stunned removal must be performed by the owning ASC."));
		return;
	}

	ClearEMPDurationTimer(Tag);
	EMPTags.RemoveTag(Tag);
}

void UEMPableComponent::HandleEMPTagDurationExpired(FGameplayTag Tag)
{
	EMPDurationTimerHandles.Remove(Tag);

	if (!EMPTags.HasTagExact(Tag))
	{
		return;
	}

	EMPTags.RemoveTag(Tag);

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority()
		|| !OwnerActor->GetClass()->ImplementsInterface(UEMPableInterface::StaticClass()))
	{
		return;
	}

	if (IEMPableInterface* Handler = Cast<IEMPableInterface>(OwnerActor))
	{
		Handler->HandleEMPEnded(Tag);
	}
}

void UEMPableComponent::ClearEMPDurationTimer(FGameplayTag Tag)
{
	FTimerHandle* TimerHandle = EMPDurationTimerHandles.Find(Tag);
	if (!TimerHandle)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(*TimerHandle);
	}

	EMPDurationTimerHandles.Remove(Tag);
}
