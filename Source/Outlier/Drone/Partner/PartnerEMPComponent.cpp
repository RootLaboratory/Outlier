#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "AbilitySystemInterface.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Interface/EMPableInterface.h"
#include "UI/EMPLayerWidget.h"
#include "UI/EMPMarkWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Net/UnrealNetwork.h"

UPartnerEMPComponent::UPartnerEMPComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPartnerEMPComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedAbilityData.EMPRange = EMPRange;
	CachedAbilityData.MarkingTime = EMPMarkingTime;
	ResetEMPEarlyCompleteTimer();

}

void UPartnerEMPComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetEMPEarlyCompleteTimer();
	DestroyEMPLayerWidget();
	ClearEMPCandidates();
	Super::EndPlay(EndPlayReason);
}

void UPartnerEMPComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPartnerEMPComponent, bEMPActive);
}

void UPartnerEMPComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEMPCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	RefreshEMPCandidates();
}

void UPartnerEMPComponent::TryEMP_Implementation()
{
	if (!PartnerCharacter || !GetWorld() || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (bEMPActive)
	{
		const float ElapsedTime = GetEMPElapsedTime();
		const float EarlyCompleteDelay = FMath::Max(EMPEarlyCompleteValue, 0.0f);

		if (MarkedActors.Num() > 0 || ElapsedTime >= EarlyCompleteDelay)
		{
			const TArray<AActor*> ConfirmedActors = MarkedActors;
			CompleteEMPOnServer(ConfirmedActors);
		}
		return;
	}

	bEMPActive = true;
	MarkedActors.Empty();
	InitializeEMPEarlyCompleteTimer();
	ClientStartEMPSearch();
	DefaultWidgetControl(true);
}

void UPartnerEMPComponent::CacheAbilityData(const FPartnerEMPAbilityData& InAbilityData)
{
	CachedAbilityData = InAbilityData;

	EMPRange = CachedAbilityData.EMPRange;
	EMPMarkingTime = CachedAbilityData.MarkingTime;
}

void UPartnerEMPComponent::ClientStartEMPSearch_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	bEMPCandidateSearchActive = true;

	EnsureEMPLayerWidget();
	RefreshEMPCandidates(); //Capture

	if (EMPLayerWidget)
	{
		const bool bInitialCaptureEmpty = EMPCandidateActors.IsEmpty(); //Capture 시 없으면, N초 뒤에 Widget 삭제 및, EMP 종료.
		const float ExpirationTime = bInitialCaptureEmpty
			? EMPInitialCaptureEmptyTimeout // 조기 종료 값
			: EMPMarkingTime;

		EMPLayerWidget->InitializeMarkingTimer(ExpirationTime);
	}

}

void UPartnerEMPComponent::ClientStopEMPSearch_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	StopEMPCandidateSearch();
}

void UPartnerEMPComponent::DefaultWidgetControl_Implementation(bool InFlag)
{
	if (AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(PartnerCharacter->GetController()))
	{
		Controller->ControlMainWidget(!InFlag);
	}
}

void UPartnerEMPComponent::RefreshEMPCandidates()
{
	if (!bEMPCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !GetWorld())
	{
		ClearEMPCandidates();
		return;
	}

	DeduplicateEMPCandidates();

	TArray<FOverlapResult> OverlapResults;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerEMPCandidateOverlap), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		PartnerCharacter->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(EMPRange),
		QueryParams
	);

	TArray<TObjectPtr<AActor>> NextCandidateActors;
	TArray<TObjectPtr<UEMPableComponent>> NextCandidateComponents;

	for (const FOverlapResult& Result : OverlapResults)
	{
		AActor* Actor = Result.GetActor();
		UEMPableComponent* EMPableComp = ResolveEMPableComponent(Actor);
		FVector2D ScreenLocation = FVector2D::ZeroVector;

		if (!IsCandidateActorValid(Actor, EMPableComp, ScreenLocation))
		{
			continue;
		}

		if (NextCandidateActors.Contains(Actor) || NextCandidateComponents.Contains(EMPableComp))
		{
			continue;
		}

		NextCandidateActors.Add(Actor);
		NextCandidateComponents.Add(EMPableComp);

		if (!EMPCandidateActors.Contains(Actor) && !EMPCandidateComponents.Contains(EMPableComp))
		{
			AddEMPCandidate(Actor, EMPableComp, ScreenLocation);
		}
	}

	for (int32 Index = EMPCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		const bool bActorStillCandidate = EMPCandidateActors.IsValidIndex(Index) && NextCandidateActors.Contains(EMPCandidateActors[Index]);
		const bool bComponentStillCandidate = NextCandidateComponents.Contains(EMPCandidateComponents[Index]);
		if (!bActorStillCandidate || !bComponentStillCandidate)
		{
			RemoveEMPCandidateAt(Index);
		}
	}

	DeduplicateEMPCandidates();

}

