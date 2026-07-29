// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/OverlapResult.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Interface/HackableInterface.h"
#include "UI/HackCandidateLayerWidget.h"
#include "UI/HackCandidateMarkerWidget.h"
#include "UI/HackMiniGameWidget.h"

UPartnerHackComponent::UPartnerHackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPartnerHackComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedAbilityData.bRequireLineOfSight = bRequireLineOfSight;

	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Dead());
	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Locked());
	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Immune());
}

void UPartnerHackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearActiveHackableComponent();
	DestroyHackMiniGameWidget();
	DestroyCandidateLayerWidget();
	ClearHackCandidates();
	Super::EndPlay(EndPlayReason);
}

void UPartnerHackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHackCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	RefreshHackCandidates();
}

void UPartnerHackComponent::TryHack_Implementation()
{
	if (!PartnerCharacter || !GetWorld())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHack blocked Partner=%s World=%d"),
				*GetNameSafe(PartnerCharacter),
				GetWorld() ? 1 : 0);
		}
		return ;
	}


	if (APartnerPlayerController* PController =
		Cast<APartnerPlayerController>(PartnerCharacter->GetController()))
	{
		const EPartnerPossessionState PossessionState =
			PController->GetPartnerPossessionState();

		if (PossessionState == EPartnerPossessionState::Transitioning)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Transitioning, %d"),
				static_cast<int32>(PossessionState)
			);
			return;
		}
	}

	//MiniGame;
	if (ActiveHackableComponent || HackMiniGameWidget)
	{
		ClientStopHackMiniGame();
		CancelActiveHack();
		return ;
	}

	if (bHackCandidateSearchActive) 
	{
		bHackCandidateSearchActive = false;
		ClientStopCandidateSearch();
		DefaultWidgetControl(bHackCandidateSearchActive);
	}
	else
	{
		bHackCandidateSearchActive = true;
		ClientStartCandidateSearch();
		DefaultWidgetControl(bHackCandidateSearchActive);
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHack started Partner=%s Range=%.1f HalfAngle=%.1f LOS=%d Candidates=%d"),
			*GetNameSafe(PartnerCharacter),
			CachedAbilityData.CandidateRange,
			CandidateHalfAngleDegrees,
			bRequireLineOfSight ? 1 : 0,
			HackCandidateComponents.Num());
	}

	return;
}

void UPartnerHackComponent::EndHackHold()
{
	ResetLocalHackHoldProgress();
}

void UPartnerHackComponent::CacheAbilityData(const FPartnerHackAbilityData& InAbilityData)
{
	CachedAbilityData = InAbilityData;

	bRequireLineOfSight = CachedAbilityData.bRequireLineOfSight;
}

void UPartnerHackComponent::ClientStartCandidateSearch_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	//UE_LOG(LogTemp, Error, TEXT("ClientStartCandidateSearch_Implementation"));

	bHackCandidateSearchActive = true;// 

	EnsureCandidateLayerWidget();
	RefreshHackCandidates();
}

void UPartnerHackComponent::ClientStopCandidateSearch_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	StopHackCandidateSearch();
}

void UPartnerHackComponent::ClientStartHackMiniGame_Implementation(AActor* TargetActor, UHackableComponent* HackableComponent)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	SetActiveHackableComponent(HackableComponent);
	StartHackMiniGame(TargetActor, HackableComponent);
}

void UPartnerHackComponent::ClientStopHackMiniGame_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ClearActiveHackableComponent();
	DestroyHackMiniGameWidget();
}

void UPartnerHackComponent::ClientAbortHackForInvalidTarget_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	AbortLocalHackForInvalidTarget();
}

