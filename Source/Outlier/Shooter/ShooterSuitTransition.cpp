#include "Shooter/ShooterCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/SuitInteraction.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "TimerManager.h"

// 서버 전환 흐름: 검증/예약 -> 두 Pawn 차단 -> FadingOut -> commit -> Applying -> FadingIn -> 종료.
// 각 단계는 두 소유 Controller의 해당 단계 완료 응답과 서버 최소 시간이 모두 충족되어야 진행한다.
// commit 전 취소는 예약만 되돌리고, commit 후 취소는 지급/획득을 유지한 채 전환 차단만 해제한다.
// Interaction은 commit 후 제거될 수 있으므로 남은 응답 대기와 타이머의 수명은 Shooter가 소유한다.
bool AShooterCharacter::BeginSuitTransition(ASuitInteraction* Interaction,
	USkeletalMesh* LegacyFirstPersonMesh, USkeletalMesh* LegacyThirdPersonMesh)
{
	// 1. 아직 외형/무기를 변경하지 않는다. 시작 조건과 설정이 유효해야 예약 단계로 넘어간다.
	if (!CanBeginSuitTransition(Interaction))
	{
		return false;
	}
	if (!ValidateSuitTransitionTiming())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SuitTransition] Invalid timing configuration Owner=%s"), *GetName());
		return false;
	}
	FShooterPresentationConfiguration Configuration;
	FString Error;
	if (!ResolvePresentationConfiguration(true, Configuration, Error, LegacyFirstPersonMesh, LegacyThirdPersonMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SuitTransition] Invalid Suit configuration Owner=%s Reason=%s"), *GetName(), *Error);
		return false;
	}
	// 2. 시작 당시 Pair/Pawn/Controller를 고정한다. 대기 중 교체된 참가자의 응답은 이어받지 않는다.
	SuitTransitionPartner = GetPartnerCharacter();
	SuitTransitionShooterController = Cast<APlayerController>(GetController());
	SuitTransitionPartnerController = Cast<APlayerController>(SuitTransitionPartner->GetController());
	SuitTransitionShooterPlayerState = GetPlayerState<AOutlierPlayerState>();
	SuitTransitionPartnerPlayerState = SuitTransitionPartner->GetPlayerState<AOutlierPlayerState>();
	if (!ValidateSuitTransitionParticipants(true) || !Interaction->ReserveFor(this))
	{
		SuitTransitionPartner.Reset();
		SuitTransitionShooterController.Reset();
		SuitTransitionPartnerController.Reset();
		SuitTransitionShooterPlayerState.Reset();
		SuitTransitionPartnerPlayerState.Reset();
		return false;
	}
	// 3. 예약을 획득한 뒤 전환 ID와 이탈 감시를 먼저 준비하고, 같은 ID로 두 Pawn을 차단한다.
	// 이 시점의 성공 반환은 요청 수락일 뿐이다. 획득 성공 통지는 이후 commit에서만 보낸다.
	SuitTransitionInteraction = Interaction;
	SuitTransitionConfiguration = Configuration;
	ActiveSuitTransitionId = FGuid::NewGuid();
	bSuitTransitionCommitted = false;
	bSuitTransitionCancelRequested = false;
	OnSuitTransitionParticipantInvalidated.AddUObject(this, &AShooterCharacter::HandleSuitTransitionParticipantInvalidated);
	SuitTransitionPartner->OnSuitTransitionParticipantInvalidated.AddUObject(this, &AShooterCharacter::HandleSuitTransitionParticipantInvalidated);
	SuitTransitionShooterPlayerState->OnPlayerCharactersChanged.AddUObject(this, &AShooterCharacter::HandleSuitTransitionPairChanged);
	SuitTransitionPartnerPlayerState->OnPlayerCharactersChanged.AddUObject(this, &AShooterCharacter::HandleSuitTransitionPairChanged);
	const FGuid Id = ActiveSuitTransitionId;
	APartnerCharacter* Partner = SuitTransitionPartner.Get();
	// 기존 행동 취소에서 Pair/생존 이벤트가 재진입할 수 있다. 취소된 ID로 다음 차단을 획득하지 않는다.
	if (!AcquireSuitTransitionBlock(Id) || ActiveSuitTransitionId != Id || !IsValid(Partner)
		|| !Partner->AcquireSuitTransitionBlock(Id) || ActiveSuitTransitionId != Id)
	{
		CancelSuitTransition();
		return false;
	}
	EnterSuitTransitionPhase(ESuitTransitionPhase::FadingOut);
	return true;
}