void UPartnerEMPComponent::ClearEMPCandidates()
{
	for (int32 Index = EMPCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		RemoveEMPCandidateAt(Index);
	}
}

void UPartnerEMPComponent::StopEMPCandidateSearch()
{
	bEMPCandidateSearchActive = false;
	ClearEMPCandidates();
	DestroyEMPLayerWidget();
}

void UPartnerEMPComponent::RefocusEMPInput()
{
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		LayerSubsystem->RefocusLayer(
			EMPLayerHandle,
			EUILayerFocusTarget::GameViewport);
	}
}

void UPartnerEMPComponent::TryMarkEMPTarget_Implementation(AActor* TargetActor)
{
	if (!TargetActor || !PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (!bEMPActive)
	{
		return;
	}

	UEMPableComponent* EMPableComponent = ResolveEMPableComponent(TargetActor);
	FVector2D ScreenLocation = FVector2D::ZeroVector;
	if (!IsCandidateActorValid(TargetActor, EMPableComponent, ScreenLocation))
	{

		return;
	}

	if (!MarkedActors.Contains(TargetActor)
		&& CachedAbilityData.MaxTargets > 0
		&& MarkedActors.Num() >= CachedAbilityData.MaxTargets)
	{

		return;
	}

	MarkedActors.AddUnique(TargetActor);
}

bool UPartnerEMPComponent::NotifyEMPConfirmed()
{
	if (!bEMPCandidateSearchActive)
	{
		return false;
	}

	TryEMP();
	return true;
}

void UPartnerEMPComponent::NotifyEMPExpired()
{
	if (!bEMPCandidateSearchActive)
	{
		return;
	}

	StopEMPCandidateSearch();
	ServerExpireEMP();
}

void UPartnerEMPComponent::ServerCompleteEMP_Implementation(const TArray<AActor*>& InMarkedActors)
{
	CompleteEMPOnServer(InMarkedActors);
}

void UPartnerEMPComponent::CompleteEMPOnServer(const TArray<AActor*>& InMarkedActors)
{
	if (!bEMPActive)
	{
		return;
	}

	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		CancelEMPOnServer();
		return;
	}

	if (InMarkedActors.Num() <= 0)
	{

		bEMPActive = false;
		MarkedActors.Empty();
		ResetEMPEarlyCompleteTimer();
		ClientCompleteEMP();
		DefaultWidgetControl(false);
		OnEMPFinished.Broadcast(false, false);
		return;
	}

	int32 AppliedTargetCount = 0;
	for (AActor* MarkedActor : InMarkedActors)
	{
		if (CachedAbilityData.MaxTargets > 0 && AppliedTargetCount >= CachedAbilityData.MaxTargets)
		{
			break;
		}

		if (!IsValid(MarkedActor))
		{
			continue;
		}

		UEMPableComponent* EMPableComponent = ResolveEMPableComponent(MarkedActor);
		FVector2D ScreenLocation = FVector2D::ZeroVector;
		if (!IsCandidateActorValid(MarkedActor, EMPableComponent, ScreenLocation))
		{
			continue;
		}

		IAbilitySystemInterface* AbilitySystemActor = Cast<IAbilitySystemInterface>(MarkedActor);
		UOutlierAbilitySystemComponent* TargetAbilitySystem = AbilitySystemActor
			? Cast<UOutlierAbilitySystemComponent>(AbilitySystemActor->GetAbilitySystemComponent())
			: nullptr;
		if (!TargetAbilitySystem
			|| !TargetAbilitySystem->ApplyStunStateToSelf(
				CachedAbilityData.StunDuration,
				PartnerCharacter).IsValid())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[PartnerEMP] Target has no compatible ASC or rejected Stun GameplayEffect. Target=%s Duration=%.2f"),
				*GetNameSafe(MarkedActor),
				CachedAbilityData.StunDuration);
			continue;
		}

		++AppliedTargetCount;
	}

	bEMPActive = false;
	MarkedActors.Empty();
	ResetEMPEarlyCompleteTimer();
	ClientCompleteEMP();
	DefaultWidgetControl(false);
	OnEMPFinished.Broadcast(AppliedTargetCount > 0, false);
}

