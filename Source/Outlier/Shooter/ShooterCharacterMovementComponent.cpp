#include "Shooter/ShooterCharacterMovementComponent.h"

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Components/PrimitiveComponent.h"

#if !UE_BUILD_SHIPPING
#include "Network/OutlierArenaSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterSlide, Log, All);

static TAutoConsoleVariable<int32> CVarShooterSlideDiagnostic(
	TEXT("outlier.SlideDiagnostic"), 0,
	TEXT("Shooter movement diagnostics: 0=off, 1=events, 2=events and simulation samples. Non-shipping only."),
	ECVF_Default);
#endif

DEFINE_LOG_CATEGORY_STATIC(LogShooterCoyote, Log, All);

bool FShooterMovementInput::operator==(const FShooterMovementInput& Other) const
{
	return Epoch == Other.Epoch && CrouchRequest == Other.CrouchRequest && JumpCancelRequest == Other.JumpCancelRequest
		&& bSprint == Other.bSprint && bCrouch == Other.bCrouch && bSprintAtCrouch == Other.bSprintAtCrouch;
}

void FShooterMovementInput::Serialize(FArchive& Ar)
{
	Ar.SerializeIntPacked(Epoch);
	Ar.SerializeIntPacked(CrouchRequest);
	Ar.SerializeIntPacked(JumpCancelRequest);
	Ar.SerializeBits(&bSprint, 1);
	Ar.SerializeBits(&bCrouch, 1);
	Ar.SerializeBits(&bSprintAtCrouch, 1);
}

void FShooterSlideSnapshot::Serialize(FArchive& Ar)
{
	AppliedInput.Serialize(Ar);
	Ar.SerializeIntPacked(ProcessedCrouchRequest);
	Ar.SerializeIntPacked(ProcessedJumpCancelRequest);
	Ar.SerializeIntPacked(ActiveRequest);
	Ar.SerializeBits(&bActive, 1);
	Ar.SerializeBits(&bSprintHeld, 1);
	Ar.SerializeBits(&bCrouchHeld, 1);
	Ar.SerializeBits(&bCrouched, 1);
	Ar << EndReason;
	if (bActive)
	{
		Ar << Direction << StartSpeed << CurrentSpeed << Elapsed << Duration;
	}
}

UShooterMovementComponent* UShooterCharacterMovementComponent::GetShooterMovement() const
{
	const auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	return Shooter ? Shooter->MovementComponent.Get() : nullptr;
}

bool UShooterCharacterMovementComponent::IsReplayingMovement() const
{
	return bRestoringMovementState || (CharacterOwner && CharacterOwner->bClientUpdating);
}

bool UShooterCharacterMovementComponent::QueueMovementInput(EShooterMovementInput Input)
{
	if (bApplyingMovementInput || !CharacterOwner || !CharacterOwner->IsLocallyControlled())
	{
		return false;
	}
	switch (Input)
	{
	case EShooterMovementInput::SprintPressed: LiveMovementInput.bSprint = true; break;
	case EShooterMovementInput::SprintReleased: LiveMovementInput.bSprint = false; break;
	case EShooterMovementInput::CrouchPressed:
		if (++NextMovementRequest == 0) ++NextMovementRequest;
		LiveMovementInput.CrouchRequest = NextMovementRequest;
		LiveMovementInput.bSprintAtCrouch = LiveMovementInput.bSprint;
		LiveMovementInput.bCrouch = true;
		break;
	case EShooterMovementInput::CrouchReleased: LiveMovementInput.bCrouch = false; break;
	case EShooterMovementInput::SlideJumpCancel:
		if (++NextMovementRequest == 0) ++NextMovementRequest;
		LiveMovementInput.JumpCancelRequest = NextMovementRequest;
		break;
	}
	return true;
}

void UShooterCharacterMovementComponent::ApplyMovementInput()
{
	UShooterMovementComponent* Movement = GetShooterMovement();
	if (!Movement || (!bUsingMovementInput && !CharacterOwner->IsLocallyControlled()))
	{
		return;
	}
	if (!bUsingMovementInput)
	{
		MoveInput = LiveMovementInput;
	}
	if (MoveInput.Epoch != MovementEpoch)
	{
		// 이전 수명주기의 입력은 재전송/재생되더라도 소비하지 않는다. 새 기준은 서버 보정으로 전달한다.
		CharacterOwner->StopJumping();
		bWantsToCrouch = bSlideSimulating || Movement->WantsToCrouch();
		return;
	}
	bMovementInputTransition = !(MoveInput == AppliedMovementInput);
	TGuardValue<bool> InputGuard(bApplyingMovementInput, true);
	const auto IsNewer = [](uint32 Request, uint32 Processed)
	{
		return Request != 0 && static_cast<int32>(Request - Processed) > 0;
	};
	const bool bNewCrouch = IsNewer(MoveInput.CrouchRequest, ProcessedCrouchRequest);
	const bool bNewCancel = IsNewer(MoveInput.JumpCancelRequest, ProcessedJumpCancelRequest);
	const auto ApplyCancel = [&]()
	{
		ProcessedJumpCancelRequest = MoveInput.JumpCancelRequest;
		if (bSlideSimulating) Movement->StopSlide(ESlideEndReason::JumpCancel);
		CharacterOwner->StopJumping();
	};
	const auto ApplySprint = [&](bool bHeld)
	{
		if (bHeld) Movement->HandleSprintPressed();
		else Movement->HandleSprintReleased();
	};
	// 같은 프레임에 누르고 놓아도 시작 요청은 남는다. 취소와 재입력은 요청 순서대로 판정한다.
	const bool bCancelFirst = bNewCancel && bNewCrouch
		&& static_cast<int32>(MoveInput.CrouchRequest - MoveInput.JumpCancelRequest) > 0;
	if (bCancelFirst)
	{
		ApplyCancel();
	}
	if (bNewCrouch)
	{
		ProcessedCrouchRequest = MoveInput.CrouchRequest; // 거절한 요청도 소비해 재생/재전송으로 되살리지 않는다.
		ApplySprint(MoveInput.bSprintAtCrouch);
		const uint32 PreviousRequest = ActiveSlideRequest;
		if (!bSlideSimulating) ActiveSlideRequest = MoveInput.CrouchRequest;
		Movement->RequestCrouchOrSlide();
		if (!bSlideSimulating) ActiveSlideRequest = PreviousRequest;
	}
	if (bNewCancel && !bCancelFirst)
	{
		ApplyCancel();
	}
	if (bNewCrouch || MoveInput.bSprint != AppliedMovementInput.bSprint)
	{
		ApplySprint(MoveInput.bSprint);
	}
	if (!MoveInput.bCrouch && (bNewCrouch || AppliedMovementInput.bCrouch))
	{
		Movement->RequestUncrouch();
	}
	AppliedMovementInput = MoveInput;

	// 예측 측과 서버의 Slide 활성 상태가 달라도 Slide 중 점프는 취소만 수행한다.
	if (bSlideSimulating && CharacterOwner->bPressedJump)
	{
		Movement->StopSlide(ESlideEndReason::JumpCancel);
		CharacterOwner->StopJumping();
	}
	// 엔진 compressed flag에 남은 과거 앉기 상태가 서버 거절/취소 결과를 덮어쓰지 않게 한다.
	bWantsToCrouch = bSlideSimulating || Movement->WantsToCrouch();
}

void UShooterCharacterMovementComponent::SaveSlidePhysics()
{
	SavedGroundFriction = GroundFriction;
	SavedBrakingDecelerationWalking = BrakingDecelerationWalking;
	SavedBrakingFrictionFactor = BrakingFrictionFactor;
	SavedMaxAcceleration = MaxAcceleration;
	SavedMaxWalkSpeedCrouched = MaxWalkSpeedCrouched;
}