bool AShooterCharacter::CanBeginSuitTransition(ASuitInteraction* Interaction)
{
	return HasAuthority() && !ActiveSuitTransitionId.IsValid() && !IsSuitTransitionBlocked()
		&& !IsDead() && !HasAcquiredSuit() && IsValid(Interaction) && Interaction->CanReserveFor(this);
}

bool AShooterCharacter::ValidateSuitTransitionTiming() const
{
	const auto IsValidDuration = [](float Duration) { return FMath::IsFinite(Duration) && Duration >= 0.0f; };
	return IsValidDuration(SuitFadeOutDuration) && IsValidDuration(SuitBlackHoldDuration)
		&& IsValidDuration(SuitFadeInDuration) && FMath::IsFinite(SuitTransitionResponseTimeout)
		&& SuitTransitionResponseTimeout > FMath::Max3(SuitFadeOutDuration, SuitBlackHoldDuration, SuitFadeInDuration);
}

bool AShooterCharacter::ValidateSuitTransitionParticipants(bool bForReservation) const
{
	// 시작 검증에는 미획득/Partner 무장 여부/기존 차단까지 포함한다.
	// 진행 중에는 이미 이 전환이 차단을 소유하고 commit 후에는 무기도 있으므로 Pair/생존 검증만 공통 적용한다.
	return !IsDead()
		&& ValidateSuitTransitionControllers()
		&& ValidateSuitTransitionPair()
		&& ValidateSuitTransitionPartnerState(bForReservation);
}

bool AShooterCharacter::ValidateSuitTransitionControllers() const
{
	const APartnerCharacter* Partner = SuitTransitionPartner.Get();
	const APlayerController* ShooterController = SuitTransitionShooterController.Get();
	const APlayerController* PartnerController = SuitTransitionPartnerController.Get();
	// 시작 당시 Pawn이 유지되고, 두 Controller가 여전히 그 Pawn을 각각 소유해야 한다.
	if (!IsValid(Partner) || GetPartnerCharacter() != Partner
		|| Partner->IsActorBeingDestroyed() || IsActorBeingDestroyed())
	{
		return false;
	}
	return ShooterController && PartnerController && ShooterController != PartnerController
		&& GetController() == ShooterController && Partner->GetController() == PartnerController
		&& ShooterController->GetPawn() == this && PartnerController->GetPawn() == Partner;
}

bool AShooterCharacter::ValidateSuitTransitionPair() const
{
	const APartnerCharacter* Partner = SuitTransitionPartner.Get();
	const AOutlierPlayerState* ShooterPS = GetPlayerState<AOutlierPlayerState>();
	const AOutlierPlayerState* PartnerPS = Partner ? Partner->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (!ShooterPS || !PartnerPS || ShooterPS != SuitTransitionShooterPlayerState.Get()
		|| PartnerPS != SuitTransitionPartnerPlayerState.Get())
	{
		return false;
	}
	if (!ShooterPS->IsShooterPlayer() || !PartnerPS->IsPartnerPlayer()
		|| ShooterPS->GetPairId() < 0 || ShooterPS->GetPairId() != PartnerPS->GetPairId())
	{
		return false;
	}
	// PairId뿐 아니라 양쪽 PlayerState의 실제 Pawn 연결도 시작 당시 Pair와 일치해야 한다.
	return ShooterPS->GetShooterCharacter() == this && PartnerPS->GetShooterCharacter() == this
		&& ShooterPS->GetPartnerCharacter() == Partner && PartnerPS->GetPartnerCharacter() == Partner;
}