void UPartnerEMPComponent::ServerCancelEMP_Implementation()
{
	CancelEMPOnServer();
}

void UPartnerEMPComponent::ServerExpireEMP_Implementation()
{

	if (!MarkedActors.IsEmpty())
	{
		const TArray<AActor*> ExpiredMarkedActors = MarkedActors;
		CompleteEMPOnServer(ExpiredMarkedActors);
		return;
	}

	CancelEMPOnServer();
}

void UPartnerEMPComponent::CancelEMPOnServer()
{
	const bool bWasActive = bEMPActive;

	bEMPActive = false;
	MarkedActors.Empty();
	ResetEMPEarlyCompleteTimer();
	ClientCompleteEMP();
	DefaultWidgetControl(false);
	if (bWasActive)
	{
		OnEMPFinished.Broadcast(false, true);
	}
}

void UPartnerEMPComponent::ClientCompleteEMP_Implementation()
{
	StopEMPCandidateSearch();
}

void UPartnerEMPComponent::CancelForReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	CancelEMPOnServer();
	StopEMPCandidateSearch();
}

UEMPableComponent* UPartnerEMPComponent::ResolveEMPableComponent(AActor* Actor) const
{
	if (!Actor || Actor == PartnerCharacter)
	{
		return nullptr;
	}

	if (!Actor->GetClass()->ImplementsInterface(UEMPableInterface::StaticClass()))
	{
		return nullptr;
	}

	UEMPableComponent* EMPableComp = Cast<IEMPableInterface>(Actor)->GetEMPableComponent();

	return EMPableComp;
}

bool UPartnerEMPComponent::IsCandidateActorValid(AActor* Actor, UEMPableComponent* EMPableComp, FVector2D& OutScreenLocation) const
{
	if (!Actor || !PartnerCharacter || !EMPableComp || !EMPableComp->IsEMPTargetType())
	{
		return false;
	}

	if (FVector::DistSquared(PartnerCharacter->GetActorLocation(), Actor->GetActorLocation()) > FMath::Square(EMPRange))
	{
		return false;
	}

	if (Actor->GetWorld() != GetWorld())
	{

		return false;
	}

	if (!EMPableComp->CanBeEMPTarget(RequiredEMPTags, BlockedEMPTags))
	{
		return false;
	}

	if (bRequireLineOfSight && !HasLineOfSight(Actor))
	{
		return false;
	}

	if (!IsActorInViewport(Actor, OutScreenLocation))
	{
		return false;
	}

	return true;
}

bool UPartnerEMPComponent::IsActorInViewport(AActor* Actor, FVector2D& OutScreenLocation) const
{
	if (!Actor || !PartnerCharacter)
	{
		return false;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		OutScreenLocation = FVector2D::ZeroVector;
		return true;
	}

	if (!PlayerController->ProjectWorldLocationToScreen(Actor->GetActorLocation(), OutScreenLocation, true))
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	return ViewportX <= 0 || ViewportY <= 0
		|| (OutScreenLocation.X >= 0.0f
			&& OutScreenLocation.Y >= 0.0f
			&& OutScreenLocation.X <= ViewportX
			&& OutScreenLocation.Y <= ViewportY);
}

bool UPartnerEMPComponent::HasLineOfSight(AActor* Actor) const
{
	if (!Actor || !PartnerCharacter || !GetWorld())
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerEMPLineOfSight), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	FVector ViewLocation;
	FRotator ViewRotation;
	if (AController* Controller = PartnerCharacter->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		ViewLocation = PartnerCharacter->GetActorLocation();
	}

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLocation,
		Actor->GetActorLocation(),
		ECC_Visibility,
		QueryParams
	);

	return !bHit || Hit.GetActor() == Actor;
}

void UPartnerEMPComponent::InitializeEMPEarlyCompleteTimer()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		ResetEMPEarlyCompleteTimer();
		return;
	}

	EMPStartTimeSeconds = World->GetTimeSeconds();
}

void UPartnerEMPComponent::ResetEMPEarlyCompleteTimer()
{
	EMPStartTimeSeconds = 0.0f;
}

float UPartnerEMPComponent::GetEMPElapsedTime() const
{
	const UWorld* World = GetWorld();
	if (!World || !bEMPActive)
	{
		return 0.0f;
	}

	return FMath::Max(World->GetTimeSeconds() - EMPStartTimeSeconds, 0.0f);
}

