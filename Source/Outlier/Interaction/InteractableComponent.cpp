// Fill out your copyright notice in the Description page of Project Settings.

#include "Interaction/InteractableComponent.h"

#include "UI/TargetPointHudWidget.h"
#include "Components/SceneComponent.h"
#include "Components/PanelWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Drone/Partner/HackableComponent.h"
#include "UI/ActorBasedWidgetSubsystem.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractionDescActor.h"
#include "Interaction/InteractionNode.h"
#include "Interaction/InteractInfoSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "UI/InteractInfoWidget.h"
#include "UI/InteractKeyWidget.h"
#include "UI/InteractionLockedWidget.h"
#include "UI/InteractionOutlineWidget.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDebugInteractionDismiss(
		TEXT("outlier.UI.DebugInteractionDismiss"), 1,
		TEXT("Log interaction widget dismissal and Locked/Outline visibility transitions. 0: off, 1: on."));

	void LogInteractionWidgetState(const UInteractableComponent* Component, const TCHAR* Event,
		bool bShown, const UUserWidget* Widget, const AFirstPersonCharacter* Interactor = nullptr)
	{
		if (!CVarDebugInteractionDismiss.GetValueOnGameThread()) return;
		const AFirstPersonPlayerController* Controller = Interactor
			? Cast<AFirstPersonPlayerController>(Interactor->GetController())
			: (Widget ? Cast<AFirstPersonPlayerController>(Widget->GetOwningPlayer()) : nullptr);
		UE_LOG(LogTemp, Warning,
			TEXT("[InteractionWidgetDebug] %s World=%s NetMode=%d Owner=%s Component=%s Shown=%d Widget=%s Visibility=%d Parent=%s Interactor=%s Controller=%s HudHiddenForDeath=%d"),
			Event, *GetNameSafe(Component->GetWorld()),
			Component->GetWorld() ? static_cast<int32>(Component->GetWorld()->GetNetMode()) : -1,
			*GetNameSafe(Component->GetOwner()), *GetNameSafe(Component), bShown ? 1 : 0,
			*GetNameSafe(Widget), Widget ? static_cast<int32>(Widget->GetVisibility()) : -1,
			*GetNameSafe(Widget ? Widget->GetParent() : nullptr), *GetNameSafe(Interactor),
			*GetNameSafe(Controller), Controller && Controller->IsHudHiddenForDeath() ? 1 : 0);
	}
}

namespace InteractableComponentTags
{
	static FGameplayTag Hold()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Type.Hold")), false);
	}

	static FGameplayTag Concurrent()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Session.Concurrent")), false);
	}

	static FGameplayTag MultipleUse()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Use.Multiple")), false);
	}

	static FGameplayTag Used()
	{
		return OutlierGameplayTags::State::Used();
	}

	static FGameplayTag Locked()
	{
		return OutlierGameplayTags::State::Locked();
	}

	static FGameplayTag Disabled()
	{
		return OutlierGameplayTags::State::Disabled();
	}
}

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	HoldSessions.Reserve(MaxConcurrentHoldSessions);
}

void UInteractableComponent::OnRegister()
{
	// BP 복제 시 생성자의 참조가 템플릿을 가리키지 않도록 실제 소유 노드로 다시 연결한다.
	InteractionNode = Cast<AInteractionNode>(GetOwner());
	Super::OnRegister();
	RefreshInteractionTargetPoint();
}

void UInteractableComponent::InitializeComponent()
{
	Super::InitializeComponent();
	RefreshInteractionTargetPoint();
}

void UInteractableComponent::OnUnregister()
{
	CachedInteractionTargetPoint.Reset();
	Super::OnUnregister();
}