void UPartnerHackComponent::ServerCompleteHack_Implementation(const FHackResultContext& ResultContext)
{
	if (!IsValid(ActiveHackableComponent)
		|| !IsValid(ActiveHackableComponent->GetOwner())
		|| ActiveHackableComponent->GetOwner()->IsActorBeingDestroyed())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PartnerHackDebug] Hack failed: active target is no longer valid"));
		}

		ClearActiveHackableComponent();
		ClientAbortHackForInvalidTarget();
		DefaultWidgetControl(false);
		return;
	}

	if (ResultContext.TargetActor != ActiveHackableComponent->GetOwner())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PartnerHackDebug] Hack failed: stale result Target=%s ActiveTarget=%s"),
				*GetNameSafe(ResultContext.TargetActor),
				*GetNameSafe(ActiveHackableComponent->GetOwner()));
		}
		return;
	}

	UHackableComponent* CompletedHackableComponent = ActiveHackableComponent;
	AActor* CompletedTargetActor = CompletedHackableComponent->GetOwner();
	ClearActiveHackableComponent();

	//possess 전, Input widget 정리. -> Enemy Widget이 생긴다면 수정 해야 할 부분.
	ClientStopHackMiniGame();
	DefaultWidgetControl(false);

	FHackResultContext MutableResultContext = ResultContext;
	MutableResultContext.TargetActor = CompletedTargetActor;
	MutableResultContext.InstigatorActor = PartnerCharacter;

	CompletedHackableComponent->CompleteHack(MutableResultContext);
}

void UPartnerHackComponent::DefaultWidgetControl_Implementation(bool InFlag)
{
		//UE_LOG(LogTemp, Error, TEXT("DefaultWidgetControl"));

	if (AFirstPersonPlayerController* Controller = Cast< AFirstPersonPlayerController>(PartnerCharacter->GetController()))
	{
		//UE_LOG(LogTemp, Error, TEXT("DefaultWidgetControl Controller Valid"));

		if (InFlag) //Hacking
		{
			Controller->ControlMainWidget(false);
		}
		else
		{
			Controller->ControlMainWidget(true);
		}
	}
}

void UPartnerHackComponent::RefreshHackCandidates()
{
	if (!bHackCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !GetWorld())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Refresh blocked Partner=%s World=%d"),
				*GetNameSafe(PartnerCharacter),
				GetWorld() ? 1 : 0);
		}
		ClearHackCandidates();
		return;
	}

	const FHackQueryContext Context = BuildQueryContext();

	TArray<FOverlapResult> OverlapResults;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerHackCandidateOverlap), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		PartnerCharacter->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(CachedAbilityData.CandidateRange),
		QueryParams
	);

	TArray<TObjectPtr<UHackableComponent>> NextCandidateComponents;

	for (const FOverlapResult& Result : OverlapResults)
	{
		AActor* Actor = Result.GetActor();
		UHackableComponent* HackableComponent = ResolveHackableComponent(Actor);
		FVector2D ScreenLocation = FVector2D::ZeroVector;

		if (!IsCandidateActorValid(Actor, HackableComponent, Context, ScreenLocation))
		{
			continue;
		}

		NextCandidateComponents.AddUnique(HackableComponent);

		if (!HackCandidateComponents.Contains(HackableComponent))
		{
			AddHackCandidate(Actor, HackableComponent, ScreenLocation);
		}
	}

	for (int32 Index = HackCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		if (!NextCandidateComponents.Contains(HackCandidateComponents[Index]))
		{
			RemoveHackCandidateAt(Index);
		}
	}

	if (bDebugHack && LastDebugCandidateCount != HackCandidateComponents.Num())
	{
		LastDebugCandidateCount = HackCandidateComponents.Num();
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate count changed Count=%d Overlaps=%d"),
			HackCandidateComponents.Num(),
			OverlapResults.Num());
	}
}

bool UPartnerHackComponent::TryBeginHackHold()
{
	if (!PartnerCharacter
		|| !PartnerCharacter->IsLocallyControlled()
		|| !bHackCandidateSearchActive
		|| !HoveredMarkerWidget
		|| !HoveredHackActor)
	{
		return false;
	}

	HoveredMarkerWidget->StartHackHold(CachedAbilityData.MiniGameTime); //Property로
	return true;
}

void UPartnerHackComponent::NotifyHackMarkerHovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor)
{
	if (!MarkerWidget || !TargetActor)
	{
		return;
	}

	if (HoveredMarkerWidget && HoveredMarkerWidget != MarkerWidget)
	{
		HoveredMarkerWidget->CancelHackHold();
	}

	HoveredMarkerWidget = MarkerWidget;
	HoveredHackActor = TargetActor;
}

void UPartnerHackComponent::NotifyHackMarkerUnhovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor)
{
	if (HoveredMarkerWidget != MarkerWidget || HoveredHackActor != TargetActor)
	{
		return;
	}

	ResetLocalHackHoldProgress();
	HoveredMarkerWidget = nullptr;
	HoveredHackActor = nullptr;
}

