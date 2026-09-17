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

	// [WP 리로드 검증용 계측 — 2026-09-14 비활성화]
	// 리로드가 액터를 실제로 재생성하는지(Ptr 비교) / 런타임 태그가 리로드를 넘어 살아남는지
	// (State.HackedOnce)를 보려고 넣었던 일회용 로그. 대상 액터 이름이 하드코딩돼 있다.
	// 같은 종류를 다시 의심하게 되면 이 블록을 되살릴 것 — 판정법은 [[WorldPartition/2026-09-14]] 참고.
	//const AActor* Owner = GetOwner();
	//if (Owner && Owner->GetName().Contains(TEXT("InteractionStatMachine")))
	//{
	//	UE_LOG(LogTemp, Warning,
	//		TEXT("[HackableLifecycle] BeginPlay Owner=%s Ptr=%p Level=%s NetMode=%d Tags=%s"),
	//		*Owner->GetPathName(),
	//		Owner,
	//		*GetNameSafe(Owner->GetLevel()),
	//		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
	//		*HackTags.ToStringSimple());
	//}
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
	// [WP 리로드 검증용 계측 — 2026-09-14 비활성화] BeginPlay 쪽과 짝. 둘 다 살려야 Ptr 비교가 된다.
	//const AActor* Owner = GetOwner();
	//if (Owner && Owner->GetName().Contains(TEXT("InteractionStatMachine")))
	//{
	//	UE_LOG(LogTemp, Warning,
	//		TEXT("[HackableLifecycle] EndPlay Owner=%s Ptr=%p Level=%s NetMode=%d Reason=%d Tags=%s"),
	//		*Owner->GetPathName(),
	//		Owner,
	//		*GetNameSafe(Owner->GetLevel()),
	//		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
	//		static_cast<int32>(EndPlayReason),
	//		*HackTags.ToStringSimple());
	//}

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
		// [WP 리로드 검증용 계측 — 2026-09-14 비활성화]
		// 리로드 후에도 HackedOnce가 남아 재해킹이 막히는지 보려고 넣었던 1회성 로그.
		// bLoggedHackedOnceBlock 멤버는 이 로그 전용이라 같이 비활성화한다.
		//if (!bLoggedHackedOnceBlock)
		//{
		//	bLoggedHackedOnceBlock = true;
		//	UE_LOG(LogTemp, Warning,
		//		TEXT("[HackableLifecycle] HackedOnce candidate check Owner=%s Ptr=%p NetMode=%d MultiUse=%d Tags=%s"),
		//		*GetPathNameSafe(Owner),
		//		Owner,
		//		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		//		Context.HackMultiUseTags.Num() > 0 && HackTags.HasAny(Context.HackMultiUseTags),
		//		*HackTags.ToStringSimple());
		//}
		return Context.HackMultiUseTags.Num() > 0
			&& HackTags.HasAny(Context.HackMultiUseTags);
	}
	//bLoggedHackedOnceBlock = false;

	return true;
}

void UHackableComponent::OnRep_HackTags()
{
	OnCheckpointHackStateRestored.Broadcast(
		HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()));
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