void UInteractableComponent::RefreshInteractionTargetPoint()
{
	CachedInteractionTargetPoint.Reset();
	const AActor* Owner = GetOwner();
	const FComponentReference& Reference = InteractionTargetPointReference;
	// 빈 참조는 엔진에서 Root로 해석되므로 제외한다. 미지정 상태는 Actor 원점을 사용한다.
	const bool bHasComponentSelection = !Reference.ComponentProperty.IsNone()
		|| !Reference.PathToComponent.IsEmpty() || !Reference.OverrideComponent.IsExplicitlyNull();
	if (!IsValid(Owner) || !bHasComponentSelection
		|| (!Reference.OtherActor.IsExplicitlyNull() && Reference.OtherActor.Get() != Owner)
		|| (!Reference.OverrideComponent.IsExplicitlyNull() && !Reference.OverrideComponent.IsValid()))
	{
		return;
	}

	USceneComponent* TargetPoint = Cast<USceneComponent>(Reference.GetComponent(GetOwner()));
	if (IsValid(TargetPoint) && TargetPoint->GetOwner() == Owner && !TargetPoint->IsEditorOnly())
	{
		CachedInteractionTargetPoint = TargetPoint;
	}
}

FVector UInteractableComponent::GetInteractionWidgetLocation() const
{
	const AActor* Owner = GetOwner();
	const USceneComponent* TargetPoint = CachedInteractionTargetPoint.Get();
	if (IsValid(TargetPoint) && TargetPoint->IsRegistered() && TargetPoint->GetOwner() == Owner)
	{
		// UI 위치는 BP에서 배치한 기준점 하나로 결정한다.
		return TargetPoint->GetComponentLocation();
	}
	return (Owner ? Owner->GetActorLocation() : FVector::ZeroVector);
}

void UInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	// BP Construction에서 생성·재부착된 컴포넌트까지 확정된 뒤 참조를 다시 해결한다.
	RefreshInteractionTargetPoint();
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->RegisterWidgetOwner(this);
	}
}

void UInteractableComponent::OnOutlineRangeChanged_Implementation(
	AFirstPersonCharacter* Interactor, bool bInRange)
{
	// Outline and widget presentation can be implemented here or in a Blueprint override.
}

void UInteractableComponent::DismissActorBasedWidgets_Implementation()
{
	LogInteractionWidgetState(this, TEXT("Dismiss.Begin.Locked"), bLockedShown, InteractionLockedWidget);
	LogInteractionWidgetState(this, TEXT("Dismiss.Begin.Outline"), bOutlineShown, InteractionOutlineWidget);
	DeactivateInteractionDesc();
	InteractInfoWidgetDeactivate();
	InteractKeyWidgetDeactivate();
	InteractionLockedWidgetDeactivate();
	InteractionOutlineWidgetDeactivate();
	LogInteractionWidgetState(this, TEXT("Dismiss.End.Locked"), bLockedShown, InteractionLockedWidget);
	LogInteractionWidgetState(this, TEXT("Dismiss.End.Outline"), bOutlineShown, InteractionOutlineWidget);
}

void UInteractableComponent::RefreshWidgetTick()
{
	// 투영 실패로 숨겨져도 표시 요청은 유지해야 카메라를 돌렸을 때 다시 나타난다.
	SetComponentTickEnabled(!HoldSessions.IsEmpty()
		|| (InteractionNode && InteractionNode->NeedsPresentationUpdate())
		|| bInfoShown || bKeyShown || bLockedShown || bOutlineShown);
}

void UInteractableComponent::UpdateWidgetPresentation()
{
	if (HudLayer)
	{
		const FVector Point = GetInteractionWidgetLocation();
		HudLayer->UpdateContent(InteractInfoWidget, bInfoShown, Point, InteractInfoWidgetDrawSize);
		HudLayer->UpdateContent(InteractKeyWidget, bKeyShown, Point, InteractKeyWidgetDrawSize);
		HudLayer->UpdateContent(InteractionLockedWidget, bLockedShown, Point, InteractionLockedWidgetDrawSize);
		HudLayer->UpdateContent(InteractionOutlineWidget, bOutlineShown, Point, InteractionOutlineWidgetDrawSize);
	}
	RefreshWidgetTick();
}

void UInteractableComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickHoldInteractions(DeltaTime);
	if (InteractionNode && InteractionNode->NeedsPresentationUpdate()) InteractionNode->UpdateHoldPresentation();
	UpdateWidgetPresentation();
}

void UInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->UnregisterWidgetOwner(this);
	}
	CancelAllHoldSessions();
	ClearInteractionDescActor();
	InteractionLockedWidgetDeactivate();
	InteractionOutlineWidgetDeactivate();
	if (HudLayer) { HudLayer->Release(); HudLayer = nullptr; }
	Super::EndPlay(EndPlayReason);
}

void UInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInteractableComponent, InteractableTags);
}

bool UInteractableComponent::IsLockedYet() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner)
		&& Owner->FindComponentByClass<UHackableComponent>()
		&& InteractableTags.HasTagExact(InteractableComponentTags::Locked());
}

bool UInteractableComponent::CanInteract(const FGameplayTagContainer& InteractorTags) const
{
	return MeetsInteractionRequirements(InteractorTags)
		&& !InteractableTags.HasTagExact(InteractableComponentTags::Locked());
}

bool UInteractableComponent::CanShowInteractionWidgets(const FGameplayTagContainer& InteractorTags) const
{
	// Hack lock prompts remain visible regardless of the player's interaction permissions.
	if (IsLockedYet())
	{
		return !InteractableTags.HasTagExact(InteractableComponentTags::Used())
			&& !InteractableTags.HasTagExact(InteractableComponentTags::Disabled());
	}
	return MeetsInteractionRequirements(InteractorTags)
		&& (!InteractableTags.HasTagExact(InteractableComponentTags::Locked()) || IsLockedYet());
}

bool UInteractableComponent::CanShowInteractionOutlineWidget() const
{
	return !InteractableTags.HasTagExact(InteractableComponentTags::Used())
		&& !InteractableTags.HasTagExact(InteractableComponentTags::Disabled())
		&& (!InteractableTags.HasTagExact(InteractableComponentTags::Locked()) || IsLockedYet());
}

bool UInteractableComponent::MeetsInteractionRequirements(const FGameplayTagContainer& InteractorTags) const
{
	if (InteractionNode && !InteractorTags.HasTagExact(OutlierGameplayTags::Actor::Role::Partner())) return false;
	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	if (UsedTag.IsValid() && InteractableTags.HasTagExact(UsedTag))
	{
		return false;
	}

	const FGameplayTag DisabledTag = InteractableComponentTags::Disabled();
	if (DisabledTag.IsValid() && InteractableTags.HasTagExact(DisabledTag))
	{
		return false;
	}

	const FGameplayTag ShooterRoleTag = OutlierGameplayTags::Actor::Role::Shooter();
	if (ShooterRoleTag.IsValid()
		&& InteractableTags.HasTagExact(ShooterRoleTag)
		&& !InteractorTags.HasTagExact(ShooterRoleTag))
	{
		return false;
	}

	if (!RequiredInteractorQuery.IsEmpty()
		&& !RequiredInteractorQuery.Matches(InteractorTags))
	{
		return false;
	}

	if (BlockedInteractorTags.Num() > 0
		&& InteractorTags.HasAny(BlockedInteractorTags))
	{
		return false;
	}

	return true;
}

bool UInteractableComponent::RequiresHoldInteract() const
{
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();
	return HoldTag.IsValid() && InteractableTags.HasTagExact(HoldTag);
}

bool UInteractableComponent::AllowsConcurrentHold() const
{
	const FGameplayTag ConcurrentTag = InteractableComponentTags::Concurrent();
	return ConcurrentTag.IsValid() && InteractableTags.HasTagExact(ConcurrentTag);
}

bool UInteractableComponent::IsMultipleUse() const
{
	const FGameplayTag MultipleUseTag = InteractableComponentTags::MultipleUse();
	return MultipleUseTag.IsValid() && InteractableTags.HasTagExact(MultipleUseTag);
}

EInteractionFlowResult UInteractableComponent::AdvanceInteractionFlow(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		return EInteractionFlowResult::Rejected;
	}

	if (RequiresHoldInteract())
	{
		return EInteractionFlowResult::Completed;
	}

	MarkHoldReady();
	return EInteractionFlowResult::HoldReady;
}