void UPartnerHackComponent::NotifyHackHoldCompleted(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor)
{
	if (!IsValid(MarkerWidget)
		|| !IsValid(TargetActor)
		|| TargetActor->IsActorBeingDestroyed()
		|| MarkerWidget != HoveredMarkerWidget
		|| TargetActor != HoveredHackActor)
	{
		return;
	}

	ResetLocalHackHoldProgress();
	ServerTryStartHack(TargetActor);
}

void UPartnerHackComponent::ResetLocalHackHoldProgress()
{
	if (HoveredMarkerWidget)
	{
		HoveredMarkerWidget->CancelHackHold();
	}
}

void UPartnerHackComponent::ServerTryStartHack_Implementation(AActor* TargetActor)
{
	if (!PartnerCharacter)
	{
		return;
	}

	UHackableComponent* HackableComponent = ResolveHackableComponent(TargetActor);
	if (!HackableComponent)
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack failed: no hackable component Target=%s"),
				*GetNameSafe(TargetActor));
		}
		return;
	}

	FVector2D ScreenLocation = FVector2D::ZeroVector;
	const FHackQueryContext Context = BuildQueryContext();
	if (!IsCandidateActorValid(TargetActor, HackableComponent, Context, ScreenLocation))
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack failed: invalid candidate Target=%s Hackable=%s"),
				*GetNameSafe(TargetActor),
				*GetNameSafe(HackableComponent));
		}
		return;
	}

	const float EffectiveRange = CachedAbilityData.EffectiveRange > 0.0f
		? CachedAbilityData.EffectiveRange
		: CachedAbilityData.CandidateRange;
	if (FVector::DistSquared(PartnerCharacter->GetActorLocation(), TargetActor->GetActorLocation()) > FMath::Square(EffectiveRange))
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack failed: outside effective range Target=%s Range=%.1f"),
				*GetNameSafe(TargetActor),
				EffectiveRange);
		}
		return;
	}

	DeactivateUnselectedCandidates(HackableComponent);
	bHackCandidateSearchActive = false;
	SetActiveHackableComponent(HackableComponent);

	//HackedOnce Marked
	ActiveHackableComponent->MarkAsHackedOnce();

	//Actor Override
	if (IHackableInterface* Handler = Cast<IHackableInterface>(TargetActor))
	{
		Handler->HandleHackStarted(Context);
	}

	//Widegt
	ClientStopCandidateSearch();
	ClientStartHackMiniGame(TargetActor, HackableComponent);

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack success Target=%s Hackable=%s Screen=(%.1f, %.1f)"),
			*GetNameSafe(TargetActor),
			*GetNameSafe(HackableComponent),
			ScreenLocation.X,
			ScreenLocation.Y);
	}
}

void UPartnerHackComponent::ClientCompleteHack()
{
	
}

void UPartnerHackComponent::ClearHackCandidates()
{
	for (int32 Index = HackCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		RemoveHackCandidateAt(Index);
	}
}

void UPartnerHackComponent::StopHackCandidateSearch()
{
	ResetLocalHackHoldProgress();
	HoveredMarkerWidget = nullptr;
	HoveredHackActor = nullptr;

	bHackCandidateSearchActive = false;
	DestroyCandidateLayerWidget();
	ClearHackCandidates();
	LastDebugCandidateCount = INDEX_NONE;

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate search stopped"));
	}
}

FHackQueryContext UPartnerHackComponent::BuildQueryContext() const
{
	FHackQueryContext Context;
	Context.InstigatorActor = PartnerCharacter;
	Context.MaxRange = CachedAbilityData.EffectiveRange > 0.0f
		? CachedAbilityData.EffectiveRange
		: CachedAbilityData.CandidateRange;
	Context.RequiredTags = RequiredCandidateTags;
	Context.BlockedTags = BlockedCandidateTags;
	Context.HackMultiUseTags.AddTag(HackGameplayTags::Use::Multiple());

	if (PartnerCharacter)
	{
		if (AController* Controller = PartnerCharacter->GetController())
		{
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(Context.ViewLocation, ViewRotation);
			Context.ViewDirection = ViewRotation.Vector();
		}
		else
		{
			Context.ViewLocation = PartnerCharacter->GetActorLocation();
			Context.ViewDirection = PartnerCharacter->GetActorForwardVector();
		}
	}

	return Context;
}

