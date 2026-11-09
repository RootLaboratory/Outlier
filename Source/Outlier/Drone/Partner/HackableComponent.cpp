#include "Drone/Partner/HackableComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/HackableInterface.h"
#include "Net/UnrealNetwork.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Components/SceneComponent.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"
#endif


UHackableComponent::UHackableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

void UHackableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHackableComponent, HackTags);
}

bool UHackableComponent::HasHackTargetPointReference() const
{
	return !HackTargetPointReference.ComponentProperty.IsNone()
		|| !HackTargetPointReference.PathToComponent.IsEmpty()
		|| !HackTargetPointReference.OverrideComponent.IsExplicitlyNull()
		|| !HackTargetPointReference.OtherActor.IsExplicitlyNull();
}

USceneComponent* UHackableComponent::ResolveHackTargetPoint() const
{
	const AActor* Owner = GetOwner();
	const FComponentReference& Reference = HackTargetPointReference;
	// 빈 참조는 엔진에서 Root로 해석되므로 직접 제외한다. 다른 Actor만 지정한 참조도 허용하지 않는다.
	const bool bHasComponentSelection = !Reference.ComponentProperty.IsNone()
		|| !Reference.PathToComponent.IsEmpty()
		|| !Reference.OverrideComponent.IsExplicitlyNull();
	if (!IsValid(Owner) || !bHasComponentSelection
		|| (!Reference.OtherActor.IsExplicitlyNull() && Reference.OtherActor.Get() != Owner)
		|| (!Reference.OverrideComponent.IsExplicitlyNull() && !Reference.OverrideComponent.IsValid()))
	{
		return nullptr;
	}

	USceneComponent* TargetPoint = Cast<USceneComponent>(Reference.GetComponent(GetOwner()));
	// 서버에서도 존재하는 같은 소유자의 위치만 판정에 사용한다. 복제 여부 자체는 요구하지 않는다.
	return IsValid(TargetPoint) && TargetPoint->GetOwner() == Owner
		&& !TargetPoint->IsEditorOnly() && TargetPoint->NeedsLoadForServer()
		? TargetPoint : nullptr;
}

void UHackableComponent::RefreshHackTargetPoint()
{
	CachedHackTargetPoint = ResolveHackTargetPoint();
}

bool UHackableComponent::TryGetHackTargetLocation(FVector& OutLocation) const
{
	const USceneComponent* TargetPoint = CachedHackTargetPoint.Get();
	if (!IsValid(TargetPoint) || !TargetPoint->IsRegistered() || TargetPoint->GetOwner() != GetOwner())
	{
		return false;
	}

	// Transform은 고정 저장하지 않는다. 부모 이동·회전·스케일과 목표점 배치를 그대로 따른다.
	OutLocation = TargetPoint->GetComponentLocation();
	return true;
}

void UHackableComponent::OnRegister()
{
	Super::OnRegister();
	RefreshHackTargetPoint();
}

void UHackableComponent::InitializeComponent()
{
	Super::InitializeComponent();
	// OnRegister 당시 아직 생성되지 않았던 BP 컴포넌트까지 최종 생성 뒤에 해결한다.
	RefreshHackTargetPoint();
}

void UHackableComponent::OnUnregister()
{
	CachedHackTargetPoint.Reset();
	Super::OnUnregister();
}

void UHackableComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshHackTargetPoint();

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
	CachedHackTargetPoint.Reset();

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UHackableComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// Super에서 BP 컴포넌트를 재구성할 수 있으므로 기존 인스턴스의 캐시 갱신을 먼저 끝낸다.
	if (PropertyChangedEvent.MemberProperty
		&& PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UHackableComponent, HackTargetPointReference))
	{
		RefreshHackTargetPoint();
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

EDataValidationResult UHackableComponent::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	if (!HasHackTargetPointReference())
	{
		if (IsHackTargetType())
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("%s: HackTargetPointReference 미지정. 기존 목표점 계산을 사용합니다."), *GetPathName())));
		}
		return Parent;
	}

	// BP 템플릿에는 SCS의 실제 인스턴스가 없다. 참조 해석 검증은 생성된 Actor에서 수행한다.
	if (IsTemplate() || (GetOwner() && GetOwner()->IsTemplate()))
	{
		return Parent;
	}
	if (!ResolveHackTargetPoint())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("%s: HackTargetPointReference는 같은 Actor의 서버에 존재하는 SceneComponent여야 합니다. 누락·파괴·EditorOnly 참조를 확인하세요."),
			*GetPathName())));
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Parent;
}
#endif

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

bool UHackableComponent::RequiresMiniGame() const
{
	return HackTags.HasTag(HackGameplayTags::MiniGame::Root())
		&& !HackTags.HasTag(HackGameplayTags::MiniGame::None());
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
