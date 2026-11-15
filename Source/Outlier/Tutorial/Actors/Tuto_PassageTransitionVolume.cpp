#include "Tutorial/Actors/Tuto_PassageTransitionVolume.h"

#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Interaction/PassageHatch.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"

bool ATuto_PassageTransitionVolume::IsLocalTransitionActive(const AFirstPersonPlayerController* Controller)
{
	if (!Controller) return false;
	const ATuto_PassageTransitionVolume* Active = Controller->LocalPassageTransitionVolume.Get();
	return Active && Active->LocalPassageId.IsValid();
}

void ATuto_PassageTransitionVolume::StartLocalPassageFade(
	AFirstPersonPlayerController* Controller, const FGuid& InExecutionId, const FOutlierScreenTransitionTiming& Timing)
{
	if (!IsValid(Controller) || !Controller->IsLocalController() || Controller->GetWorld() != GetWorld()
		|| !InExecutionId.IsValid() || !Controller->GetPawn() || LocalPassageId.IsValid()
		|| IsLocalTransitionActive(Controller)) return;
	ULocalPlayer* Player = Controller->GetLocalPlayer();
	ULocalPlayerPostProcessSubsystem* PP = Player ? Player->GetSubsystem<ULocalPlayerPostProcessSubsystem>() : nullptr;
	if (!PP) return;
	LocalPassageController = Controller;
	Controller->LocalPassageTransitionVolume = this;
	LocalPassagePawn = Controller->GetPawn();
	PassagePostProcessSubsystem = PP;
	LocalPassageId = InExecutionId;
	LocalPassageTiming = Timing;
	bPassageCoveredSent = false;
	bPassageRevealed = false;
	Controller->SetIgnoreMoveInput(true);
	Controller->SetIgnoreLookInput(true);
	bPassageInputLocked = true;
	Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandleLocalPawnChanged);
	Controller->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleLocalParticipantEndPlay);
	Controller->GetPawn()->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleLocalParticipantEndPlay);
	if (AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(LocalPassagePawn.Get()))
		Character->OnSuitTransitionParticipantInvalidated.AddUObject(this, &ThisClass::HandleLocalPawnInvalidated);
	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(LocalPassagePawn.Get()))
		Shooter->OnCharacterDeath.AddUniqueDynamic(this, &ThisClass::HandleLocalCharacterDeath);
	PassageCoveredHandle = PP->OnScreenBlackoutCovered.AddUObject(this, &ThisClass::HandlePassageCovered, InExecutionId);
	GetWorldTimerManager().SetTimer(PassageWatchdogTimer, this, &ThisClass::ClearLocalPassageFade,
		FMath::Max(10.0f, Timing.FadeOutDuration + Timing.FadeInDuration + 5.0f), false);
	PP->StartScreenBlackout(Timing.FadeOutDuration);
}

void ATuto_PassageTransitionVolume::HandlePassageCovered(FGuid InExecutionId)
{
	AFirstPersonPlayerController* Controller = LocalPassageController.Get();
	if (LocalPassageId != InExecutionId || bPassageCoveredSent || !Controller
		|| !LocalPassagePawn.IsValid() || LocalPassagePawn.Get() != Controller->GetPawn()) return;
	if (ULocalPlayerPostProcessSubsystem* PP = PassagePostProcessSubsystem.Get())
		PP->OnScreenBlackoutCovered.Remove(PassageCoveredHandle);
	PassageCoveredHandle.Reset();
	bPassageCoveredSent = true;
	Controller->ServerNotifyPassageCovered(this, InExecutionId);
}

void ATuto_PassageTransitionVolume::RevealLocalPassage(
	AFirstPersonPlayerController* Controller, const FGuid& InExecutionId, FVector Destination)
{
	if (!LocalPassageId.IsValid() || LocalPassageId != InExecutionId
		|| LocalPassageController.Get() != Controller || !bPassageCoveredSent) return;
	PassageArrivalLocation = Destination;
	GetWorldTimerManager().SetTimer(PassageArrivalTimer, this, &ThisClass::PollPassageArrival, 0.02f, true);
	PollPassageArrival();
}

void ATuto_PassageTransitionVolume::PollPassageArrival()
{
	AFirstPersonPlayerController* Controller = LocalPassageController.Get();
	if (!Controller || !LocalPassagePawn.IsValid() || LocalPassagePawn.Get() != Controller->GetPawn())
	{
		ClearLocalPassageFade();
		return;
	}
	if (!LocalPassagePawn->GetActorLocation().Equals(PassageArrivalLocation, 20.0f)) return;
	GetWorldTimerManager().ClearTimer(PassageArrivalTimer);
	if (ULocalPlayerPostProcessSubsystem* PP = PassagePostProcessSubsystem.Get())
	{
		PassageRevealedHandle = PP->OnScreenBlackoutRevealed.AddUObject(this, &ThisClass::HandlePassageRevealed);
		PP->StartScreenBlackoutReveal(LocalPassageTiming.FadeInDuration);
	}
	else ClearLocalPassageFade();
}

