#include "Drone/Partner/HackableComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/HackableInterface.h"
#include "Net/UnrealNetwork.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Save/OutlierSaveSubSystem.h"


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

void UHackableComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && !CheckpointProgressId.IsNone())
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetWorld() && GetWorld()->GetGameInstance()
			? GetWorld()->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
				EOutlierWorldProgressType::HackedObject,
				CheckpointProgressId,
				this);
			if (bProgressIdRegistered
				&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::HackedObject, CheckpointProgressId))
			{
				HackTags.AddTag(OutlierGameplayTags::State::HackedOnce());
				OnCheckpointHackStateRestored.Broadcast(true);
				Owner->ForceNetUpdate();
			}
		}
	}
}

void UHackableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetWorld() && GetWorld()->GetGameInstance()
			? GetWorld()->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::HackedObject,
				CheckpointProgressId,
				this);
		}
	}

	OnHackTargetInvalidated.Broadcast(this, EndPlayReason);
	OnHackTargetInvalidated.Clear();
	OnHackTargetUnavailable.Clear();

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
	//bLoggedHackedOnceBlock = false;

	return true;
}

void UHackableComponent::OnRep_HackTags()
{
	if (HackTags.HasTag(OutlierGameplayTags::State::Dead())
		|| HackTags.HasTag(OutlierGameplayTags::State::Locked()))
	{
		NotifyHackTargetUnavailable();
	}

	// 완료 상태를 표현에 재적용하는 통보다. 새 해킹 성공 처리가 아니므로 보상/전투 시작을 재실행하지 않는다.
	OnCheckpointHackStateRestored.Broadcast(
		HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()));
}

void UHackableComponent::NotifyHackTargetUnavailable()
{
	OnHackTargetUnavailable.Broadcast(this);
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
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetWorld() && GetWorld()->GetGameInstance()
			? GetWorld()->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::HackedObject,
				CheckpointProgressId,
				true);
		}
	}
	GetOwner()->ForceNetUpdate();
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