UHackableComponent* UPartnerHackComponent::ResolveHackableComponent(AActor* Actor) const
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor == PartnerCharacter)
	{
		return nullptr;
	}

	if (!Actor->GetClass()->ImplementsInterface(UHackableInterface::StaticClass()))
	{
		return nullptr;
	}

	UHackableComponent* HackableComponent = Cast<IHackableInterface>(Actor)->GetHackableComponent();
	if (bDebugHack && !HackableComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Interface actor returned null component Actor=%s Class=%s"),
			*GetNameSafe(Actor),
			*GetNameSafe(Actor->GetClass()));
	}

	return HackableComponent;
}

bool UPartnerHackComponent::IsCandidateActorValid(AActor* Actor, UHackableComponent* HackableComponent, const FHackQueryContext& Context, FVector2D& OutScreenLocation) const
{
	if (!IsValid(Actor)
		|| Actor->IsActorBeingDestroyed()
		|| !IsValid(HackableComponent)
		|| !HackableComponent->IsHackTargetType())
	{
		return false;
	}

	if (!IsInsidePartnerFrustum(Actor, OutScreenLocation))
	{
		return false;
	}

	if (!HackableComponent->CanBeHackTarget(Context))
	{
		return false;
	}

	if (bRequireLineOfSight && !HasLineOfSight(Actor))
	{
		return false;
	}

	return true;
}

bool UPartnerHackComponent::IsInsidePartnerFrustum(AActor* Actor, FVector2D& OutScreenLocation) const
{
	if (!Actor || !PartnerCharacter)
	{
		return false;
	}

	const FHackQueryContext Context = BuildQueryContext();
	const FVector ToTarget = Actor->GetActorLocation() - Context.ViewLocation;
	const float DistanceSq = ToTarget.SizeSquared();

	if (DistanceSq > FMath::Square(CachedAbilityData.CandidateRange))
	{
		return false;
	}

	const FVector TargetDirection = ToTarget.GetSafeNormal();
	const float Dot = FVector::DotProduct(Context.ViewDirection.GetSafeNormal(), TargetDirection);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(CandidateHalfAngleDegrees));
	if (Dot < MinDot)
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

bool UPartnerHackComponent::HasLineOfSight(AActor* Actor) const
{
	if (!Actor || !PartnerCharacter || !GetWorld())
	{
		return false;
	}

	FHackQueryContext Context = BuildQueryContext();

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerHackLineOfSight), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Context.ViewLocation,
		Actor->GetActorLocation(),
		ECC_Visibility,
		QueryParams
	);

	return !bHit || Hit.GetActor() == Actor;
}

void UPartnerHackComponent::EnsureCandidateLayerWidget()
{
	if (CandidateLayerWidget || !PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		UE_LOG(LogTemp, Error, TEXT("Not Local"));
		return;
	}

	TSubclassOf<UHackCandidateLayerWidget> EffectiveLayerClass = CandidateLayerWidgetClass;
	if (!EffectiveLayerClass)
	{
		EffectiveLayerClass = UHackCandidateLayerWidget::StaticClass();
	}

	CandidateLayerWidget = CreateWidget<UHackCandidateLayerWidget>(PlayerController, EffectiveLayerClass);
	if (!CandidateLayerWidget)
	{
		return;
	}

	CandidateLayerWidget->SetMarkerWidgetClass(CandidateMarkerWidgetClass);
	CandidateLayerWidget->SetHackableInfoWidgetClass(HackableInfoWidgetClass);
	CandidateLayerWidget->BindHackComponent(this);
	CandidateLayerWidget->AddToViewport(100);

	ApplyCandidateInputMode();
}

void UPartnerHackComponent::DestroyCandidateLayerWidget()
{
	if (!CandidateLayerWidget)
	{
		return;
	}

	CandidateLayerWidget->BindHackComponent(nullptr);
	CandidateLayerWidget->RemoveFromParent();
	CandidateLayerWidget = nullptr;

	RestoreGameInputMode();
}