void ATuto_PassageTransitionVolume::HandlePassageRevealed()
{
	if (!LocalPassageId.IsValid()) return;
	const FGuid RestoredId = LocalPassageId;
	AFirstPersonPlayerController* Controller = LocalPassageController.Get();
	APawn* Participant = LocalPassagePawn.Get();
	bPassageRevealed = true;
	ClearLocalPassageFade();
	// Listen Host의 ACK는 즉시 서버로 재진입한다. 입력 잠금을 먼저 반납한 뒤 완료를 알린다.
	if (Controller) Controller->ServerNotifyPresentationRestored(RestoredId, Participant);
}

void ATuto_PassageTransitionVolume::CancelLocalPassage(
	AFirstPersonPlayerController* Controller, const FGuid& InExecutionId)
{
	if (LocalPassageId == InExecutionId && LocalPassageController.Get() == Controller) ClearLocalPassageFade();
}

void ATuto_PassageTransitionVolume::ClearLocalPassageFade()
{
	const bool bWasActive = LocalPassageId.IsValid();
	LocalPassageId.Invalidate();
	bPassageCoveredSent = false;
	GetWorldTimerManager().ClearTimer(PassageWatchdogTimer);
	GetWorldTimerManager().ClearTimer(PassageArrivalTimer);
	if (ULocalPlayerPostProcessSubsystem* PP = PassagePostProcessSubsystem.Get())
	{
		PP->OnScreenBlackoutCovered.Remove(PassageCoveredHandle);
		PP->OnScreenBlackoutRevealed.Remove(PassageRevealedHandle);
		if (bWasActive && !bPassageRevealed) PP->StartScreenBlackoutReveal(LocalPassageTiming.CleanupRevealDuration);
	}
	PassageCoveredHandle.Reset();
	PassageRevealedHandle.Reset();
	if (APawn* LocalPawn = LocalPassagePawn.Get())
	{
		LocalPawn->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleLocalParticipantEndPlay);
		if (AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(LocalPawn))
			Character->OnSuitTransitionParticipantInvalidated.RemoveAll(this);
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(LocalPawn))
			Shooter->OnCharacterDeath.RemoveDynamic(this, &ThisClass::HandleLocalCharacterDeath);
	}
	if (AFirstPersonPlayerController* Controller = LocalPassageController.Get())
	{
		if (Controller->LocalPassageTransitionVolume.Get() == this)
			Controller->LocalPassageTransitionVolume.Reset();
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleLocalPawnChanged);
		Controller->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleLocalParticipantEndPlay);
		if (bPassageInputLocked)
		{
			Controller->SetIgnoreMoveInput(false);
			Controller->SetIgnoreLookInput(false);
		}
	}
	bPassageInputLocked = false;
	LocalPassageController.Reset();
	LocalPassagePawn.Reset();
	PassagePostProcessSubsystem.Reset();
}

void ATuto_PassageTransitionVolume::ResetLocalPassageForReload()
{
	// 새 로딩 화면을 설치하기 전에 구연출 콜백/입력만 반납한다. 뒤늦은 취소는 새 화면을 걷지 못한다.
	bPassageRevealed = true;
	ClearLocalPassageFade();
}

void ATuto_PassageTransitionVolume::HandleLocalPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (NewPawn != LocalPassagePawn.Get()) ClearLocalPassageFade();
}

void ATuto_PassageTransitionVolume::HandleLocalParticipantEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	if (HasAuthority()) CancelTransition();
	ClearLocalPassageFade();
}

void ATuto_PassageTransitionVolume::HandleLocalCharacterDeath()
{
	if (HasAuthority()) CancelTransition();
	ClearLocalPassageFade();
}

void ATuto_PassageTransitionVolume::HandleLocalPawnInvalidated(AFirstPersonCharacter* Character)
{
	if (HasAuthority()) CancelTransition();
	ClearLocalPassageFade();
}