FShooterSlideSnapshot UShooterCharacterMovementComponent::CaptureSlideSnapshot() const
{
	FShooterSlideSnapshot Result;
	Result.AppliedInput = AppliedMovementInput;
	Result.ProcessedCrouchRequest = ProcessedCrouchRequest;
	Result.ProcessedJumpCancelRequest = ProcessedJumpCancelRequest;
	Result.ActiveRequest = ActiveSlideRequest;
	Result.Direction = SlideDirection;
	Result.StartSpeed = SlideStartSpeed;
	Result.CurrentSpeed = CurrentSlideSpeed;
	Result.Elapsed = SlideElapsedTime;
	Result.Duration = SlideSimulationDuration;
	Result.EndReason = SlideEndReason;
	Result.bActive = bSlideSimulating;
	Result.bCrouched = CharacterOwner && CharacterOwner->bIsCrouched;
	if (const auto* Movement = GetShooterMovement())
	{
		Result.bSprintHeld = Movement->WantsToSprint();
		Result.bCrouchHeld = Movement->WantsToCrouch();
	}
	return Result;
}

void UShooterCharacterMovementComponent::RestoreSlideSnapshot(const FShooterSlideSnapshot& Snapshot)
{
	auto* Shooter = CastChecked<AShooterCharacter>(CharacterOwner);
	auto* Movement = GetShooterMovement();
	if (!Movement) return;
	if (static_cast<int32>(Snapshot.AppliedInput.Epoch - MovementEpoch) < 0) return;
	if (Snapshot.AppliedInput.Epoch != MovementEpoch)
	{
		MovementEpoch = Snapshot.AppliedInput.Epoch;
		// 전환 전 홀드/진입 요청을 새 서버 기준에 붙이지 않는다. 요청 순번 자체는 계속 증가한다.
		LiveMovementInput = {};
		LiveMovementInput.Epoch = MovementEpoch;
		MoveInput = LiveMovementInput;
	}
	// 위치/속도는 엔진 보정 결과를 유지한다. 진입 API를 다시 호출하면 배율과 연출이 중복된다.
	if (!Snapshot.bActive) EndSlideSimulation();
	else
	{
		if (!bSlideSimulating) SaveSlidePhysics();
		bSlideSimulating = true;
		++SlideSimulationSerial;
		SlideMovement = Movement;
		SlideDirection = Snapshot.Direction;
		SlideStartSpeed = Snapshot.StartSpeed;
		CurrentSlideSpeed = Snapshot.CurrentSpeed;
		SlideElapsedTime = Snapshot.Elapsed;
		SlideSimulationDuration = Snapshot.Duration;
		GroundFriction = BrakingDecelerationWalking = BrakingFrictionFactor = MaxAcceleration = 0.0f;
		MaxWalkSpeedCrouched = CurrentSlideSpeed;
		PublishSlideState();
	}
	AppliedMovementInput = Snapshot.AppliedInput;
	ProcessedCrouchRequest = Snapshot.ProcessedCrouchRequest;
	ProcessedJumpCancelRequest = Snapshot.ProcessedJumpCancelRequest;
	// 같은 Pawn을 새 소유자가 조종하면 로컬 순번은 낮을 수 있다. 서버의 소비 지점 뒤에서 새 요청을 발급한다.
	if (static_cast<int32>(ProcessedCrouchRequest - NextMovementRequest) > 0) NextMovementRequest = ProcessedCrouchRequest;
	if (static_cast<int32>(ProcessedJumpCancelRequest - NextMovementRequest) > 0) NextMovementRequest = ProcessedJumpCancelRequest;
	ActiveSlideRequest = Snapshot.ActiveRequest;
	SlideEndReason = Snapshot.EndReason;
	Movement->bWantsToSprint = Snapshot.bSprintHeld;
	Movement->bWantsToCrouch = Snapshot.bCrouchHeld;
	Movement->bIsSliding = Snapshot.bActive;
	Movement->bIsSlidingCanceled = !Snapshot.bActive && Snapshot.EndReason != uint8(ESlideEndReason::Finished);
	if (Snapshot.bActive && Shooter->GetActionLock() == EShooterActionLock::None)
	{
		Shooter->BeginActionLock(EShooterActionLock::Slide);
	}
	else if (!Snapshot.bActive)
	{
		Shooter->EndActionLock(EShooterActionLock::Slide);
	}
	bWantsToCrouch = Snapshot.bActive || Snapshot.bCrouchHeld;
	if (Shooter->bIsCrouched != Snapshot.bCrouched)
	{
		// 위치 보정은 캡슐 자세까지 되돌리지 않는다. 승인된 자세만 복원하고 보정된 중심은 다시 이동하지 않는다.
		// 이후 새 앉기/일어서기는 기존 지형 검사를 그대로 사용한다.
		Shooter->SetIsCrouched(Snapshot.bCrouched);
		if (Snapshot.bCrouched) Crouch(true);
		else UnCrouch(true);
		bForceNextFloorCheck = true;
	}
	Movement->RefreshMovementState();
}

void UShooterCharacterMovementComponent::ReconcileSlidePresentation()
{
	if (IsReplayingMovement()) return;
	auto* Shooter = CastChecked<AShooterCharacter>(CharacterOwner);
	// 같은 승인 요청이 뒤늦게 활성 상태로 보정돼도 시작 연출을 다시 재생하지 않는다.
	const bool bNewRequest = ActiveSlideRequest != 0 && PresentedSlideRequest != ActiveSlideRequest;
	const bool bDirectEntry = ActiveSlideRequest == 0 && !bSlidePresented;
	if (bSlideSimulating && (PresentedSlideEpoch != MovementEpoch || bNewRequest || bDirectEntry))
	{
#if WITH_DEV_AUTOMATION_TESTS
		++SlidePresentationStarts;
#endif
		if (Shooter->HasAuthority()) Shooter->MulticastPlayThirdPersonActionMontage(EShooterMontageAction::Slide, Shooter->GetWeaponType());
		if (Shooter->IsLocallyControlled()) Shooter->PlayFirstPersonActionMontage(EShooterMontageAction::Slide, Shooter->GetWeaponType());
		PresentedSlideRequest = ActiveSlideRequest;
		PresentedSlideEpoch = MovementEpoch;
	}
	else if (!bSlideSimulating && bSlidePresented)
	{
#if WITH_DEV_AUTOMATION_TESTS
		++SlidePresentationStops;
#endif
		if (Shooter->HasAuthority()) Shooter->MulticastStopSlideMontage();
		// 소유자가 아닌 서버의 TP 중단은 multicast의 로컬 실행이 담당한다. 소유자는 예측 연출만 직접 중단한다.
		if (!Shooter->HasAuthority() || Shooter->IsLocallyControlled())
		{
			Shooter->StopSplitMontages(Shooter->GetActionMontage(EShooterMontageAction::Slide, true),
				Shooter->GetActionMontage(EShooterMontageAction::Slide, false));
		}
	}
	bSlidePresented = bSlideSimulating;
	Shooter->RefreshSlideAudio();
}