bool AShooterCharacter::ValidateSuitTransitionPartnerState(bool bForReservation) const
{
	const APartnerCharacter* Partner = SuitTransitionPartner.Get();
	const UAbilitySystemComponent* PartnerASC = Partner ? Partner->GetAbilitySystemComponent() : nullptr;
	if (!PartnerASC || PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting())
		|| PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return false;
	}
	if (!bForReservation)
	{
		return true;
	}
	// PlayerState의 존재/Pair 연결은 앞선 참가자 검증에서 확인한다. 예약 시작에만 지급 전 조건을 적용한다.
	return !GetPlayerState<AOutlierPlayerState>()->GetAcquiredSuit()
		&& !Partner->GetPlayerState<AOutlierPlayerState>()->GetAcquiredSuit()
		&& !Partner->GetCurrentWeapon() && !Partner->IsSuitTransitionBlocked();
}

bool AShooterCharacter::CanCommitSuitTransition() const
{
	ASuitInteraction* Interaction = SuitTransitionInteraction.Get();
	const APartnerCharacter* Partner = SuitTransitionPartner.Get();
	const AOutlierPlayerState* PS = GetPlayerState<AOutlierPlayerState>();
	// 참가자 검증 이후 호출한다. 대기 중 예약/획득/무장 조건이 바뀌었다면 지급하지 않는다.
	if (!Interaction || !Interaction->IsReservedFor(this) || PS->GetAcquiredSuit()
		|| Partner->GetPlayerState<AOutlierPlayerState>()->GetAcquiredSuit() || Partner->GetCurrentWeapon())
	{
		return false;
	}
	FShooterPresentationConfiguration Configuration;
	FString Error;
	if (!ResolvePresentationConfiguration(true, Configuration, Error,
		Interaction->ShooterFirstPersonMesh, Interaction->ShooterThirdPersonMesh))
	{
		return false;
	}
	// 유효한 설정이어도 시작 때 검증한 구성과 다르면 거부한다. 암전 대기 중 교체된 설정을 적용하지 않는다.
	return Configuration.FirstPersonMesh == SuitTransitionConfiguration.FirstPersonMesh
		&& Configuration.ThirdPersonMesh == SuitTransitionConfiguration.ThirdPersonMesh
		&& Configuration.FirstPersonAnimClass == SuitTransitionConfiguration.FirstPersonAnimClass
		&& Configuration.ThirdPersonAnimClass == SuitTransitionConfiguration.ThirdPersonAnimClass;
}

float AShooterCharacter::GetSuitTransitionMinimumTime(ESuitTransitionPhase Phase) const
{
	switch (Phase)
	{
	case ESuitTransitionPhase::FadingOut:
		return SuitFadeOutDuration;
	case ESuitTransitionPhase::Applying:
		return SuitBlackHoldDuration;
	default:
		return SuitFadeInDuration;
	}
}

bool AShooterCharacter::IsSuitTransitionPhaseReady() const
{
	if (!bSuitTransitionShooterReady || !bSuitTransitionPartnerReady
		|| GetWorld()->GetTimeSeconds() - SuitTransitionPhaseStartedAt + KINDA_SMALL_NUMBER
			< GetSuitTransitionMinimumTime(SuitTransitionPhase))
	{
		return false;
	}
	return true;
}