bool UInteractableComponent::BeginHoldInteract(AFirstPersonCharacter* Interactor, int32 AttemptId)
{
	if (!Interactor || !RequiresHoldInteract()
		|| (InteractionNode && (!FMath::IsFinite(HoldDuration) || HoldDuration <= 0.0f))
		|| !CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		return false;
	}

	if (FindHoldSessionIndex(Interactor) != INDEX_NONE)
	{
		return true;
	}

	if ((!AllowsConcurrentHold() && !HoldSessions.IsEmpty())
		|| HoldSessions.Num() >= MaxConcurrentHoldSessions)
	{
		return false;
	}

	FHoldInteractionSession& NewSession = HoldSessions.AddDefaulted_GetRef();
	NewSession.Interactor = Interactor;
	if (InteractionNode) InteractionNode->BeginHoldPresentation(Interactor, AttemptId, HoldDuration);

	if (Interactor->IsLocallyControlled())
	{
		if (AInteractionDescActor* DescActor = EnsureInteractionDescActor())
		{
			DescActor->SetActorLocation(GetInteractionWidgetLocation());
			DescActor->ActivateDescFromSource(Interactor, this);
			DescActor->SetProgress(0.0f);
		}
	}

	SetComponentTickEnabled(true);
	return true;
}

void UInteractableComponent::EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled)
{
	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	if (SessionIndex == INDEX_NONE)
	{
		return;
	}

	const bool bWasLocalSession = Interactor && Interactor->IsLocallyControlled();
	if (InteractionNode) InteractionNode->EndHoldPresentation(bCanceled);
	HoldSessions.RemoveAt(SessionIndex);

	if (bCanceled && HoldSessions.IsEmpty())
	{

		if ((GetOwner() && GetOwner()->HasAuthority()) || !AllowsConcurrentHold())
		{
			ClearHoldReady();
		}
	}

	if (bWasLocalSession && IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(0.0f);
		InteractionDescActor->DeactivateDesc();
	}

	if (HoldSessions.IsEmpty())
	{
		RefreshWidgetTick();
	}
}

void UInteractableComponent::ResetHoldInteraction(AFirstPersonCharacter* Interactor)
{
	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	if (SessionIndex != INDEX_NONE)
	{
		EndHoldInteract(Interactor, true);
		return;
	}

	if ((GetOwner() && GetOwner()->HasAuthority()) || !AllowsConcurrentHold())
	{
		ClearHoldReady();
	}

	if (Interactor && Interactor->IsLocallyControlled() && IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(0.0f);
		InteractionDescActor->DeactivateDesc();
	}
}

bool UInteractableComponent::ResetHoldReadyIfIdle()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !HoldSessions.IsEmpty())
	{
		return false;
	}
	ClearHoldReady();
	Owner->ForceNetUpdate();
	return true;
}

bool UInteractableComponent::CanCommitHoldInteraction(const AFirstPersonCharacter* Interactor) const
{
	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	if (SessionIndex == INDEX_NONE)
	{
		return false;
	}

	constexpr float CompletionToleranceSeconds = 0.1f;
	// 수집 함수가 bCollected를 먼저 설정해도 세션 확정에는 원래 시작 시각을 사용한다.
	if (InteractionNode)
	{
		return InteractionNode->HoldState.GetProgress(
			InteractionNode->GetPresentationTime() + CompletionToleranceSeconds) >= 1.0f;
	}
	return HoldSessions[SessionIndex].ElapsedTime + CompletionToleranceSeconds >= HoldDuration;
}

bool UInteractableComponent::CommitHoldInteraction(AFirstPersonCharacter* Interactor)
{
	if (!CanCommitHoldInteraction(Interactor))
	{
		return false;
	}

	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	HoldSessions.RemoveAt(SessionIndex);

	CommitInteraction(Interactor);
	if (HoldSessions.IsEmpty()) RefreshWidgetTick();
	return true;
}

void UInteractableComponent::CommitInteraction(AFirstPersonCharacter* Interactor)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || IsMultipleUse()) return;
	MarkUsed(Interactor);
	OnRep_InteractableTags();
	Owner->ForceNetUpdate();
}

void UInteractableComponent::SyncInteractionStateFromServer(
	EInteractionFlowResult FlowResult)
{
	if (FlowResult == EInteractionFlowResult::HoldReady)
	{
		MarkHoldReady();
		return;
	}

	if (FlowResult == EInteractionFlowResult::Completed
		&& !IsMultipleUse())
	{
		MarkUsed(nullptr);
	}
}