void UShooterCharacterMovementComponent::ResetSlideForLifecycle()
{
	auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	auto* Movement = GetShooterMovement();
	if (!Shooter || !Movement) return;
	const bool bHadSlidePresentation = bSlidePresented;

	// 1. 홀드를 먼저 비워 종료 과정에서 Sprint/앉기가 재개되지 않게 하고, 기존 종료 API로 물리를 복구한다.
	Movement->ClearInputIntent();
	Movement->StopSlide(ESlideEndReason::ForcedCancel);
	EndSlideSimulation();
	Movement->bIsSliding = false;
	Shooter->EndActionLock(EShooterActionLock::Slide);
	Movement->StopSprintInternal();
	bWantsToCrouch = false;
	Shooter->UnCrouch();
	Shooter->StopJumping();
	ReconcileSlidePresentation();
	// 관찰자는 CMC 시뮬레이션을 소유하지 않아 bSlidePresented가 false여도 수신한 몽타주가 있을 수 있다.
	if (!bHadSlidePresentation || IsReplayingMovement())
	{
		Shooter->StopSplitMontages(Shooter->GetActionMontage(EShooterMontageAction::Slide, true),
			Shooter->GetActionMontage(EShooterMontageAction::Slide, false));
	}
	bSlidePresented = false;

	// 2. 서버만 수명주기 번호를 발급한다. 클라이언트의 새 번호는 다음 이동 보정에서 확정한다.
	if (Shooter->HasAuthority()) ++MovementEpoch;
	LiveMovementInput = {};
	LiveMovementInput.Epoch = MovementEpoch;
	MoveInput = AppliedMovementInput = LiveMovementInput;
	ActiveSlideRequest = PresentedSlideRequest = 0;
	SlideEndReason = uint8(ESlideEndReason::ForcedCancel);
	bMovementInputTransition = true;
	SlideDirection = Movement->SlideDirection = FVector::ZeroVector;
	SlideStartSpeed = CurrentSlideSpeed = SlideElapsedTime = SlideSimulationDuration = 0.0f;
	Movement->SlideStartSpeed = Movement->SlideElapsedTime = 0.0f;
	// 일상적인 Slide 종료의 보간은 유지한다. 수명주기 경계에서만 이전 카메라 효과를 즉시 버린다.
	Shooter->TargetSlideCameraEffectAlpha = Shooter->CurrentSlideCameraEffectAlpha = 0.0f;
	Shooter->SlideCameraEffectElapsedTime = Shooter->SlideCameraEffectDuration = 0.0f;
	Shooter->TargetSlideCameraRollDegrees = Shooter->ActiveSlideCameraRollDegrees = 0.0f;
	Movement->RefreshMovementState();

	// 3. 전환 전 보정/저장 이동은 새 위치나 새 조종으로 재생하지 않는다. 이동 timestamp와 요청 순번은 유지한다.
	bHasCorrectedJumpInput = false;
	if (ClientPredictionData)
	{
		if (CharacterOwner->bClientUpdating) LifecycleReplayMoves.Append(ClientPredictionData->SavedMoves);
		ClientPredictionData->SavedMoves.Reset();
		ClientPredictionData->PendingMove.Reset();
		ClientPredictionData->LastAckedMove.Reset();
		ClientPredictionData->bUpdatePosition = false;
	}
	if (ServerPredictionData)
	{
		ServerPredictionData->PendingAdjustment.TimeStamp = 0.0f;
		ServerPredictionData->bForceClientUpdate = true;
	}
	PendingServerSlide = CaptureSlideSnapshot();
#if !UE_BUILD_SHIPPING
	Movement->SlideDiagnosticId = 0;
	SlideDiagnosticStartTime = 0.0;
	DiagnosticCaptureUntil = DiagnosticReplayTimeStamp = -1.0f;
#endif
}

class FShooterSavedMove : public FSavedMove_Character
{
	using Super = FSavedMove_Character;

public:
	FShooterJumpSnapshot BeforeJump;
	uint32 AudioMoveId = 0;
	uint32 AudioAirborneId = 0;
	bool bStartedFalling = false;
	FShooterMovementInput MovementInput;
	uint32 SlideRequest = 0;
	bool bSliding = false;
	bool bSprintHeld = false;
	bool bCrouchHeld = false;
	bool bCrouched = false;
	uint8 EndReason = 0;

	virtual void Clear() override
	{
		Super::Clear();
		BeforeJump = {};
		AudioMoveId = AudioAirborneId = 0;
		bStartedFalling = false;
		MovementInput = {};
		SlideRequest = 0;
		bSliding = false;
		bSprintHeld = bCrouchHeld = false;
		bCrouched = false;
		EndReason = 0;
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		const auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
		// 소유 클라이언트에서는 CheckJumpInput 다음에 저장된다. 소비 후 상태를 저장하면 재생 때 첫 점프가 사라진다.
		BeforeJump = Movement->BeforeJumpInput;
		AudioMoveId = Movement->AudioMoveId;
		AudioAirborneId = Movement->AudioAirborneId;
		JumpCurrentCount = BeforeJump.JumpCount;
		bWasJumping = BeforeJump.bWasJumping;
		JumpKeyHoldTime = BeforeJump.HoldTime;
		JumpForceTimeRemaining = BeforeJump.ForceTime;
		bStartedFalling = Movement->IsFalling();
		MovementInput = Movement->MoveInput;
		SlideRequest = Movement->ActiveSlideRequest;
		bSliding = Movement->IsSlideSimulating();
		bForceNoCombine |= bSliding || Movement->bMovementInputTransition;
		bForceNoCombine |= BeforeJump.Coyote.bAvailable || bStartedFalling || bPressedJump || JumpCurrentCount != 0;
	}

	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode Mode) override
	{
		Super::PostUpdate(Character, Mode);
		const auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
		AudioAirborneId = Movement->AudioAirborneId;
		bForceNoCombine |= bSliding != Movement->IsSlideSimulating() || SlideRequest != Movement->ActiveSlideRequest;
		bSliding = Movement->IsSlideSimulating();
		SlideRequest = Movement->ActiveSlideRequest;
		const FShooterSlideSnapshot Slide = Movement->CaptureSlideSnapshot();
		bSprintHeld = Slide.bSprintHeld;
		bCrouchHeld = Slide.bCrouchHeld;
		bCrouched = Slide.bCrouched;
		EndReason = Slide.EndReason;
		// 바닥 이탈을 포함한 이동도 결합하지 않아 서버에서 지지 상실 시각을 다시 판정할 수 있게 한다.
		bForceNoCombine |= Character->GetCharacterMovement()->IsFalling();
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const auto* NewShooterMove = static_cast<const FShooterSavedMove*>(NewMove.Get());
		return !BeforeJump.Coyote.bAvailable && !NewShooterMove->BeforeJump.Coyote.bAvailable
			&& !bSliding && !NewShooterMove->bSliding && MovementInput == NewShooterMove->MovementInput
			&& !bStartedFalling && !NewShooterMove->bStartedFalling
			&& Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
	{
		const auto* Acked = static_cast<const FShooterSavedMove*>(LastAckedMove.Get());
		return !Acked || !(MovementInput == Acked->MovementInput) || bSliding != Acked->bSliding
			|| Super::IsImportantMove(LastAckedMove);
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
#if !UE_BUILD_SHIPPING
		// 재생 중에는 현재 입력 시간이 아니라 저장된 이동의 timestamp로 로그를 연결한다.
		if (UShooterCharacterMovementComponent::IsSlideDiagnosticEnabled())
		{
			Movement->DiagnosticReplayTimeStamp = TimeStamp;
			if (Movement->ShouldTraceSlideMovement())
			{
				Movement->LogSlideDiagnostic(TEXT("ReplayMove"), DeltaTime, TimeStamp);
			}
		}
#endif
		// 첫 재생은 서버 보정 상태, 이후 재생은 직전 이동의 결과를 잇는다.
		// 원래 예측했던 횟수/유예를 매번 복원하면 서버가 거부한 기회가 다시 살아난다.
		const FShooterJumpSnapshot ReplayStart = Movement->CaptureJumpSnapshot();
		Super::PrepMoveFor(Character);
		Movement->RestoreJumpSnapshot(ReplayStart);
		// 게임플레이 이력은 보정 결과를 잇되, 연출 키는 원래 저장 이동의 것을 재사용한다.
		Movement->AudioMoveId = AudioMoveId;
		Movement->AudioAirborneId = AudioAirborneId;
		Movement->MoveInput = MovementInput;
	}
};

class FShooterClientPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	explicit FShooterClientPredictionData(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement) {}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FShooterSavedMove());
	}
};

UShooterCharacterMovementComponent::UShooterCharacterMovementComponent()
{
	SetMoveResponseDataContainer(MoveResponse);
	SetNetworkMoveDataContainer(AudioMoveData);
}

FShooterAudioMoveDataContainer::FShooterAudioMoveDataContainer()
{
	NewMoveData = &Moves[0];
	PendingMoveData = &Moves[1];
	OldMoveData = &Moves[2];
}

void FShooterAudioMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	const auto& Move = static_cast<const FShooterSavedMove&>(ClientMove);
	AudioMoveId = Move.AudioMoveId;
	AudioAirborneId = Move.AudioAirborneId;
	MovementInput = Move.MovementInput;
	PredictedSlideRequest = Move.SlideRequest;
	bPredictedSliding = Move.bSliding;
	bPredictedSprintHeld = Move.bSprintHeld;
	bPredictedCrouchHeld = Move.bCrouchHeld;
	bPredictedCrouched = Move.bCrouched;
	PredictedEndReason = Move.EndReason;
}

bool FShooterAudioMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar,
	UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	if (!FCharacterNetworkMoveData::Serialize(Movement, Ar, PackageMap, MoveType)) return false;
	Ar.SerializeIntPacked(AudioMoveId);
	Ar.SerializeIntPacked(AudioAirborneId);
	MovementInput.Serialize(Ar);
	Ar.SerializeIntPacked(PredictedSlideRequest);
	Ar.SerializeBits(&bPredictedSliding, 1);
	Ar.SerializeBits(&bPredictedSprintHeld, 1);
	Ar.SerializeBits(&bPredictedCrouchHeld, 1);
	Ar.SerializeBits(&bPredictedCrouched, 1);
	Ar << PredictedEndReason;
	return !Ar.IsError();
}

uint64 UShooterCharacterMovementComponent::GetMovementAudioEventId(bool bLanding) const
{
	const uint32 Id = bLanding ? AudioAirborneId : AudioMoveId;
	return Id ? (uint64(Id) << 1) | uint64(bLanding) : 0;
}

void UShooterCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime,
	uint8 CompressedFlags, const FVector& NewAccel)
{
	const auto* Move = static_cast<const FShooterAudioMoveData*>(GetCurrentNetworkMoveData());
	TGuardValue<bool> NetworkAudioGuard(bUsingNetworkAudioIds, Move != nullptr);
	TGuardValue<bool> MovementInputGuard(bUsingMovementInput, Move != nullptr);
	if (Move)
	{
		AudioMoveId = Move->AudioMoveId;
		AudioAirborneId = Move->AudioAirborneId;
		MoveInput = Move->MovementInput;
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UShooterCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	// 기존 소유 관계를 시작 시 한 번만 찾아 진단 샘플마다 컴포넌트/서브시스템 검색을 하지 않는다.
	DiagnosticMovement = CharacterOwner ? CharacterOwner->FindComponentByClass<UShooterMovementComponent>() : nullptr;
	DiagnosticArena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
#endif
}

#if !UE_BUILD_SHIPPING
bool UShooterCharacterMovementComponent::IsSlideDiagnosticEnabled()
{
	return CVarShooterSlideDiagnostic.GetValueOnGameThread() > 0;
}

bool UShooterCharacterMovementComponent::ShouldTraceSlideMovement() const
{
	if (!IsSlideDiagnosticEnabled() || !CharacterOwner)
	{
		return false;
	}
	const UShooterMovementComponent* State = DiagnosticMovement.Get();
	return State && (State->IsSliding() || State->IsSprinting()
		|| GetWorld()->GetTimeSeconds() <= DiagnosticCaptureUntil);
}

void UShooterCharacterMovementComponent::LogSlideDiagnostic(const TCHAR* Event, float MoveDeltaSeconds, float MoveTimeStamp)
{
	if (!IsSlideDiagnosticEnabled() || !CharacterOwner)
	{
		return;
	}
	const bool bIsSample = FCString::Strcmp(Event, TEXT("MoveBefore")) == 0 || FCString::Strcmp(Event, TEXT("MoveAfter")) == 0
		|| FCString::Strcmp(Event, TEXT("SlideStep")) == 0 || FCString::Strcmp(Event, TEXT("ReplayMove")) == 0;
	if (bIsSample && CVarShooterSlideDiagnostic.GetValueOnGameThread() < 2)
	{
		return;
	}
	const UShooterMovementComponent* State = DiagnosticMovement.Get();
	if (!State)
	{
		return;
	}
	// 종료 로그도 Slide 해제 전에 들어온다. 즉시 종료된 경우까지 종료 후 1초간 보정을 관찰한다.
	if (State->IsSliding())
	{
		DiagnosticCaptureUntil = GetWorld()->GetTimeSeconds() + 1.0f;
	}

	// 로컬 ID는 시도 묶음이다. 프로세스 간에는 PlayerId와 이동 timestamp/실행 측을 함께 비교한다.
	if (MoveTimeStamp < 0.0f)
	{
		if (CharacterOwner->bClientUpdating)
		{
			MoveTimeStamp = DiagnosticReplayTimeStamp;
		}
		else if (const FCharacterNetworkMoveData* Move = GetCurrentNetworkMoveData())
		{
			MoveTimeStamp = Move->TimeStamp;
		}
		else if (CharacterOwner->HasAuthority() && ServerPredictionData)
		{
			MoveTimeStamp = ServerPredictionData->CurrentClientTimeStamp;
		}
		else if (ClientPredictionData)
		{
			MoveTimeStamp = ClientPredictionData->CurrentTimeStamp;
		}
	}
	const UWorld* World = GetWorld();
	const AShooterCharacter* Shooter = CastChecked<AShooterCharacter>(CharacterOwner);
	const APlayerState* PlayerState = CharacterOwner->GetPlayerState();
	const UOutlierArenaSubsystem* Arena = DiagnosticArena.Get();
	// 준비 상태는 이벤트에서만 조회한다. 레벨 표시 완료는 현재 위치의 충돌 준비 보장이 아니다.
	const int32 GameplayLevelsReady = !bIsSample && Arena ? (Arena->IsGameplayLevelsReady() ? 1 : 0) : -1;
	UE_LOG(LogShooterSlide, Log,
		TEXT("[SlideDiag] Event=%s Owner=%s PlayerId=%d World=%s NetMode=%d Role=%d Local=%d LocalSlideId=%u Epoch=%u Request=%u ProcessedCrouch=%u ProcessedCancel=%u ")
		TEXT("Frame=%llu RealTime=%.6f WorldTime=%.6f MoveTS=%.6f FrameDt=%.6f MoveDt=%.6f Replay=%d ClientUpdatePending=%d ")
		TEXT("Sprint=%d WantsSprint=%d Slide=%d WantsCrouch=%d Crouched=%d Dead=%d Reload=%d Lock=%d SuitBlocked=%d ")
		TEXT("Mode=%d CustomMode=%d Pos=%s Vel=%s Speed2D=%.3f StartSpeed=%.3f TargetSpeed=%.3f Dir=%s ")
		TEXT("SimElapsed=%.6f WorldElapsed=%.6f Duration=%.3f GameplayLevelsReady=%d FloorWalkable=%d FloorActor=%s"),
		Event, *CharacterOwner->GetPathName(), PlayerState ? PlayerState->GetPlayerId() : -1,
		*World->GetName(), static_cast<int32>(World->GetNetMode()), static_cast<int32>(CharacterOwner->GetLocalRole()),
		CharacterOwner->IsLocallyControlled(), State->SlideDiagnosticId, MovementEpoch, ActiveSlideRequest,
		ProcessedCrouchRequest, ProcessedJumpCancelRequest, static_cast<uint64>(GFrameCounter),
		FPlatformTime::Seconds(), World->GetTimeSeconds(), MoveTimeStamp, World->GetDeltaSeconds(), MoveDeltaSeconds,
		static_cast<int32>(CharacterOwner->bClientUpdating), ClientPredictionData && ClientPredictionData->bUpdatePosition,
		State->IsSprinting(), State->WantsToSprint(), State->IsSliding(), State->WantsToCrouch(), static_cast<int32>(CharacterOwner->bIsCrouched),
		Shooter->IsDead(), Shooter->IsReloading(), Shooter->IsActionLocked(), Shooter->IsSuitTransitionBlocked(),
		static_cast<int32>(MovementMode), CustomMovementMode, *CharacterOwner->GetActorLocation().ToString(), *Velocity.ToString(),
		Velocity.Size2D(), SlideStartSpeed, CurrentSlideSpeed, *SlideDirection.ToString(),
		SlideElapsedTime, bSlideSimulating ? World->GetTimeSeconds() - SlideDiagnosticStartTime : -1.0,
		State->GetSlideDiagnosticDuration(), GameplayLevelsReady,
		CurrentFloor.IsWalkableFloor(), *GetNameSafe(CurrentFloor.HitResult.GetActor()));
}
#endif

void UShooterCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
#if !UE_BUILD_SHIPPING
	if (ShouldTraceSlideMovement())
	{
		// 관찰만 한다. 이 경로에서 HandleSlideWallHit를 호출하면 기존 벽 취소 정책이 바뀐다.
		LogSlideDiagnostic(TEXT("Impact"), TimeSlice);
		UE_LOG(LogShooterSlide, Log,
			TEXT("[SlideDiag] ImpactDetail Owner=%s Frame=%llu Actor=%s Component=%s Blocking=%d Penetrating=%d HitTime=%.4f Normal=%s Delta=%s"),
			*GetPathNameSafe(CharacterOwner), static_cast<uint64>(GFrameCounter), *GetNameSafe(Hit.GetActor()),
			*GetNameSafe(Hit.GetComponent()), static_cast<int32>(Hit.bBlockingHit), static_cast<int32>(Hit.bStartPenetrating), Hit.Time,
			*Hit.ImpactNormal.ToString(), *MoveDelta.ToString());
	}
#endif
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
}