void AShooterCharacter::PrepareForSuitTransition()
{
	// 게임플레이 상태와 예약된 후속 처리를 먼저 취소한다. 몽타주만 멈추면 Reload/Equip 상태가 남을 수 있다.
	Super::PrepareForSuitTransition();
	ClearInputIntent();
	StopLean();
	StopAimInternal();
	CancelReloadInternal();
	CancelLocalProceduralWeaponSwitch();
	if (InventoryComponent && HasAuthority())
	{
		InventoryComponent->CancelPendingWeaponSwitch();
	}
	if (CombatComponent)
	{
		CombatComponent->CancelMeleeAttack();
	}
	if (IsSliding())
	{
		StopSlide(ESlideEndReason::ForcedCancel);
	}
	StopSprintInternal();
	EndActionLock(EShooterActionLock::Equip);
	// 취소 상태를 확정한 뒤 포즈 재생을 중단하여 종료 콜백/Notify가 이전 행동을 완료하지 못하게 한다.
	for (const USkeletalMeshComponent* Component : { GetFirstPersonMesh(), GetMesh() })
	{
		if (!Component) { continue; }
		if (UAnimInstance* Instance = Component->GetAnimInstance())
		{
			Instance->Montage_Stop(0.0f);
		}
		// Linked ABP에도 이전 Reload/Equip Notify가 남지 않도록 함께 중단한다.
		for (UAnimInstance* LinkedInstance : Component->GetLinkedAnimInstances())
		{
			if (LinkedInstance) { LinkedInstance->Montage_Stop(0.0f); }
		}
	}
	if (HasAuthority())
	{
		CancelActiveQuantumLeap();
		EndActiveBulletReflection(false);
		EndActiveWeaponOvercharge(false);
		EndActiveStealth(false);
	}
}

void AShooterCharacter::OnSuitTransitionBlockChanged()
{
	RefreshShooterSuitAvailabilityUI();
}

void AShooterCharacter::EnterSuitTransitionPhase(ESuitTransitionPhase Phase)
{
	// 단계마다 타이머와 두 참가자의 준비 여부를 새로 시작한다. 이전 단계 응답은 다음 단계 완료로 재사용하지 않는다.
	GetWorldTimerManager().ClearTimer(SuitTransitionMinimumTimer);
	GetWorldTimerManager().ClearTimer(SuitTransitionTimeoutTimer);
	SuitTransitionPhase = Phase;
	SuitTransitionPhaseStartedAt = GetWorld()->GetTimeSeconds();
	bSuitTransitionShooterReady = false;
	bSuitTransitionPartnerReady = false;
	const FGuid Id = ActiveSuitTransitionId;
	const float MinimumTime = GetSuitTransitionMinimumTime(Phase);
	// 응답이 먼저 오면 최소 시간 타이머가, 시간이 먼저 지나면 마지막 참가자의 응답이 진행을 재검사한다.
	// 타이머는 준비 응답을 대신 생성하지 않는다. ID/단계를 캡처해 이전 타이머의 지연 호출도 무시한다.
	FTimerDelegate MinimumDelegate = FTimerDelegate::CreateWeakLambda(this, [this, Id, Phase]()
	{
		if (ActiveSuitTransitionId == Id && SuitTransitionPhase == Phase)
		{
			AdvanceSuitTransition();
		}
	});
	if (MinimumTime > 0.0f)
	{
		GetWorldTimerManager().SetTimer(SuitTransitionMinimumTimer, MinimumDelegate, MinimumTime, false);
	}
	else
	{
		SuitTransitionMinimumTimer = GetWorldTimerManager().SetTimerForNextTick(MinimumDelegate);
	}
	GetWorldTimerManager().SetTimer(SuitTransitionTimeoutTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Id, Phase]()
		{
			if (ActiveSuitTransitionId == Id && SuitTransitionPhase == Phase)
			{
				UE_LOG(LogTemp, Warning, TEXT("[SuitTransition] Timeout Owner=%s Phase=%d Committed=%d"),
					*GetName(), static_cast<int32>(Phase), bSuitTransitionCommitted ? 1 : 0);
				CancelSuitTransition();
			}
		}), SuitTransitionResponseTimeout, false);
	// 이 이벤트 자체는 암전/준비 완료가 아니다. 실제 연출 연결 전에는 timeout으로 취소한다.
	OnSuitTransitionPhaseChanged.Broadcast(Id, Phase);
	// 시작 당시 Controller만 요청을 받는다. Listen Host 콜백이 즉시 다음 단계/취소로 재진입할 수 있으므로
	// 각 전송 직전에 ID/단계를 다시 확인하여 이전 단계 요청이 다음 단계 뒤에 전달되지 않게 한다.
	for (TWeakObjectPtr<APlayerController> ParticipantController : { SuitTransitionShooterController, SuitTransitionPartnerController })
	{
		if (ActiveSuitTransitionId != Id || SuitTransitionPhase != Phase)
		{
			break;
		}
		if (AFirstPersonPlayerController* FirstPersonController = Cast<AFirstPersonPlayerController>(ParticipantController.Get()))
		{
			FirstPersonController->SendSuitTransitionPhaseFromServer(this, Id, Phase, MinimumTime);
		}
	}
}