void UInteractableComponent::HandleInteractionSucceeded(
	AFirstPersonCharacter* Interactor,
	EInteractionFlowResult FlowResult,
	bool bSyncStateFromServer)
{
	InteractKeyWidgetDeactivate();

	if (bSyncStateFromServer)
	{
		SyncInteractionStateFromServer(FlowResult);
	}

	if (FlowResult == EInteractionFlowResult::Completed)
	{
		DeactivateInteractionDesc();
	}
	else if (FlowResult == EInteractionFlowResult::HoldReady)
	{
		ActivateInteractionDesc(Interactor);
	}
}

void UInteractableComponent::MarkHoldReady()
{
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();
	if (HoldTag.IsValid())
	{
		InteractableTags.AddTag(HoldTag);
	}
}

void UInteractableComponent::ActivateInteractionDesc(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	if (!CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		DeactivateInteractionDesc();
		return;
	}

	if (AInteractionDescActor* DescActor = EnsureInteractionDescActor())
	{
		DescActor->SetActorLocation(GetInteractionWidgetLocation());
		DescActor->ActivateDescFromSource(Interactor, this);
		DescActor->PopupAnimationCall(false);
		return;
	}

	InteractInfoWidgetActivate(Interactor);
}

void UInteractableComponent::DeactivateInteractionDesc()
{
	ClearInteractionDescActor();
	InteractInfoWidgetDeactivate();
}