bool UShooterCharacterMovementComponent::HasCoyoteOpportunity() const
{
	const auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	return Shooter && Shooter->GetController() && !Shooter->IsDead() && !Shooter->IsSuitTransitionBlocked()
		&& Shooter->CoyoteTime > 0.0f && IsFalling() && Shooter->JumpCurrentCount == 0
		&& CoyoteState.bAvailable && CoyoteState.RemainingTime >= 0.0f;
}

void UShooterCharacterMovementComponent::ResetCoyoteTime()
{
	CoyoteState = {};
}

FShooterJumpSnapshot UShooterCharacterMovementComponent::CaptureJumpSnapshot() const
{
	FShooterJumpSnapshot Snapshot;
	Snapshot.Coyote = CoyoteState;
	Snapshot.bExternalMovementPending = bExternalMovementPending;
	if (CharacterOwner)
	{
		Snapshot.JumpCount = CharacterOwner->JumpCurrentCount;
		Snapshot.HoldTime = CharacterOwner->JumpKeyHoldTime;
		Snapshot.ForceTime = CharacterOwner->JumpForceTimeRemaining;
		Snapshot.bWasJumping = CharacterOwner->bWasJumping;
		Snapshot.bPressedJump = CharacterOwner->bPressedJump;
	}
	return Snapshot;
}

void UShooterCharacterMovementComponent::RestoreJumpSnapshot(const FShooterJumpSnapshot& Snapshot)
{
	CoyoteState = Snapshot.Coyote;
	bExternalMovementPending = Snapshot.bExternalMovementPending;
	if (CharacterOwner)
	{
		CharacterOwner->JumpCurrentCount = Snapshot.JumpCount;
		CharacterOwner->JumpCurrentCountPreJump = Snapshot.JumpCount;
		CharacterOwner->JumpKeyHoldTime = Snapshot.HoldTime;
		CharacterOwner->JumpForceTimeRemaining = Snapshot.ForceTime;
		CharacterOwner->bWasJumping = Snapshot.bWasJumping;
		CharacterOwner->bPressedJump = Snapshot.bPressedJump;
	}
}

void UShooterCharacterMovementComponent::PrepareJumpInput(float DeltaTime)
{
	(void)DeltaTime;
	if (CharacterOwner && !CharacterOwner->bClientUpdating && !bUsingNetworkAudioIds)
	{
		// 입력 판정이 SetMoveFor보다 먼저 호출된다. 키를 여기서 발급해 점프와 저장 이동이 공유한다.
		AudioMoveId = ++NextAudioMoveId;
		if (AudioMoveId == 0) AudioMoveId = ++NextAudioMoveId;
	}
	BeforeJumpInput = CaptureJumpSnapshot();
	bFallingFirstJumpAttempt = CharacterOwner && CharacterOwner->bPressedJump
		&& !CharacterOwner->bWasJumping && IsFalling() && CharacterOwner->JumpCurrentCount == 0;
	bCoyoteJumpAttempt = bFallingFirstJumpAttempt && HasCoyoteOpportunity();
	bJumpSucceeded = false;
}

void UShooterCharacterMovementComponent::FinishJumpInput()
{
	// 엔진의 낙하 선증가는 실패 때도 남는다. 거절된 첫 시도에서는 횟수와 유예를 소비하지 않는다.
	if (bFallingFirstJumpAttempt && !bJumpSucceeded && CharacterOwner)
	{
		CharacterOwner->JumpCurrentCount = BeforeJumpInput.JumpCount;
		CharacterOwner->JumpCurrentCountPreJump = BeforeJumpInput.JumpCount;
	}
	bFallingFirstJumpAttempt = false;
	bCoyoteJumpAttempt = false;
}

bool UShooterCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (bFallingFirstJumpAttempt && !bCoyoteJumpAttempt)
	{
		return false;
	}

	const int32 PreviousCount = CharacterOwner ? CharacterOwner->JumpCurrentCount : 0;
	const float CoyoteRemainingTime = CoyoteState.RemainingTime;
	if (bCoyoteJumpAttempt)
	{
		// CheckJumpInput의 낙하 보정 0→1만 되돌린다. 성공 후 엔진이 직접 0→1과 OnJumped를 처리한다.
		CharacterOwner->JumpCurrentCount = 0;
	}
	const bool bDidJump = Super::DoJump(bReplayingMoves, DeltaTime);
	if (bDidJump)
	{
		bJumpSucceeded = true;
		// 보정으로 저장된 이동을 재생할 때는 같은 점프의 진단 로그를 반복하지 않는다.
		if (bCoyoteJumpAttempt && !bReplayingMoves)
		{
			UE_LOG(LogShooterCoyote, Log,
				TEXT("[CoyoteJump] Success Owner=%s Side=%s Remaining=%.1fms"),
				*GetNameSafe(CharacterOwner), CharacterOwner->HasAuthority() ? TEXT("Server") : TEXT("ClientPrediction"),
				CoyoteRemainingTime * 1000.0f);
		}
		ResetCoyoteTime();
	}
	else if (bCoyoteJumpAttempt)
	{
		CharacterOwner->JumpCurrentCount = PreviousCount;
	}
	return bDidJump;
}

void UShooterCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	const bool bTraceMovement = ShouldTraceSlideMovement();
	if (bTraceMovement)
	{
		LogSlideDiagnostic(TEXT("MoveBefore"), DeltaSeconds);
	}
#endif
	// 입력은 이동 시작 시각에서 판정한다. 이미 열린 기회만 이번 이동의 시뮬레이션 시간만큼 진행한다.
	if (CoyoteState.bAvailable)
	{
		CoyoteState.RemainingTime -= FMath::Max(DeltaSeconds, 0.0f);
		if (CoyoteState.RemainingTime < 0.0f)
		{
			ResetCoyoteTime();
		}
	}
	Super::PerformMovement(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (bTraceMovement)
	{
		LogSlideDiagnostic(TEXT("MoveAfter"), DeltaSeconds);
	}
#endif
	// 외부 힘이 적용된 이동에서 발판을 벗어났다면 그 공중 체류 전체를 제외한다.
	if (IsMovingOnGround() && PendingImpulseToApply.IsZero() && PendingForceToApply.IsZero()
		&& PendingLaunchVelocity.IsZero())
	{
		bExternalMovementPending = false;
	}
}

