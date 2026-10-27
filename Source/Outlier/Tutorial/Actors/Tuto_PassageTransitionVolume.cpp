#include "Tutorial/Actors/Tuto_PassageTransitionVolume.h"

#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Interaction/PassageHatch.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"

bool ATuto_PassageTransitionVolume::HandlePawnEntered(AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || ExecutionId.IsValid() || !CanTriggerForRequiredActor() || !IsValid(SourcePlayerState)
		|| !IsValid(TeleportDestination) || !FMath::IsFinite(FadeOutDuration) || !FMath::IsFinite(FadeInDuration)
		|| FadeOutDuration < 0.0f || FadeOutDuration > 2.0f || FadeInDuration < 0.0f || FadeInDuration > 2.0f) return false;
	AShooterCharacter* Shooter = SourcePlayerState->GetShooterCharacter();
	APartnerCharacter* Partner = SourcePlayerState->GetPartnerCharacter();
	if (!IsValid(Shooter) || !IsValid(Partner)) return false;
	auto* ShooterPC = Cast<AFirstPersonPlayerController>(Shooter->GetController());
	auto* PartnerPC = Cast<AFirstPersonPlayerController>(Partner->GetController());
	if (!IsValid(ShooterPC) || !IsValid(PartnerPC) || ShooterPC == PartnerPC) return false;
	PendingSource = SourcePlayerState;
	PendingShooter = Shooter;
	PendingPartner = Partner;
	ShooterController = ShooterPC;
	PartnerController = PartnerPC;
	bShooterCovered = bPartnerCovered = false;
	ExecutionId = FGuid::NewGuid();
	FadeStartedAt = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(TransitionTimeout, this, &ThisClass::CancelTransition, 7.0f, false);
	ShooterPC->ClientStartPassageFade(this, ExecutionId, FadeOutDuration);
	PartnerPC->ClientStartPassageFade(this, ExecutionId, FadeOutDuration);
	// The base volume stays unconsumed while the asynchronous transition is pending.
	return false;
}

void ATuto_PassageTransitionVolume::NotifyScreenCovered(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId)
{
	if (!HasAuthority() || !IsValid(Controller) || bTransitionCommitted || !ExecutionId.IsValid() || ExecutionId != InExecutionId) return;
	if (Controller == ShooterController.Get() && Controller->GetPawn() == PendingShooter.Get()) bShooterCovered = true;
	else if (Controller == PartnerController.Get() && Controller->GetPawn() == PendingPartner.Get()) bPartnerCovered = true;
	else return;
	if (!bShooterCovered || !bPartnerCovered) return;
	const double Remaining = FadeOutDuration - (GetWorld()->GetTimeSeconds() - FadeStartedAt);
	if (Remaining > 0.0)
		GetWorldTimerManager().SetTimer(MinimumFadeTimer, this, &ThisClass::FinishCoveredTransition, static_cast<float>(Remaining), false);
	else FinishCoveredTransition();
}

void ATuto_PassageTransitionVolume::FinishCoveredTransition()
{
	if (bTransitionCommitted) return;
	if (!ExecutionId.IsValid() || !ShooterController.IsValid() || !PartnerController.IsValid()
		|| !PendingSource.IsValid() || !PendingShooter.IsValid() || !PendingPartner.IsValid()
		|| ShooterController->GetPawn() != PendingShooter.Get() || PartnerController->GetPawn() != PendingPartner.Get()
		|| PendingSource->GetShooterCharacter() != PendingShooter.Get()
		|| PendingSource->GetPartnerCharacter() != PendingPartner.Get() || !TeleportPair(PendingSource.Get()))
	{
		CancelTransition();
		return;
	}
	MarkEntryComplete();
	bTransitionCommitted = true;
	GetWorldTimerManager().ClearTimer(TransitionTimeout);
	GetWorldTimerManager().ClearTimer(MinimumFadeTimer);
	ShooterController->ClientRevealPassage(ExecutionId, PendingShooter->GetActorLocation(), FadeInDuration);
	PartnerController->ClientRevealPassage(ExecutionId, PendingPartner->GetActorLocation(), FadeInDuration);
}