bool ATuto_PassageTransitionVolume::HandlePawnEntered(AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || ExecutionId.IsValid()) return false;
	PassageTiming = AOutlierPostProcessVolume::ResolveScreenTransitionTiming(GetWorld(), GetActorLocation());
	if (!HasAuthority() || ExecutionId.IsValid() || !CanTriggerForRequiredActor() || !IsValid(SourcePlayerState)
		|| !IsValid(TeleportDestination) || !FMath::IsFinite(PassageTiming.FadeOutDuration) || !FMath::IsFinite(PassageTiming.FadeInDuration)
		|| PassageTiming.FadeOutDuration < 0.0f || PassageTiming.FadeInDuration < 0.0f
		|| !FMath::IsFinite(PassageTiming.CleanupRevealDuration) || PassageTiming.CleanupRevealDuration < 0.0f) return false;
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
	// 먼저 서버에서 페어 보호를 획득한다. 한쪽 실패 시 취소가 이미 획득한 보호만 복구한다.
	if (!ShooterPC->BeginPresentationProtection(ExecutionId, Shooter, PartnerPC)
		|| !PartnerPC->BeginPresentationProtection(ExecutionId, Partner, ShooterPC))
	{
		CancelTransition();
		return false;
	}
	// Pawn 교체/사망은 화면 ACK를 기다릴 수 없다. 서버도 같은 실행을 즉시 취소한다.
	for (AFirstPersonCharacter* Participant : {static_cast<AFirstPersonCharacter*>(Shooter), static_cast<AFirstPersonCharacter*>(Partner)})
	{
		Participant->OnSuitTransitionParticipantInvalidated.AddUObject(this, &ThisClass::HandleLocalPawnInvalidated);
		Participant->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleLocalParticipantEndPlay);
	}
	Shooter->OnCharacterDeath.AddUniqueDynamic(this, &ThisClass::HandleLocalCharacterDeath);
	FadeStartedAt = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(TransitionTimeout, this, &ThisClass::CancelTransition, FMath::Max(7.0f, PassageTiming.FadeOutDuration + 2.0f), false);
	ShooterPC->ClientStartPassageFade(this, ExecutionId, PassageTiming);
	PartnerPC->ClientStartPassageFade(this, ExecutionId, PassageTiming);
	// The base volume stays unconsumed while the asynchronous transition is pending.
	return false;
}

void ATuto_PassageTransitionVolume::NotifyScreenCovered(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId)
{
	if (!HasAuthority() || !IsValid(Controller) || bTransitionCommitted || !ExecutionId.IsValid() || ExecutionId != InExecutionId) return;
	if (!Controller->IsPresentationProtectionCurrent(InExecutionId, Controller->GetPawn()))
	{
		CancelTransition();
		return;
	}
	if (Controller == ShooterController.Get() && Controller->GetPawn() == PendingShooter.Get()) bShooterCovered = true;
	else if (Controller == PartnerController.Get() && Controller->GetPawn() == PendingPartner.Get()) bPartnerCovered = true;
	else return;
	if (!bShooterCovered || !bPartnerCovered) return;
	const double Remaining = PassageTiming.FadeOutDuration - (GetWorld()->GetTimeSeconds() - FadeStartedAt);
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
		|| !ShooterController->IsPresentationProtectionCurrent(ExecutionId, PendingShooter.Get())
		|| !PartnerController->IsPresentationProtectionCurrent(ExecutionId, PendingPartner.Get())
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
	ShooterController->AwaitPresentationProtectionRestore(ExecutionId, PassageTiming.FadeInDuration);
	PartnerController->AwaitPresentationProtectionRestore(ExecutionId, PassageTiming.FadeInDuration);
	ShooterController->ClientRevealPassage(this, ExecutionId, PendingShooter->GetActorLocation());
	PartnerController->ClientRevealPassage(this, ExecutionId, PendingPartner->GetActorLocation());
}

void ATuto_PassageTransitionVolume::CancelTransition()
{
	GetWorldTimerManager().ClearTimer(TransitionTimeout);
	GetWorldTimerManager().ClearTimer(MinimumFadeTimer);
	const FGuid OldId = ExecutionId;
	ExecutionId.Invalidate();
	for (APawn* Participant : {PendingShooter.Get(), PendingPartner.Get()})
	{
		if (!Participant) continue;
		Participant->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleLocalParticipantEndPlay);
		if (AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(Participant))
			Character->OnSuitTransitionParticipantInvalidated.RemoveAll(this);
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(Participant))
			Shooter->OnCharacterDeath.RemoveDynamic(this, &ThisClass::HandleLocalCharacterDeath);
	}
	if (OldId.IsValid())
	{
		for (AFirstPersonPlayerController* Controller : { ShooterController.Get(), PartnerController.Get() })
		{
			if (!Controller) continue;
			Controller->ClientClearPassageFade(this, OldId);
			Controller->RecoverPresentationProtection(OldId, PassageTiming.CleanupRevealDuration);
		}
	}
	PendingSource.Reset();
	PendingShooter.Reset();
	PendingPartner.Reset();
	ShooterController.Reset();
	PartnerController.Reset();
}

void ATuto_PassageTransitionVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearLocalPassageFade();
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