bool UShooterCharacterMovementComponent::BeginSlideSimulation(UShooterMovementComponent* Movement)
{
	const auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	if (bSlideSimulating || !Shooter || !Movement || Movement->GetOwner() != Shooter
		|| CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || !Movement->CanStartSlide())
	{
		return false;
	}
	const FVector Direction = Velocity.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	SlideMovement = Movement;
	SlideDirection = Direction;
	// 실제 진입 속도에는 은신 배율이 이미 적용돼 있다. 여기서는 Slide 배율만 한 번 곱한다.
	SlideStartSpeed = Velocity.Size2D() * FMath::Max(0.0f, Shooter->SlideSpeedMultiplier);
	CurrentSlideSpeed = SlideStartSpeed;
	SlideElapsedTime = 0.0f;
	SlideSimulationDuration = Shooter->SlideDuration;
	SaveSlidePhysics();
	SlideEndReason = uint8(ESlideEndReason::Finished);
	bSlideSimulating = true;
	++SlideSimulationSerial;
	GroundFriction = BrakingDecelerationWalking = BrakingFrictionFactor = MaxAcceleration = 0.0f;
	CurrentSlideSpeed = EvaluateSlideSpeed(0.0f);
	MaxWalkSpeedCrouched = CurrentSlideSpeed;
	Velocity.X = SlideDirection.X * CurrentSlideSpeed;
	Velocity.Y = SlideDirection.Y * CurrentSlideSpeed;
	PublishSlideState();
#if !UE_BUILD_SHIPPING
	SlideDiagnosticStartTime = GetWorld()->GetTimeSeconds();
	DiagnosticMovement = Movement;
#endif
	return true;
}

float UShooterCharacterMovementComponent::EvaluateSlideSpeed(float ElapsedTime) const
{
	const auto* Shooter = CastChecked<AShooterCharacter>(CharacterOwner);
	const float Alpha = FMath::Clamp(ElapsedTime / SlideSimulationDuration, 0.0f, 1.0f);
	float Ratio;
	if (Shooter->SlideSpeedCurve)
	{
		// 끝점보다 낮게 내려갔다가 되오르는 Cubic 구간도 기존 하한과 단조 감속을 유지한다.
		Ratio = FMath::Max(Shooter->SlideSpeedCurve->GetFloatValue(Alpha),
			Shooter->SlideSpeedCurve->GetFloatValue(1.0f));
	}
	else
	{
		Ratio = Alpha < 0.7f ? FMath::Lerp(1.0f, 0.92f, Alpha / 0.7f)
			: FMath::Lerp(0.92f, 0.10f, (Alpha - 0.7f) / 0.3f);
	}
	return FMath::Min(CurrentSlideSpeed, SlideStartSpeed * FMath::Clamp(Ratio, 0.0f, 1.0f));
}

void UShooterCharacterMovementComponent::PublishSlideState()
{
	if (UShooterMovementComponent* Movement = SlideMovement.Get())
	{
		Movement->SlideDirection = SlideDirection;
		Movement->SlideStartSpeed = SlideStartSpeed;
		Movement->SlideElapsedTime = SlideElapsedTime;
	}
}

void UShooterCharacterMovementComponent::EndSlideSimulation()
{
	if (!bSlideSimulating)
	{
		return;
	}
	bSlideSimulating = false;
	GroundFriction = SavedGroundFriction;
	BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
	BrakingFrictionFactor = SavedBrakingFrictionFactor;
	MaxAcceleration = SavedMaxAcceleration;
	MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
	// 충돌로 줄어든 속도도 그대로 넘긴다. 종료/취소에서 별도 추진력이나 정지를 만들지 않는다.
	PublishSlideState();
	SlideMovement.Reset();
}

void UShooterCharacterMovementComponent::FinishSimulatedSlide()
{
	const uint32 FinishedSerial = SlideSimulationSerial;
	if (UShooterMovementComponent* Movement = SlideMovement.Get())
	{
		Movement->StopSlide(ESlideEndReason::Finished);
	}
	// 종료 알림에서 재진입한 새 Slide는 건드리지 않는다. 이전 실행이 남았을 때만 복구한다.
	if (SlideSimulationSerial == FinishedSerial)
	{
		EndSlideSimulation();
	}
}

void UShooterCharacterMovementComponent::PhysWalking(float DeltaTime, int32 Iterations)
{
	if (bSlideSimulating)
	{
		PhysSlide(DeltaTime, Iterations, MOVE_Walking);
	}
	else
	{
		Super::PhysWalking(DeltaTime, Iterations);
	}
}

void UShooterCharacterMovementComponent::PhysNavWalking(float DeltaTime, int32 Iterations)
{
	if (bSlideSimulating)
	{
		PhysSlide(DeltaTime, Iterations, MOVE_NavWalking);
	}
	else
	{
		Super::PhysNavWalking(DeltaTime, Iterations);
	}
}