void ATuto_PassageTransitionVolume::CancelTransition()
{
	GetWorldTimerManager().ClearTimer(TransitionTimeout);
	GetWorldTimerManager().ClearTimer(MinimumFadeTimer);
	const FGuid OldId = ExecutionId;
	ExecutionId.Invalidate();
	if (OldId.IsValid())
	{
		if (ShooterController.IsValid()) ShooterController->ClientClearPassageFade(OldId);
		if (PartnerController.IsValid()) PartnerController->ClientClearPassageFade(OldId);
	}
	PendingSource.Reset();
	PendingShooter.Reset();
	PendingPartner.Reset();
	ShooterController.Reset();
	PartnerController.Reset();
}

void ATuto_PassageTransitionVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority()) CancelTransition();
	Super::EndPlay(EndPlayReason);
}

bool ATuto_PassageTransitionVolume::TeleportPair(AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || !CanTriggerForRequiredActor() || !IsValid(SourcePlayerState)) return false;
	AShooterCharacter* Shooter = SourcePlayerState->GetShooterCharacter();
	APartnerCharacter* Partner = SourcePlayerState->GetPartnerCharacter();
	if (!IsValid(Shooter) || !IsValid(Partner) || !IsValid(TeleportDestination)
		|| TeleportDestination->IsActorBeingDestroyed()) return false;
	if (Shooter->GetWorld() != GetWorld() || Partner->GetWorld() != GetWorld()) return false;
	if (TargetHatch && (!IsValid(TargetHatch) || TargetHatch->IsActorBeingDestroyed())) return false;

	const FQuat DestinationRotation = TeleportDestination->GetActorQuat();
	const FRotator Rotation = DestinationRotation.Rotator();
	FVector ShooterLocation = TeleportDestination->GetActorLocation();
	FVector PartnerLocation = ShooterLocation + DestinationRotation.RotateVector(PartnerSpawnOffset);
	// FindTeleportSpot checks without moving either pawn. TeleportTo(bIsATest=true) still moves it.
	if (!GetWorld()->FindTeleportSpot(Shooter, ShooterLocation, Rotation)
		|| !GetWorld()->FindTeleportSpot(Partner, PartnerLocation, Rotation)) return false;
	const float Separation = Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ Partner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	if (FVector::DistSquared(ShooterLocation, PartnerLocation) < FMath::Square(Separation)) return false;

	const FTransform PreviousShooter = Shooter->GetActorTransform();
	const FTransform PreviousPartner = Partner->GetActorTransform();
	auto RestorePair = [&]()
	{
		Shooter->TeleportTo(PreviousShooter.GetLocation(), PreviousShooter.Rotator(), false, true);
		Partner->TeleportTo(PreviousPartner.GetLocation(), PreviousPartner.Rotator(), false, true);
	};
	// Only consume the tutorial entry and close the hatch once both moves have succeeded.
	if (!Shooter->TeleportTo(ShooterLocation, Rotation) || !Partner->TeleportTo(PartnerLocation, Rotation))
	{
		RestorePair();
		return false;
	}
	const bool bTutorialAccepted = Super::HandlePawnEntered(SourcePlayerState);
	// This volume also works as a passage transition with no event or active world guide.
	if (!bTutorialAccepted && EventTag.IsValid())
	{
		RestorePair();
		return false;
	}
	for (ACharacter* Character : {static_cast<ACharacter*>(Shooter), static_cast<ACharacter*>(Partner)})
	{
		Character->SetBase(nullptr);
		Character->GetCharacterMovement()->StopMovementImmediately();
		if (AController* Controller = Character->GetController())
		{
			Controller->SetControlRotation(Rotation);
			Controller->ClientSetRotation(Rotation, true);
		}
		Character->ForceNetUpdate();
	}

	if (IsValid(TargetHatch)) TargetHatch->CloseHatch();
	return true;
}
