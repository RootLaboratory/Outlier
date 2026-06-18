// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/PlayerController.h"
#include "Interface/HackableInterface.h"
#include "UI/HackCandidateLayerWidget.h"

UPartnerHackComponent::UPartnerHackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPartnerHackComponent::BeginPlay()
{
	Super::BeginPlay();

	BlockedCandidateTags.AddTag(HackGameplayTags::State::Dead());
	BlockedCandidateTags.AddTag(HackGameplayTags::State::HackedOnce());
	BlockedCandidateTags.AddTag(HackGameplayTags::State::Locked());
	//얘는 버릴듯
	BlockedCandidateTags.AddTag(HackGameplayTags::State::Immune());
}

void UPartnerHackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

	RefreshHackCandidates();
}

bool UPartnerHackComponent::TryHack()
{
	if (!PartnerCharacter || !GetWorld())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHack blocked Partner=%s World=%d"),
				*GetNameSafe(PartnerCharacter),
				GetWorld() ? 1 : 0);
		}
		return false;
	}

	bHackCandidateSearchActive = true;
	EnsureCandidateLayerWidget();
	RefreshHackCandidates();

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHack started Partner=%s Range=%.1f HalfAngle=%.1f LOS=%d Candidates=%d"),
			*GetNameSafe(PartnerCharacter),
			CandidateRange,
			CandidateHalfAngleDegrees,
			bRequireLineOfSight ? 1 : 0,
			HackCandidateComponents.Num());
	}

	return true;
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
		FCollisionShape::MakeSphere(CandidateRange),
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
		else
		{
			HackableComponent->SetProjectedScreenLocation(ScreenLocation);
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

bool UPartnerHackComponent::TryStartHack(AActor* TargetActor)
{
	UHackableComponent* HackableComponent = ResolveHackableComponent(TargetActor);
	if (!HackableComponent)
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack failed: no hackable component Target=%s"),
				*GetNameSafe(TargetActor));
		}
		return false;
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
		return false;
	}

	DeactivateUnselectedCandidates(HackableComponent);

	bHackCandidateSearchActive = false;
	DestroyCandidateLayerWidget();
	ActiveHackableComponent = HackableComponent;
	HackableComponent->BeginHack(Context);
	OnHackStarted.Broadcast(TargetActor, HackableComponent);

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack success Target=%s Hackable=%s Screen=(%.1f, %.1f)"),
			*GetNameSafe(TargetActor),
			*GetNameSafe(HackableComponent),
			ScreenLocation.X,
			ScreenLocation.Y);
	}

	return true;
}

void UPartnerHackComponent::CompleteHack(const FHackResultContext& ResultContext)
{
	if (!ActiveHackableComponent)
	{
		return;
	}

	FHackResultContext MutableResultContext = ResultContext;
	MutableResultContext.TargetActor = MutableResultContext.TargetActor.Get()
		? MutableResultContext.TargetActor.Get()
		: ActiveHackableComponent->GetOwner();
	MutableResultContext.InstigatorActor = MutableResultContext.InstigatorActor.Get()
		? MutableResultContext.InstigatorActor.Get()
		: PartnerCharacter;

	ActiveHackableComponent->CompleteHack(MutableResultContext);
	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] CompleteHack Target=%s Result=%d"),
			*GetNameSafe(MutableResultContext.TargetActor.Get()),
			static_cast<int32>(MutableResultContext.Result));
	}
	ActiveHackableComponent = nullptr;
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
	Context.MaxRange = CandidateRange; //외부에서 받아오는 거로. 
	Context.RequiredTags = RequiredCandidateTags;
	Context.BlockedTags = BlockedCandidateTags;

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
	if (!Actor || Actor == PartnerCharacter)
	{
		return nullptr;
	}

	if (!Actor->GetClass()->ImplementsInterface(UHackableInterface::StaticClass()))
	{
		return nullptr;
	}

	UHackableComponent* HackableComponent = IHackableInterface::Execute_GetHackableComponent(Actor);
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
	if (!Actor || !HackableComponent || !HackableComponent->IsHackTargetType())
	{
		return false;
	}

	if (!HackableComponent->CanBeHackTarget(Context))
	{
		return false;
	}

	if (!IsInsidePartnerFrustum(Actor, OutScreenLocation))
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

	if (DistanceSq > FMath::Square(CandidateRange))
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

void UPartnerHackComponent::ApplyCandidateInputMode()
{
	if (!PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
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

void UPartnerHackComponent::RestoreGameInputMode()
{
	if (!PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	FInputModeGameOnly InputMode;
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = false;

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Game input mode restored PC=%s"),
			*GetNameSafe(PlayerController));
	}
}

void UPartnerHackComponent::AddHackCandidate(AActor* Actor, UHackableComponent* HackableComponent, const FVector2D& ScreenLocation)
{
	if (!Actor || !HackableComponent)
	{
		return;
	}

	HackCandidateActors.Add(Actor);
	HackCandidateComponents.Add(HackableComponent);

	FHackProcessContext ProcessContext;
	ProcessContext.InstigatorActor = PartnerCharacter;
	ProcessContext.TargetActor = Actor;
	ProcessContext.HackProcess = EHackProcess::Try;
	ProcessContext.ScreenLocation = ScreenLocation;
	HackableComponent->SetHackProcess(ProcessContext);

	OnHackCandidateAdded.Broadcast(Actor, HackableComponent);

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

	if (HackableComponent)
	{
		FHackProcessContext ProcessContext;
		ProcessContext.InstigatorActor = PartnerCharacter;
		ProcessContext.TargetActor = Actor;
		ProcessContext.HackProcess = EHackProcess::Done;
		HackableComponent->SetHackProcess(ProcessContext);
	}

	OnHackCandidateRemoved.Broadcast(Actor, HackableComponent);

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