void AShooterCharacter::AcknowledgeSuitTransition(APlayerController* Sender,
	const FGuid& TransitionId, ESuitTransitionPhase Phase)
{
	// 완료 응답은 준비 신호일 뿐 지급 권한이 아니다. 현재 ID/단계와 시작 당시 소유 Controller/Pawn을 검사한다.
	if (!HasAuthority() || !TransitionId.IsValid() || ActiveSuitTransitionId != TransitionId
		|| SuitTransitionPhase != Phase || Phase == ESuitTransitionPhase::Idle || !IsValid(Sender))
	{
		return;
	}
	if (Sender == SuitTransitionShooterController.Get() && Sender->GetPawn() == this)
	{
		if (bSuitTransitionShooterReady) { return; }
		bSuitTransitionShooterReady = true;
	}
	else if (Sender == SuitTransitionPartnerController.Get() && Sender->GetPawn() == SuitTransitionPartner.Get())
	{
		if (bSuitTransitionPartnerReady) { return; }
		bSuitTransitionPartnerReady = true;
	}
	else
	{
		return;
	}
	// 두 참가자가 각각 한 번씩 응답했어도 최소 시간/Pair/설정 검증을 통과해야 실제 단계가 진행된다.
	AdvanceSuitTransition();
}

void AShooterCharacter::AdvanceSuitTransition()
{
	if (!HasAuthority() || !ActiveSuitTransitionId.IsValid() || bSuitTransitionCommitInProgress)
	{
		return;
	}
	if (!ValidateSuitTransitionParticipants(false))
	{
		CancelSuitTransition();
		return;
	}
	if (!IsSuitTransitionPhaseReady())
	{
		return;
	}
	if (SuitTransitionPhase == ESuitTransitionPhase::FadingOut)
	{
		// 두 화면의 암전 완료가 확인된 유일한 지급 구간이다. Applying 응답을 기다리기 전에 서버 commit을 끝낸다.
		if (bSuitTransitionCommitted)
		{
			return;
		}
		// 기존 Rifle을 제거하기 직전에 예약/설정/획득 여부를 다시 검사한다. 검증 이후 바뀐 설정은 거부한다.
		if (!CanCommitSuitTransition())
		{
			CancelSuitTransition();
			return;
		}
		// 지급/획득 이벤트의 재진입은 commit 중복이나 중간 차단 해제를 만들지 않는다.
		// 취소 요청은 동기 commit 결과가 확정된 뒤 처리하여 성공 여부를 정확히 구분한다.
		bSuitTransitionCommitInProgress = true;
		bSuitTransitionCommitted = SuitTransitionInteraction->CommitReservedSuit(this);
		bSuitTransitionCommitInProgress = false;
		if (!bSuitTransitionCommitted || bSuitTransitionCancelRequested)
		{
			CancelSuitTransition();
			return;
		}
		if (ActiveSuitTransitionId.IsValid())
		{
			EnterSuitTransitionPhase(ESuitTransitionPhase::Applying);
		}
	}
	else if (SuitTransitionPhase == ESuitTransitionPhase::Applying)
	{
		// 지급은 이미 완료됐다. 두 클라이언트의 새 외형/무기 준비와 검은 화면 유지 시간이 끝나면 복귀를 요청한다.
		EnterSuitTransitionPhase(ESuitTransitionPhase::FadingIn);
	}
	else if (SuitTransitionPhase == ESuitTransitionPhase::FadingIn)
	{
		// 두 화면의 복귀 완료와 최소 복귀 시간이 충족된 뒤에만 전환 차단을 해제한다.
		FinishSuitTransition();
	}
}

void AShooterCharacter::HandleSuitTransitionParticipantInvalidated(AFirstPersonCharacter* Participant)
{
	(void)Participant;
	CancelSuitTransition();
}