void UShooterCharacterMovementComponent::PhysSlide(float DeltaTime, int32 Iterations, EMovementMode GroundMode)
{
	// Walking과 NavWalking의 기존 지면 처리를 공유하되, 각각의 엔진 물리 함수로 이동한다.
	const auto SimulateGround = [this, GroundMode](float Step, int32 StepIterations)
	{
		if (GroundMode == MOVE_NavWalking)
		{
			Super::PhysNavWalking(Step, StepIterations);
		}
		else
		{
			Super::PhysWalking(Step, StepIterations);
		}
	};
	if (DeltaTime < MIN_TICK_TIME || !HasValidData()
		|| !UpdatedComponent->IsQueryCollisionEnabled()
		|| (!CharacterOwner->GetController() && !bRunPhysicsWithNoController
			&& !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity()))
	{
		SimulateGround(DeltaTime, Iterations);
		return;
	}

	float RemainingTime = DeltaTime;
	const uint32 SimulatedSerial = SlideSimulationSerial;
	while (bSlideSimulating && SlideSimulationSerial == SimulatedSerial
		&& RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations)
	{
		const float TimeToFinish = SlideSimulationDuration - SlideElapsedTime;
		if (TimeToFinish < MIN_TICK_TIME)
		{
			FinishSimulatedSlide();
			break;
		}

		// 보통은 1/60초 이하로 계산한다. 긴 이동은 남은 반복 예산에 나눠 담되 엔진의 시간 상한을 넘지 않는다.
		// 마지막 반복에서 큰 잔여 시간을 한꺼번에 전진시키거나, 버린 시간을 다음 이동에 이월하지 않는다.
		const float StepLimit = FMath::Min(MaxSimulationTimeStep,
			FMath::Max(1.0f / 60.0f, RemainingTime / (MaxSimulationIterations - Iterations)));
		const float TimeStep = FMath::Min3(RemainingTime, StepLimit, TimeToFinish);
		if (TimeStep < MIN_TICK_TIME)
		{
			break;
		}

		// 구간 끝의 속도를 전체 구간에 적용하면 큰 Delta에서 거리가 줄어든다. 중간 시점의 속도로 이동한다.
		const float StepSpeed = EvaluateSlideSpeed(SlideElapsedTime + TimeStep * 0.5f);
		const float EndSpeed = FMath::Min(StepSpeed, EvaluateSlideSpeed(SlideElapsedTime + TimeStep));
		MaxWalkSpeedCrouched = StepSpeed;
		Velocity.X = SlideDirection.X * StepSpeed;
		Velocity.Y = SlideDirection.Y * StepSpeed;
		Acceleration = FVector::ZeroVector;

		// 각 호출은 MaxSimulationTimeStep 이하의 Walking 한 구간이다. 바닥/턱/충돌은 엔진 처리를 그대로 쓴다.
#if !UE_BUILD_SHIPPING
		const bool bTraceTravel = CVarShooterSlideDiagnostic.GetValueOnGameThread() >= 2;
		const FVector TravelStart = bTraceTravel ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
		const float TravelStartElapsed = SlideElapsedTime;
		const uint32 TravelEpoch = MovementEpoch;
		const uint32 TravelRequest = ActiveSlideRequest;
#endif
		SimulateGround(TimeStep, Iterations);
#if !UE_BUILD_SHIPPING
		if (bTraceTravel && HasValidData())
		{
			// Walking 호출 안의 이동만 잰다. 이동 응답의 위치 보정과 구분하고, 재생 이동은 별도 합산한다.
			// 낙하 콜백이 Slide를 끝내도 마지막 구간을 남긴다. RequestedDt는 Slide 시계의 소비량과 다를 수 있다.
			const FVector TravelEnd = UpdatedComponent->GetComponentLocation();
			float TravelTimeStamp = -1.0f;
			if (CharacterOwner->bClientUpdating)
			{
				TravelTimeStamp = DiagnosticReplayTimeStamp;
			}
			else if (const FCharacterNetworkMoveData* NetworkMove = GetCurrentNetworkMoveData())
			{
				TravelTimeStamp = NetworkMove->TimeStamp;
			}
			else if (CharacterOwner->HasAuthority() && ServerPredictionData)
			{
				TravelTimeStamp = ServerPredictionData->CurrentClientTimeStamp;
			}
			else if (ClientPredictionData)
			{
				TravelTimeStamp = ClientPredictionData->CurrentTimeStamp;
			}
			UE_LOG(LogShooterSlide, Log,
				TEXT("[SlideDiag] Event=SlideTravel Owner=%s Epoch=%u Request=%u EndEpoch=%u EndRequest=%u MoveTS=%.6f Replay=%d RequestedDt=%.6f StartElapsed=%.6f Distance2DCm=%.3f Distance3DCm=%.3f Start=%s End=%s Active=%d EndReason=%u"),
				*CharacterOwner->GetPathName(), TravelEpoch, TravelRequest, MovementEpoch, ActiveSlideRequest,
				TravelTimeStamp,
				int32(CharacterOwner->bClientUpdating), TimeStep, TravelStartElapsed,
				FVector::Dist2D(TravelStart, TravelEnd), FVector::Distance(TravelStart, TravelEnd),
				*TravelStart.ToString(), *TravelEnd.ToString(), int32(bSlideSimulating), uint32(SlideEndReason));
		}
#endif
		++Iterations;
		RemainingTime -= TimeStep;
		if (!HasValidData())
		{
			EndSlideSimulation();
			return;
		}
		if (!bSlideSimulating || SlideSimulationSerial != SimulatedSerial || MovementMode != GroundMode)
		{
			// 낙하 등으로 바뀌면 기존 Character 콜백이 취소한다. 남은 구간은 새 이동 모드에 넘긴다.
			// StartFalling은 방금 나눈 구간만 안다. 이후 낙하 시간도 코요테 유예에서 빼서 긴 Delta가 기회를 늘리지 않게 한다.
			if (IsFalling() && CoyoteState.bAvailable)
			{
				CoyoteState.RemainingTime -= RemainingTime;
				if (CoyoteState.RemainingTime < 0.0f)
				{
					ResetCoyoteTime();
				}
			}
			StartNewPhysics(RemainingTime, Iterations);
			return;
		}

		SlideElapsedTime = FMath::Min(SlideElapsedTime + TimeStep, SlideSimulationDuration);
		CurrentSlideSpeed = EndSpeed;
		MaxWalkSpeedCrouched = EndSpeed;
		// 엔진이 충돌로 줄인 속도를 복원하지 않고, 감속 비율만 적용해 구간 끝 속도로 맞춘다.
		if (StepSpeed > SMALL_NUMBER)
		{
			Velocity.X *= EndSpeed / StepSpeed;
			Velocity.Y *= EndSpeed / StepSpeed;
		}
		PublishSlideState();
#if !UE_BUILD_SHIPPING
		LogSlideDiagnostic(TEXT("SlideStep"), TimeStep);
#endif
		if (SlideSimulationDuration - SlideElapsedTime < MIN_TICK_TIME)
		{
			FinishSimulatedSlide();
		}
	}

	if ((!bSlideSimulating || SlideSimulationSerial != SimulatedSerial) && RemainingTime >= MIN_TICK_TIME)
	{
		StartNewPhysics(RemainingTime, Iterations);
	}
#if !UE_BUILD_SHIPPING
	else if (RemainingTime >= MIN_TICK_TIME && IsSlideDiagnosticEnabled())
	{
		UE_LOG(LogShooterSlide, Log, TEXT("[SlideDiag] Event=SlideTimeBudget Owner=%s RequestedDt=%.6f ConsumedDt=%.6f DroppedDt=%.6f Iterations=%d"),
			*GetNameSafe(CharacterOwner), DeltaTime, DeltaTime - RemainingTime, RemainingTime, Iterations);
	}
#endif
}

void UShooterCharacterMovementComponent::StartFalling(int32 Iterations, float RemainingTime, float TimeTick,
	const FVector& Delta, const FVector& SubLocation)
{
	const auto* Shooter = Cast<AShooterCharacter>(CharacterOwner);
	const bool bCanStart = IsMovingOnGround() && Shooter && Shooter->GetController()
		&& !Shooter->IsDead() && !Shooter->IsSuitTransitionBlocked() && Shooter->JumpCurrentCount == 0
		&& !bExternalMovementPending;
	if (bCanStart && Shooter->CoyoteTime > 0.0f)
	{
		// 엔진 StartFalling과 같은 거리 보간으로 이번 이동에서 실제 낙하에 쓰일 시간을 구한다.
		const float DesiredDistance = Delta.Size();
		const float ActualDistance = ProjectToGravityFloor(UpdatedComponent->GetComponentLocation() - SubLocation).Size();
		const float FallingTime = DesiredDistance < UE_KINDA_SMALL_NUMBER ? 0.0f
			: RemainingTime + TimeTick * (1.0f - FMath::Min(1.0f, ActualDistance / DesiredDistance));
		CoyoteState.RemainingTime = Shooter->CoyoteTime - FallingTime;
		CoyoteState.bAvailable = CoyoteState.RemainingTime >= 0.0f;
	}
	TGuardValue<bool> NaturalFallGuard(bStartingNaturalFall, bCanStart);
	Super::StartFalling(Iterations, RemainingTime, TimeTick, Delta, SubLocation);
	if (!IsFalling())
	{
		ResetCoyoteTime();
	}
}

void UShooterCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	if (MovementMode == MOVE_Falling && PreviousMode != MOVE_Falling && !bUsingNetworkAudioIds
		&& CharacterOwner && !CharacterOwner->bClientUpdating)
	{
		AudioAirborneId = AudioMoveId;
	}
#if !UE_BUILD_SHIPPING
	if (ShouldTraceSlideMovement())
	{
		// Character 통보가 Slide를 취소하기 전에 새 모드와 바닥 판정을 남긴다.
		LogSlideDiagnostic(TEXT("MovementModeChanged"));
	}
#endif
	if (!bStartingNaturalFall || MovementMode != MOVE_Falling)
	{
		ResetCoyoteTime();
	}
	if (IsMovingOnGround() && PendingImpulseToApply.IsZero() && PendingForceToApply.IsZero()
		&& PendingLaunchVelocity.IsZero())
	{
		bExternalMovementPending = false;
	}
	Super::OnMovementModeChanged(PreviousMode, PreviousCustomMode);
}

void UShooterCharacterMovementComponent::OnTeleported()
{
	ResetCoyoteTime();
	// 일반 위치 보정/재생은 승인된 Slide를 이어야 한다. 실제 순간이동만 수명주기를 끊는다.
	if (!IsReplayingMovement()) ResetSlideForLifecycle();
	Super::OnTeleported();
}

void UShooterCharacterMovementComponent::Launch(const FVector& LaunchVelocity)
{
	ResetCoyoteTime();
	bExternalMovementPending = true;
	Super::Launch(LaunchVelocity);
}

void UShooterCharacterMovementComponent::AddImpulse(FVector Impulse, bool bVelocityChange)
{
	if (!Impulse.IsNearlyZero())
	{
		ResetCoyoteTime();
		bExternalMovementPending = true;
	}
	Super::AddImpulse(Impulse, bVelocityChange);
}