bool UPartnerHackComponent::EnsureHackMiniGameWidget(AActor* TargetActor, UHackableComponent* HackableComponent)
{
	if (!PartnerCharacter || !TargetActor || !HackableComponent)
	{
		return false;
	}

	if (HackMiniGameWidget)
	{
		return true;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}

	TSubclassOf<UHackMiniGameWidget> EffectiveWidgetClass = HackMiniGameWidgetClass;
	if (!EffectiveWidgetClass)
	{
		EffectiveWidgetClass = UHackMiniGameWidget::StaticClass();
	}

	HackMiniGameWidget = CreateWidget<UHackMiniGameWidget>(PlayerController, EffectiveWidgetClass);
	if (!HackMiniGameWidget)
	{
		return false;
	}

	HackMiniGameWidget->InitializeHackMiniGame(TargetActor, HackableComponent, this);
	HackMiniGameWidget->SetMiniGameTimeLimit(CachedAbilityData.MiniGameTime);
	HackMiniGameWidget->OnHackMiniGameFinished.AddDynamic(this, &UPartnerHackComponent::HandleHackMiniGameFinished);
	HackMiniGameWidget->AddToViewport(150);

	ApplyHackMiniGameInputMode();

	return HackMiniGameWidget->StartHacking();
}

void UPartnerHackComponent::DestroyHackMiniGameWidget()
{

	if (!HackMiniGameWidget)
	{
		RestoreGameInputMode();
		return;
	}

	HackMiniGameWidget->OnHackMiniGameFinished.RemoveDynamic(this, &UPartnerHackComponent::HandleHackMiniGameFinished);
	HackMiniGameWidget->RemoveFromParent();
	HackMiniGameWidget = nullptr;

	RestoreGameInputMode();


}

void UPartnerHackComponent::ApplyCandidateInputMode()
{
	if (!PartnerCharacter)
	{
		return;
	}

	APartnerPlayerController* PlayerController =
		Cast<APartnerPlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (!PlayerController->SetFirstPersonInputMode(FirstPersonInputModeTags::Hack()))
	{
		return;
	}

	FInputModeGameAndUI InputMode;
	if (CandidateLayerWidget)
	{
		InputMode.SetWidgetToFocus(CandidateLayerWidget->TakeWidget());
	}
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);

	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate input mode applied PC=%s"),
			*GetNameSafe(PlayerController));
	}
}

void UPartnerHackComponent::ApplyHackMiniGameInputMode()
{
	if (!PartnerCharacter)
	{
		return;
	}

	APartnerPlayerController* PlayerController =
		Cast<APartnerPlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (!PlayerController->SetFirstPersonInputMode(FirstPersonInputModeTags::Hack()))
	{
		return;
	}

	FInputModeGameAndUI InputMode;
	if (HackMiniGameWidget)
	{
		InputMode.SetWidgetToFocus(HackMiniGameWidget->TakeWidget());
	}

	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);

	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

}

void UPartnerHackComponent::RestoreGameInputMode()
{
	if (!PartnerCharacter)
	{
		return;
	}

	if (HackMiniGameWidget)
	{
		return;
	}

	APartnerPlayerController* PlayerController =
		Cast<APartnerPlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (PlayerController->TryRestoreFirstPersonDefaultInputMode(FirstPersonInputModeTags::Hack()))
	{
		PlayerController->ControlMainWidget(true);
	}


	//UE_LOG(LogTemp, Error, TEXT("[HackInputDebug] RestoreGameInputMode called ??MiniGameWidget=%s CandidateWidget=%s"),
	//	*GetNameSafe(HackMiniGameWidget),
	//	*GetNameSafe(CandidateLayerWidget));
}

void UPartnerHackComponent::AddHackCandidate(AActor* Actor, UHackableComponent* HackableComponent, const FVector2D& ScreenLocation)
{
	if (!Actor || !HackableComponent)
	{
		return;
	}

	HackCandidateActors.Add(Actor);
	HackCandidateComponents.Add(HackableComponent);

	if (CandidateLayerWidget)
	{
		CandidateLayerWidget->AddCandidate(Actor, HackableComponent);
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate added Actor=%s Hackable=%s Screen=(%.1f, %.1f) Tags=%s"),
			*GetNameSafe(Actor),
			*GetNameSafe(HackableComponent),
			ScreenLocation.X,
			ScreenLocation.Y,
			*HackableComponent->HackTags.ToStringSimple());
	}
}