void AShooterCharacter::HandleSuitTransitionPairChanged(AOutlierPlayerState* ChangedPlayerState)
{
	(void)ChangedPlayerState;
	if (!ValidateSuitTransitionParticipants(false))
	{
		CancelSuitTransition();
	}
}

void AShooterCharacter::CancelSuitTransition()
{
	if (HasAuthority() && ActiveSuitTransitionId.IsValid())
	{
		if (bSuitTransitionCommitInProgress)
		{
			bSuitTransitionCancelRequested = true;
			return;
		}
		FinishSuitTransition();
	}
}

void AShooterCharacter::FinishSuitTransition()
{
	// 정상 완료와 취소가 공유하는 종료 경로다. 획득을 되돌릴지 여부가 아니라 예약 실패 통지 여부만 commit으로 구분한다.
	const FGuid Id = ActiveSuitTransitionId;
	const bool bCommitted = bSuitTransitionCommitted;
	ASuitInteraction* Interaction = SuitTransitionInteraction.Get();
	APartnerCharacter* Partner = SuitTransitionPartner.Get();
	// 내부 참가자 참조를 비운 뒤에도 종료 요청을 보낼 수 있게 시작 당시 Controller를 확보한다.
	const TWeakObjectPtr<APlayerController> ShooterController = SuitTransitionShooterController;
	const TWeakObjectPtr<APlayerController> PartnerController = SuitTransitionPartnerController;
	GetWorldTimerManager().ClearTimer(SuitTransitionMinimumTimer);
	GetWorldTimerManager().ClearTimer(SuitTransitionTimeoutTimer);
	// ID를 먼저 무효화하여 해제 중 생긴 이벤트/늦은 응답이 이전 전환을 다시 진행하지 못하게 한다.
	ActiveSuitTransitionId.Invalidate();
	SuitTransitionPhase = ESuitTransitionPhase::Idle;
	OnSuitTransitionParticipantInvalidated.RemoveAll(this);
	if (AOutlierPlayerState* PS = SuitTransitionShooterPlayerState.Get())
	{
		PS->OnPlayerCharactersChanged.RemoveAll(this);
	}
	if (AOutlierPlayerState* PS = SuitTransitionPartnerPlayerState.Get())
	{
		PS->OnPlayerCharactersChanged.RemoveAll(this);
	}
	if (Partner)
	{
		Partner->OnSuitTransitionParticipantInvalidated.RemoveAll(this);
		Partner->ReleaseSuitTransitionBlock(Id);
	}
	ReleaseSuitTransitionBlock(Id);
	// commit 전 실패만 Interaction을 재시도 가능하게 되돌린다. commit 후에는 소비된 Actor 없이도 종료할 수 있다.
	if (!bCommitted && IsValid(Interaction))
	{
		Interaction->ReleaseReservation(this);
		CompleteDeferredInteraction(Interaction, false);
	}
	SuitTransitionInteraction.Reset();
	SuitTransitionPartner.Reset();
	SuitTransitionShooterController.Reset();
	SuitTransitionPartnerController.Reset();
	SuitTransitionShooterPlayerState.Reset();
	SuitTransitionPartnerPlayerState.Reset();
	SuitTransitionConfiguration = FShooterPresentationConfiguration();
	bSuitTransitionCommitted = false;
	bSuitTransitionCancelRequested = false;
	for (TWeakObjectPtr<APlayerController> ParticipantController : { ShooterController, PartnerController })
	{
		if (AFirstPersonPlayerController* FirstPersonController = Cast<AFirstPersonPlayerController>(ParticipantController.Get()))
		{
			FirstPersonController->SendSuitTransitionPhaseFromServer(this, Id, ESuitTransitionPhase::Idle, 0.0f);
		}
	}
	// 내부 ID는 이미 무효지만, 종료 알림에는 이전 ID를 전달해 연결된 Controller가 해당 연출만 정리하게 한다.
	OnSuitTransitionPhaseChanged.Broadcast(Id, ESuitTransitionPhase::Idle);
}