void UShooterCharacterMovementComponent::AddForce(FVector Force)
{
	if (!Force.IsNearlyZero())
	{
		ResetCoyoteTime();
		bExternalMovementPending = true;
	}
	Super::AddForce(Force);
}

FNetworkPredictionData_Client* UShooterCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		auto* MutableThis = const_cast<UShooterCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FShooterClientPredictionData(*this);
	}
	return ClientPredictionData;
}

void UShooterCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime,
	const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	const auto* Move = static_cast<const FShooterAudioMoveData*>(GetCurrentNetworkMoveData());
	const FShooterSlideSnapshot Slide = CaptureSlideSnapshot();
	if (Move && (Move->MovementInput.Epoch != MovementEpoch || Move->bPredictedSliding != bSlideSimulating
		|| (bSlideSimulating && Move->PredictedSlideRequest != ActiveSlideRequest)
		|| Move->bPredictedSprintHeld != Slide.bSprintHeld || Move->bPredictedCrouchHeld != Slide.bCrouchHeld
		|| Move->bPredictedCrouched != Slide.bCrouched
		|| Move->PredictedEndReason != SlideEndReason))
	{
		// 위치 오차가 작아도 거절된 Slide 상태를 보정한다. 클라이언트의 진입 속도/방향은 채택하지 않는다.
		GetPredictionData_Server_Character()->bForceClientUpdate = true;
	}
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation,
		ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
	if (GetPredictionData_Server_Character()->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		// 전송은 나중에 일어날 수 있으므로 보정 대상 이동이 끝난 시점의 상태를 보관한다.
		PendingServerSnapshot = CaptureJumpSnapshot();
		PendingServerSlide = CaptureSlideSnapshot();
#if !UE_BUILD_SHIPPING
		if (ShouldTraceSlideMovement() && !ServerPredictionData->PendingAdjustment.bAckGoodMove)
		{
			LogSlideDiagnostic(TEXT("ServerCorrectionQueued"), DeltaTime, ClientTimeStamp);
		}
#endif
	}
}

void FShooterMoveResponse::ServerFillResponseData(const UCharacterMovementComponent& Movement,
	const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, PendingAdjustment);
	JumpSnapshot = CastChecked<UShooterCharacterMovementComponent>(&Movement)->PendingServerSnapshot;
	SlideSnapshot = CastChecked<UShooterCharacterMovementComponent>(&Movement)->PendingServerSlide;
}

bool FShooterMoveResponse::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, PackageMap))
	{
		return false;
	}
	if (IsCorrection())
	{
		Ar << JumpSnapshot.Coyote.RemainingTime;
		Ar << JumpSnapshot.Coyote.bAvailable;
		Ar << JumpSnapshot.JumpCount;
		Ar << JumpSnapshot.HoldTime;
		Ar << JumpSnapshot.ForceTime;
		Ar << JumpSnapshot.bWasJumping;
		Ar << JumpSnapshot.bPressedJump;
		Ar << JumpSnapshot.bExternalMovementPending;
		SlideSnapshot.Serialize(Ar);
	}
	return !Ar.IsError();
}

void UShooterCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	// 새 수명주기를 받은 뒤 도착한 이전 보정은 위치와 Slide 모두 되돌리지 않는다.
	if (Response.IsCorrection() && static_cast<int32>(
		static_cast<const FShooterMoveResponse&>(Response).SlideSnapshot.AppliedInput.Epoch - MovementEpoch) < 0) return;
	TGuardValue<bool> RestoreGuard(bRestoringMovementState, Response.IsCorrection());
#if !UE_BUILD_SHIPPING
	const bool bTraceCorrection = Response.IsCorrection() && ShouldTraceSlideMovement();
	if (bTraceCorrection)
	{
		LogSlideDiagnostic(TEXT("CorrectionBefore"), 0.0f, Response.ClientAdjustment.TimeStamp);
	}
	const FVector CorrectionStart = bTraceCorrection ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
#endif
	Super::ClientHandleMoveResponse(Response);
#if !UE_BUILD_SHIPPING
	if (bTraceCorrection)
	{
		// 수신과 적용은 다르다. 전후 위치와 후속 ReplayMove를 함께 보고 보정 적용 여부를 판단한다.
		LogSlideDiagnostic(TEXT("CorrectionAfter"), 0.0f, Response.ClientAdjustment.TimeStamp);
	}
#endif
	const auto* ClientData = GetPredictionData_Client_Character();
	if (Response.IsCorrection() && ClientData->bUpdatePosition && ClientData->LastAckedMove.IsValid()
		&& ClientData->LastAckedMove->TimeStamp == Response.ClientAdjustment.TimeStamp)
	{
		const bool bLocalPressedJump = CharacterOwner->bPressedJump;
		const auto& Snapshot = static_cast<const FShooterMoveResponse&>(Response).JumpSnapshot;
		RestoreJumpSnapshot(Snapshot);
		RestoreSlideSnapshot(static_cast<const FShooterMoveResponse&>(Response).SlideSnapshot);
		// 현재 로컬 입력은 보존하고, 이동 재생을 시작할 때만 서버의 입력 기준 상태로 바꾼다.
		CharacterOwner->bPressedJump = bLocalPressedJump;
		bCorrectedPressedJump = Snapshot.bPressedJump;
		bHasCorrectedJumpInput = true;
#if !UE_BUILD_SHIPPING
		if (bTraceCorrection)
		{
			LogSlideDiagnostic(TEXT("SlideCorrectionApplied"), 0.0f, Response.ClientAdjustment.TimeStamp);
			// 수신 횟수 대신 실제 적용된 보정만 기록한다. 이 거리를 SlideTravel에 더하면 재이동으로 오인한다.
			const FVector CorrectionEnd = CharacterOwner->GetActorLocation();
			UE_LOG(LogShooterSlide, Log,
				TEXT("[SlideDiag] Event=CorrectionTravel Owner=%s Epoch=%u Request=%u MoveTS=%.6f Distance2DCm=%.3f Distance3DCm=%.3f Start=%s End=%s"),
				*CharacterOwner->GetPathName(), MovementEpoch, ActiveSlideRequest, Response.ClientAdjustment.TimeStamp,
				FVector::Dist2D(CorrectionStart, CorrectionEnd), FVector::Distance(CorrectionStart, CorrectionEnd),
				*CorrectionStart.ToString(), *CorrectionEnd.ToString());
		}
#endif
	}
}

bool UShooterCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	if (!bHasCorrectedJumpInput || !CharacterOwner)
	{
		const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
		LifecycleReplayMoves.Reset();
		return bReplayed;
	}
	const bool bLocalPressedJump = CharacterOwner->bPressedJump;
	CharacterOwner->bPressedJump = bCorrectedPressedJump;
	bHasCorrectedJumpInput = false;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	// 중단 콜백이 재생 목록을 비웠다면 전환 전 점프 홀드도 다시 살리지 않는다.
	if (LifecycleReplayMoves.Num() > 0) CharacterOwner->StopJumping();
	else CharacterOwner->bPressedJump = bLocalPressedJump;
	LifecycleReplayMoves.Reset();
	// 엔진은 재생 전 앉기 플래그를 복원한다. 물리 입력은 재생 결과에 맞추고 실제 키 입력은 별도로 유지한다.
	if (auto* Movement = GetShooterMovement()) bWantsToCrouch = bSlideSimulating || Movement->WantsToCrouch();
	ReconcileSlidePresentation();
	return bReplayed;
}

void UShooterCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	const bool bWasPressed = CharacterOwner && CharacterOwner->bPressedJump;
	Super::UpdateFromCompressedFlags(Flags);
	if (CharacterOwner && !CharacterOwner->HasAuthority() && CharacterOwner->bClientUpdating)
	{
		// 엔진은 서버에서만 입력 전환을 복원한다. 클라이언트 재생도 놓기/재입력에 맞춰 Hold를 초기화한다.
		if (!CharacterOwner->bPressedJump)
		{
			CharacterOwner->StopJumping();
		}
		else if (!bWasPressed)
		{
			CharacterOwner->Jump();
		}
	}
}