void UInteractableComponent::InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	if (!CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractInfoWidgetDeactivate();
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController || !InteractInfoWidgetClass)
	{
		return;
	}

	UUserWidget* Widget = EnsureInteractInfoWidget(PlayerController);
	if (!Widget)
	{
		return;
	}

	FGameplayTag InteractTag;
	FInteractInfoRow InteractInfo;
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UInteractInfoSubsystem* InteractInfoSubsystem =
		GameInstance ? GameInstance->GetSubsystem<UInteractInfoSubsystem>() : nullptr;

	if (!GetPrimaryInteractTag(InteractTag)
		|| !InteractInfoSubsystem
		|| !InteractInfoSubsystem->TryGetInteractInfo(InteractTag, InteractInfo))
	{
		return;
	}

	InteractKeyWidgetDeactivate();
	bInfoShown = true;

	if (!InteractInfoWidget)
	{
		InteractInfoWidget = Cast<UInteractInfoWidget>(Widget);
	}

	if (InteractInfoWidget)
	{
		InteractInfoWidget->UpdateInteractInfo(InteractTag, InteractInfo);
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractInfoWidgetDeactivate()
{
	if (InteractInfoHudWidget)
	{
		bInfoShown = false;
	}

	if (InteractInfoWidget)
	{
		InteractInfoWidget->ClearInteractInfo();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractKeyWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	if (!CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractKeyWidgetDeactivate();
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController || !InteractKeyWidgetClass)
	{
		return;
	}

	UUserWidget* Widget = EnsureInteractKeyWidget(PlayerController);
	if (!Widget)
	{
		return;
	}

	bKeyShown = true;

	if (!InteractKeyWidget)
	{
		InteractKeyWidget = Cast<UInteractKeyWidget>(Widget);
	}

	if (InteractKeyWidget)
	{
		InteractKeyWidget->UpdateInteractKey();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractKeyWidgetDeactivate()
{
	if (InteractKeyHudWidget)
	{
		bKeyShown = false;
	}

	if (InteractKeyWidget)
	{
		InteractKeyWidget->ClearInteractKey();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::UpdateInnerRangeWidgets(AFirstPersonCharacter* Interactor, bool bFocused)
{
	if (!IsValid(Interactor) || !Interactor->IsLocallyControlled())
	{
		return;
	}

	InteractionOutlineWidgetDeactivate();
	if (!CanShowInteractionWidgets(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractKeyWidgetDeactivate();
		InteractionLockedWidgetDeactivate();
		DeactivateInteractionDesc();
		return;
	}
	if (CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractionLockedWidgetDeactivate();
		if (bFocused)
		{
			InteractKeyWidgetDeactivate();
		}
		else
		{
			InteractKeyWidgetActivate(Interactor);
		}
	}
	else if (IsLockedYet())
	{
		InteractKeyWidgetDeactivate();
		InteractionLockedWidgetActivate(Interactor);
	}
	else
	{
		InteractKeyWidgetDeactivate();
		InteractionLockedWidgetDeactivate();
	}
}

void UInteractableComponent::InteractionLockedWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!IsValid(Interactor) || !Interactor->IsLocallyControlled()
		|| !CanShowInteractionWidgets(Interactor->GetOwnedGameplayTagsForQuery()) || !IsLockedYet())
	{
		InteractionLockedWidgetDeactivate();
		return;
	}
	if (!InteractionLockedHudWidget)
	{
		InteractionLockedHudWidget = CreateHudWidget(
			Cast<APlayerController>(Interactor->GetController()), InteractionLockedWidgetClass);
		if (InteractionLockedHudWidget)
		{
			InteractionLockedWidget = Cast<UInteractionLockedWidget>(InteractionLockedHudWidget);
		}
	}
	if (InteractionLockedHudWidget && InteractionLockedWidget)
	{
		const bool bLogTransition = !bLockedShown || !InteractionLockedWidget->IsVisible();
		InteractionLockedWidget->ShowLockedPrompt();
		bLockedShown = true;
		if (bLogTransition)
			LogInteractionWidgetState(this, TEXT("Locked.Activate"), bLockedShown, InteractionLockedWidget, Interactor);
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractionLockedWidgetDeactivate()
{
	const bool bLogTransition = bLockedShown || (InteractionLockedWidget && InteractionLockedWidget->IsVisible());
	if (InteractionLockedHudWidget)
	{
		bLockedShown = false;
	}
	if (InteractionLockedWidget)
	{
		InteractionLockedWidget->HideLockedPrompt();
	}
	UpdateWidgetPresentation();
	if (bLogTransition)
		LogInteractionWidgetState(this, TEXT("Locked.Deactivate"), bLockedShown, InteractionLockedWidget);
}

void UInteractableComponent::InteractionOutlineWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!IsValid(Interactor) || !Interactor->IsLocallyControlled())
	{
		return;
	}
	const bool bCanInteract = CanInteract(Interactor->GetOwnedGameplayTagsForQuery());
	const bool bLocked = !bCanInteract && IsLockedYet();
	if (!CanShowInteractionOutlineWidget())
	{
		InteractionOutlineWidgetDeactivate();
		DeactivateInteractionDesc();
		return;
	}
	InteractKeyWidgetDeactivate();
	InteractionLockedWidgetDeactivate();
	if (!InteractionOutlineHudWidget)
	{
		InteractionOutlineHudWidget = CreateHudWidget(
			Cast<APlayerController>(Interactor->GetController()), InteractionOutlineWidgetClass);
		if (InteractionOutlineHudWidget)
		{
			InteractionOutlineWidget = Cast<UInteractionOutlineWidget>(InteractionOutlineHudWidget);
		}
	}
	if (InteractionOutlineHudWidget && InteractionOutlineWidget)
	{
		const bool bLogTransition = !bOutlineShown || !InteractionOutlineWidget->IsVisible();
		InteractionOutlineWidget->SetLocked(bLocked);
		bOutlineShown = true;
		if (bLogTransition)
			LogInteractionWidgetState(this, TEXT("Outline.Activate"), bOutlineShown, InteractionOutlineWidget, Interactor);
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractionOutlineWidgetDeactivate()
{
	const bool bLogTransition = bOutlineShown || (InteractionOutlineWidget && InteractionOutlineWidget->IsVisible());
	if (InteractionOutlineHudWidget)
	{
		bOutlineShown = false;
	}
	if (InteractionOutlineWidget)
	{
		InteractionOutlineWidget->HideIndicator();
	}
	UpdateWidgetPresentation();
	if (bLogTransition)
		LogInteractionWidgetState(this, TEXT("Outline.Deactivate"), bOutlineShown, InteractionOutlineWidget);
}

void UInteractableComponent::OnRep_InteractableTags()
{
	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	const FGameplayTag LockedTag = InteractableComponentTags::Locked();
	const FGameplayTag DisabledTag = InteractableComponentTags::Disabled();
	const bool bInteractionUnavailable =
		(UsedTag.IsValid() && InteractableTags.HasTagExact(UsedTag))
		|| (LockedTag.IsValid() && InteractableTags.HasTagExact(LockedTag))
		|| (DisabledTag.IsValid() && InteractableTags.HasTagExact(DisabledTag));

	if (bInteractionUnavailable)
	{
		// Used가 수집 상태보다 먼저 복제돼도 성공을 취소로 표시하지 않는다.
		CancelAllHoldSessions(!InteractableTags.HasTagExact(UsedTag));
		InteractKeyWidgetDeactivate();
		DeactivateInteractionDesc();
	}
	if ((UsedTag.IsValid() && InteractableTags.HasTagExact(UsedTag))
		|| (DisabledTag.IsValid() && InteractableTags.HasTagExact(DisabledTag)))
	{
		InteractionLockedWidgetDeactivate();
		InteractionOutlineWidgetDeactivate();
	}
}

int32 UInteractableComponent::FindHoldSessionIndex(const AFirstPersonCharacter* Interactor) const
{
	return HoldSessions.IndexOfByPredicate(
		[Interactor](const FHoldInteractionSession& Session)
		{
			return Session.Interactor.Get() == Interactor;
		});
}

void UInteractableComponent::TickHoldInteractions(float DeltaTime)
{
	for (int32 Index = HoldSessions.Num() - 1; Index >= 0; --Index)
	{
		FHoldInteractionSession& Session = HoldSessions[Index];
		AFirstPersonCharacter* Interactor = Session.Interactor.Get();
		if (InteractionNode && Interactor && GetOwner()->HasAuthority()
			&& (!Interactor->GetController() || !Interactor->CanInteract()
				|| !CanInteract(Interactor->GetOwnedGameplayTagsForQuery())))
		{
			// 연결 종료/사망/조건 무효화 → 서버 세션 종료 → 소유 클라이언트 입력 상태 정리.
			EndHoldInteract(Interactor, true);
			Interactor->NotifyHoldInteractInvalidated(GetOwner());
			continue;
		}
		if (!Interactor)
		{
			if (InteractionNode) InteractionNode->EndHoldPresentation(true);
			HoldSessions.RemoveAtSwap(Index);
			if (InteractionNode && HoldSessions.IsEmpty() && GetOwner()->HasAuthority()) ClearHoldReady();
			continue;
		}

		Session.ElapsedTime += DeltaTime;
		// Node는 UI와 발광에 같은 서버 시각을 사용한다. 다른 상호작용의 누적 시간 정책은 유지한다.
		const float HoldProgress = InteractionNode ? InteractionNode->GetHoldProgress()
			: FMath::Clamp(Session.ElapsedTime / FMath::Max(HoldDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

		if (Interactor->IsLocallyControlled() && IsValid(InteractionDescActor))
		{
			InteractionDescActor->SetProgress(HoldProgress);
		}

		if (HoldProgress >= 1.0f
			&& !Session.bCompletionNotified
			&& Interactor->IsLocallyControlled())
		{
			Session.bCompletionNotified = true;
			Interactor->NotifyHoldInteractCompleted(GetOwner());
		}
	}

	if (HoldSessions.IsEmpty())
	{
		RefreshWidgetTick();
	}
}

void UInteractableComponent::CancelOtherHoldSessions(AFirstPersonCharacter* CompletedInteractor)
{
	if (InteractionNode) InteractionNode->EndHoldPresentation(false);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (const FHoldInteractionSession& Session : HoldSessions)
		{
			if (AFirstPersonCharacter* Interactor = Session.Interactor.Get())
			{
				if (Interactor != CompletedInteractor)
				{
					Interactor->NotifyHoldInteractInvalidated(GetOwner());
				}
			}
		}
	}

	HoldSessions.Reset();
	RefreshWidgetTick();
}

void UInteractableComponent::CancelAllHoldSessions(bool bCanceled)
{
	if (InteractionNode) InteractionNode->EndHoldPresentation(bCanceled);
	HoldSessions.Reset();
	RefreshWidgetTick();

	if (IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(0.0f);
		InteractionDescActor->DeactivateDesc();
	}

	InteractInfoWidgetDeactivate();
}

void UInteractableComponent::MarkUsed(AFirstPersonCharacter* CompletedInteractor)
{
	CancelOtherHoldSessions(CompletedInteractor);

	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();

	if (UsedTag.IsValid())
	{
		InteractableTags.AddTag(UsedTag);
	}
	if (HoldTag.IsValid())
	{
		InteractableTags.RemoveTag(HoldTag);
	}
}

void UInteractableComponent::RestoreUsedState(bool bUsed)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	if (!UsedTag.IsValid())
	{
		return;
	}

	if (bUsed)
	{
		CancelAllHoldSessions();
		InteractableTags.AddTag(UsedTag);
	}
	else
	{
		InteractableTags.RemoveTag(UsedTag);
	}
	Owner->ForceNetUpdate();
}

void UInteractableComponent::ClearHoldReady()
{
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();
	if (HoldTag.IsValid())
	{
		InteractableTags.RemoveTag(HoldTag);
	}
}

AInteractionDescActor* UInteractableComponent::EnsureInteractionDescActor()
{
	if (IsValid(InteractionDescActor))
	{
		return InteractionDescActor;
	}

	AActor* Owner = GetOwner();
	if (!InteractionDescActorClass || !Owner || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	InteractionDescActor = GetWorld()->SpawnActor<AInteractionDescActor>(
		InteractionDescActorClass,
		GetInteractionWidgetLocation(),
		Owner->GetActorRotation(),
		SpawnParams);

	if (InteractionDescActor)
	{
		InteractionDescActor->SetReplicates(false);
		InteractionDescActor->SetReplicateMovement(false);
	}

	return InteractionDescActor;
}

void UInteractableComponent::ClearInteractionDescActor()
{
	if (IsValid(InteractionDescActor))
	{
		InteractionDescActor->Destroy();
	}

	InteractionDescActor = nullptr;
}

UUserWidget* UInteractableComponent::CreateHudWidget(APlayerController* PlayerController, TSubclassOf<UUserWidget> WidgetClass)
{
	if (!PlayerController || !PlayerController->IsLocalController() || !WidgetClass) return nullptr;
	if (!HudLayer) HudLayer = UTargetPointHudWidget::CreateForPlayer(PlayerController, this);
	if (!HudLayer) return nullptr;
	UUserWidget* Widget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
	if (Widget) HudLayer->AddContent(Widget);
	return Widget;
}

UUserWidget* UInteractableComponent::EnsureInteractInfoWidget(APlayerController* PlayerController)
{
	if (!InteractInfoHudWidget)
	{
		InteractInfoHudWidget = CreateHudWidget(PlayerController, InteractInfoWidgetClass);
		InteractInfoWidget = Cast<UInteractInfoWidget>(InteractInfoHudWidget);
	}
	return InteractInfoHudWidget;
}

UUserWidget* UInteractableComponent::EnsureInteractKeyWidget(APlayerController* PlayerController)
{
	if (!InteractKeyHudWidget)
	{
		InteractKeyHudWidget = CreateHudWidget(PlayerController, InteractKeyWidgetClass);
		InteractKeyWidget = Cast<UInteractKeyWidget>(InteractKeyHudWidget);
	}
	return InteractKeyHudWidget;
}

bool UInteractableComponent::GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const
{
	const FGameplayTag TargetRootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Target")), false);
	TArray<FGameplayTag> InteractableTagArray;
	InteractableTags.GetGameplayTagArray(InteractableTagArray);

	FGameplayTag FirstValidTag;
	for (const FGameplayTag& Tag : InteractableTagArray)
	{
		if (!Tag.IsValid())
		{
			continue;
		}

		if (!FirstValidTag.IsValid())
		{
			FirstValidTag = Tag;
		}

		if (TargetRootTag.IsValid() && Tag.MatchesTag(TargetRootTag))
		{
			OutInteractTag = Tag;
			return true;
		}
	}

	if (FirstValidTag.IsValid())
	{
		OutInteractTag = FirstValidTag;
		return true;
	}

	return false;
}