void UPartnerEMPComponent::EnsureEMPLayerWidget()
{
	if (IsValid(EMPLayerWidget))
	{
		return;
	}

	if (!PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	TSubclassOf<UEMPLayerWidget> EffectiveClass = EMPLayerWidgetClass;
	if (!EffectiveClass)
	{
		EffectiveClass = UEMPLayerWidget::StaticClass();
	}

	EMPLayerWidget = CreateWidget<UEMPLayerWidget>(PlayerController, EffectiveClass);


	if (!IsValid(EMPLayerWidget))
	{
		return;
	}

	EMPLayerWidget->SetMarkWidgetClass(EMPMarkWidgetClass);
	EMPLayerWidget->BindEMPComponent(this);
	EMPLayerWidget->InitializeMarkingTimer(EMPMarkingTime);

	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		EMPLayerHandle = LayerSubsystem->PushWidget(
			UILayerTags::Gameplay(),
			EMPLayerWidget,
			FirstPersonInputModeTags::EMP(),
			this,
			EUILayerFocusTarget::Widget,
			true);
	}

	if (!EMPLayerHandle.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PartnerEMP] Failed to push EMP widget into the local UI layer"));
		EMPLayerWidget->BindEMPComponent(nullptr);
		EMPLayerWidget = nullptr;
	}
}

void UPartnerEMPComponent::DestroyEMPLayerWidget()
{

	if (!IsValid(EMPLayerWidget))
	{
		if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
		{
			LayerSubsystem->PopLayer(EMPLayerHandle);
		}
		EMPLayerHandle.Reset();
		return;
	}

	EMPLayerWidget->ClearMarkers();
	EMPLayerWidget->SetVisibility(ESlateVisibility::Collapsed);
	EMPLayerWidget->BindEMPComponent(nullptr);

	bool bPoppedLayer = false;
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		bPoppedLayer = LayerSubsystem->PopLayer(EMPLayerHandle);
	}
	if (!bPoppedLayer)
	{
		EMPLayerWidget->RemoveFromParent();
	}
	EMPLayerHandle.Reset();

	EMPLayerWidget = nullptr;

}

ULocalPlayerUILayerSubsystem* UPartnerEMPComponent::GetUILayerSubsystem() const
{
	if (!PartnerCharacter)
	{
		return nullptr;
	}

	APlayerController* PlayerController =
		Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	return LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
}

void UPartnerEMPComponent::AddEMPCandidate(AActor* Actor, UEMPableComponent* EMPableComp, const FVector2D& ScreenLocation)
{
	if (!Actor || !EMPableComp)
	{
		return;
	}

	if (EMPCandidateActors.Contains(Actor) || EMPCandidateComponents.Contains(EMPableComp))
	{
		return;
	}

	EMPCandidateActors.Add(Actor);
	EMPCandidateComponents.Add(EMPableComp);

	if (EMPLayerWidget)
	{
		EMPLayerWidget->AddCandidate(Actor, EMPableComp);
	}
}

void UPartnerEMPComponent::RemoveEMPCandidateAt(int32 Index)
{
	if (!EMPCandidateComponents.IsValidIndex(Index))
	{
		return;
	}

	UEMPableComponent* EMPableComp = EMPCandidateComponents[Index];
	AActor* Actor = EMPCandidateActors.IsValidIndex(Index) ? EMPCandidateActors[Index] : nullptr;

	if (EMPLayerWidget)
	{
		EMPLayerWidget->RemoveCandidate(Actor, EMPableComp);
	}

	EMPCandidateComponents.RemoveAtSwap(Index);
	if (EMPCandidateActors.IsValidIndex(Index))
	{
		EMPCandidateActors.RemoveAtSwap(Index);
	}
}

void UPartnerEMPComponent::DeduplicateEMPCandidates()
{
	TArray<TObjectPtr<AActor>> SeenActors;
	TArray<TObjectPtr<UEMPableComponent>> SeenComponents;

	for (int32 Index = EMPCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = EMPCandidateActors.IsValidIndex(Index) ? EMPCandidateActors[Index] : nullptr;
		UEMPableComponent* EMPableComp = EMPCandidateComponents[Index];

		if (!Actor || !EMPableComp || SeenActors.Contains(Actor) || SeenComponents.Contains(EMPableComp))
		{
			RemoveEMPCandidateAt(Index);
			continue;
		}

		SeenActors.Add(Actor);
		SeenComponents.Add(EMPableComp);
	}
}