void UPartnerHackComponent::RemoveHackCandidateAt(int32 Index)
{
	if (!HackCandidateComponents.IsValidIndex(Index))
	{
		return;
	}

	UHackableComponent* HackableComponent = HackCandidateComponents[Index];
	AActor* Actor = HackCandidateActors.IsValidIndex(Index) ? HackCandidateActors[Index] : nullptr;

	if (CandidateLayerWidget)
	{
		CandidateLayerWidget->RemoveCandidate(Actor, HackableComponent);
	}

	if (Actor && HoveredHackActor == Actor)
	{
		ResetLocalHackHoldProgress();
		HoveredMarkerWidget = nullptr;
		HoveredHackActor = nullptr;
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate removed Actor=%s Hackable=%s"),
			*GetNameSafe(Actor),
			*GetNameSafe(HackableComponent));
	}

	HackCandidateComponents.RemoveAtSwap(Index);
	if (HackCandidateActors.IsValidIndex(Index))
	{
		HackCandidateActors.RemoveAtSwap(Index);
	}
}

void UPartnerHackComponent::DeactivateUnselectedCandidates(UHackableComponent* SelectedComponent)
{
	for (int32 Index = HackCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		if (HackCandidateComponents[Index] != SelectedComponent)
		{
			RemoveHackCandidateAt(Index);
		}
	}
}

void UPartnerHackComponent::CancelActiveHack()
{
	ResetLocalHackHoldProgress();

	if (HackMiniGameWidget)
	{
		HackMiniGameWidget->CancelHacking();
	}

	if (!ActiveHackableComponent)
	{
		return;
	}

	FHackResultContext ResultContext;
	ResultContext.TargetActor = ActiveHackableComponent->GetOwner();
	ResultContext.InstigatorActor = PartnerCharacter;
	ResultContext.Result = EHackResult::Cancelled;
	ServerCompleteHack(ResultContext);
}

void UPartnerHackComponent::SetActiveHackableComponent(UHackableComponent* HackableComponent)
{
	if (ActiveHackableComponent == HackableComponent)
	{
		return;
	}

	ClearActiveHackableComponent();
	ActiveHackableComponent = HackableComponent;

	if (IsValid(ActiveHackableComponent))
	{
		ActiveHackableComponent->OnHackTargetInvalidated.AddUObject(
			this,
			&UPartnerHackComponent::HandleHackTargetInvalidated);
	}
}

void UPartnerHackComponent::ClearActiveHackableComponent()
{
	if (ActiveHackableComponent)
	{
		ActiveHackableComponent->OnHackTargetInvalidated.RemoveAll(this);
	}

	ActiveHackableComponent = nullptr;
}

void UPartnerHackComponent::AbortLocalHackForInvalidTarget()
{
	ClearActiveHackableComponent();
	ResetLocalHackHoldProgress();
	HoveredMarkerWidget = nullptr;
	HoveredHackActor = nullptr;
	bHackCandidateSearchActive = false;

	// Remove the completion delegate with the widget so the invalid target cannot
	// report a late mini-game result back to the server.
	DestroyHackMiniGameWidget();
	DestroyCandidateLayerWidget();
	ClearHackCandidates();
	LastDebugCandidateCount = INDEX_NONE;
}

void UPartnerHackComponent::HandleHackTargetInvalidated(
	UHackableComponent* InvalidatedComponent,
	EEndPlayReason::Type EndPlayReason)
{
	if (InvalidatedComponent != ActiveHackableComponent)
	{
		return;
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerHackDebug] Hack failed: active target ended Target=%s EndPlayReason=%d"),
			*GetNameSafe(InvalidatedComponent ? InvalidatedComponent->GetOwner() : nullptr),
			static_cast<int32>(EndPlayReason));
	}

	const bool bHasAuthority = GetOwner() && GetOwner()->HasAuthority();
	AbortLocalHackForInvalidTarget();

	if (bHasAuthority)
	{
		ClientAbortHackForInvalidTarget();
		DefaultWidgetControl(false);
	}
}

void UPartnerHackComponent::StartHackMiniGame(AActor* TargetActor, UHackableComponent* HackableComponent)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	if (EnsureHackMiniGameWidget(TargetActor, HackableComponent))
	{
		return;
	}

	FHackResultContext ResultContext;
	ResultContext.TargetActor = TargetActor;
	ResultContext.InstigatorActor = PartnerCharacter;
	ResultContext.Result = EHackResult::Fail;
	ServerCompleteHack(ResultContext);
	DestroyHackMiniGameWidget();
}

void UPartnerHackComponent::HandleHackMiniGameFinished(const FHackResultContext& ResultContext)
{
	DestroyHackMiniGameWidget();

	ServerCompleteHack(ResultContext);
}
