#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Interface/EMPableInterface.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
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
			//UE_LOG(LogTemp, Error, TEXT("Confirmed Called"));
		}
		return;
	}

	bEMPActive = true;
	MarkedActors.Empty();
	InitializeEMPEarlyCompleteTimer();
	ClientStartEMPSearch();
	DefaultWidgetControl(true);

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] TryEMP started Partner=%s Range=%.1f"),
			*GetNameSafe(PartnerCharacter), EMPRange);
	}
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

	if (bDebugEMP && LastDebugCandidateCount != EMPCandidateComponents.Num())
	{
		LastDebugCandidateCount = EMPCandidateComponents.Num();
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] Candidate count changed Count=%d Overlaps=%d"),
			EMPCandidateComponents.Num(),
			OverlapResults.Num());
	}
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
	LastDebugCandidateCount = INDEX_NONE;

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] Candidate search stopped"));
	}
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
		if (bDebugEMP)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] TryMarkEMPTarget ignored invalid target Actor=%s"),
				*GetNameSafe(TargetActor));
		}

		return;
	}

	if (!MarkedActors.Contains(TargetActor)
		&& CachedAbilityData.MaxTargets > 0
		&& MarkedActors.Num() >= CachedAbilityData.MaxTargets)
	{
		if (bDebugEMP)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] TryMarkEMPTarget ignored: max targets reached MaxTargets=%d"),
				CachedAbilityData.MaxTargets);
		}

		return;
	}

	MarkedActors.AddUnique(TargetActor);

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] TryMarkEMPTarget Actor=%s TotalMarked=%d"),
			*GetNameSafe(TargetActor), MarkedActors.Num());
	}
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

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] NotifyEMPExpired"));
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
		if (bDebugEMP)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] ServerCompleteEMP ignored: no marked actors"));
		}

		bEMPActive = false;
		MarkedActors.Empty();
		ResetEMPEarlyCompleteTimer();
		ClientCompleteEMP();
		DefaultWidgetControl(false);
		OnEMPFinished.Broadcast(false, false);
		return;
	}
	
	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] ServerCompleteEMP MarkedCount=%d"), InMarkedActors.Num());
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
			if (bDebugEMP)
			{
				UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] ServerCompleteEMP skipped: invalid target Actor=%s"),
					*GetNameSafe(MarkedActor));
			}
			continue;
		}

		//Component에서는 Tag 주입과 Duration에 대한 EMP 만료 처리, Tag에 대한 상태 처리는 인터페이스 함수에서 구체화
		EMPableComponent->ApplyEMPTagForDuration(
			OutlierGameplayTags::State::Stunned(),
			CachedAbilityData.StunDuration
		);

		if (IEMPableInterface* Handler = Cast<IEMPableInterface>(MarkedActor))
		{
			Handler->HandleEMPStarted(OutlierGameplayTags::State::Stunned());
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
	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] ServerExpireEMP"));
	}

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
	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] ServerCancelEMP"));
	}

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

	if (bDebugEMP && !EMPableComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] Interface actor returned null component Actor=%s"),
			*GetNameSafe(Actor));
	}

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
		if (bDebugEMP)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] Candidate ignored: different world Actor=%s ActorWorld=%s ComponentWorld=%s"),
				*GetPathNameSafe(Actor),
				*GetNameSafe(Actor->GetWorld()),
				*GetNameSafe(GetWorld()));
		}

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
		//UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] EnsureEMPLayerWidget: already exists Widget=%s"), *GetNameSafe(EMPLayerWidget));
		return;
	}

	if (!PartnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] EnsureEMPLayerWidget: no PartnerCharacter"));
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] EnsureEMPLayerWidget: no local PlayerController PC=%s"), *GetNameSafe(PlayerController));
		return;
	}

	TSubclassOf<UEMPLayerWidget> EffectiveClass = EMPLayerWidgetClass;
	if (!EffectiveClass)
	{
		EffectiveClass = UEMPLayerWidget::StaticClass();
	}

	EMPLayerWidget = CreateWidget<UEMPLayerWidget>(PlayerController, EffectiveClass);

	//UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] EnsureEMPLayerWidget: CreateWidget result=%s Class=%s"),
	//	*GetNameSafe(EMPLayerWidget), *GetNameSafe(EffectiveClass));

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
	//UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] DestroyEMPLayerWidget: ptr=%s IsValid=%d"),
	//	*GetNameSafe(EMPLayerWidget), IsValid(EMPLayerWidget) ? 1 : 0);

	if (!IsValid(EMPLayerWidget))
	{
		//DestroyRemainingEMPWidgets(PlayerController);
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

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] DestroyEMPLayerWidget: after RemoveFromParent IsInViewport=%d"),
			EMPLayerWidget->IsInViewport() ? 1 : 0);
	}

	EMPLayerWidget = nullptr;

	//DestroyRemainingEMPWidgets(PlayerController);
}

void UPartnerEMPComponent::DestroyRemainingEMPWidgets(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}

	TArray<UUserWidget*> RemainingLayers;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PlayerController, RemainingLayers, UEMPLayerWidget::StaticClass(), true);
	int32 RemovedLayerCount = 0;
	for (UUserWidget* Widget : RemainingLayers)
	{
		if (UEMPLayerWidget* LayerWidget = Cast<UEMPLayerWidget>(Widget))
		{
			if (!LayerWidget->IsInViewport() && !LayerWidget->GetParent())
			{
				UE_LOG(LogTemp, Error, TEXT("NON SIKE!"));
				continue;
			}

			LayerWidget->ClearMarkers();
			LayerWidget->SetVisibility(ESlateVisibility::Collapsed);
			LayerWidget->RemoveFromParent();
			++RemovedLayerCount;
		}
	}

	TArray<UUserWidget*> RemainingMarks;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PlayerController, RemainingMarks, UEMPMarkWidget::StaticClass(), false);
	int32 RemovedMarkCount = 0;
	for (UUserWidget* Widget : RemainingMarks)
	{
		if (Widget)
		{
			if (!Widget->IsInViewport() && !Widget->GetParent())
			{
				continue;
			}

			Widget->SetVisibility(ESlateVisibility::Collapsed);
			Widget->RemoveFromParent();
			++RemovedMarkCount;
		}
	}

	if (bDebugEMP && (RemovedLayerCount > 0 || RemovedMarkCount > 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] DestroyEMPLayerWidget: removed attached remaining Layers=%d Marks=%d"),
			RemovedLayerCount,
			RemovedMarkCount);
	}
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

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] Candidate added Comp=%s NetMode=%d Local=%d Actor=%s ActorPtr=%p ActorWorld=%s ActorPath=%s EMPable=%s EMPablePtr=%p CountBefore=%d Screen=(%.1f, %.1f) Tags=%s"),
			*GetNameSafe(this),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
			PartnerCharacter && PartnerCharacter->IsLocallyControlled() ? 1 : 0,
			*GetNameSafe(Actor),
			Actor,
			*GetNameSafe(Actor->GetWorld()),
			*GetPathNameSafe(Actor),
			*GetNameSafe(EMPableComp),
			EMPableComp,
			EMPCandidateComponents.Num(),
			ScreenLocation.X,
			ScreenLocation.Y,
			*EMPableComp->EMPTags.ToStringSimple());
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

	if (bDebugEMP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerEMPDebug] Candidate removed Comp=%s NetMode=%d Local=%d Actor=%s"),
			*GetNameSafe(this),
			GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
			PartnerCharacter && PartnerCharacter->IsLocallyControlled() ? 1 : 0,
			*GetNameSafe(Actor));
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
