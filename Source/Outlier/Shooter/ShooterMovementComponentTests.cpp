#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "OutlierPlayerState.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterGroundedMovementTest,
	"Outlier.Shooter.GroundedMovementRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterGroundedMovementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete Shooter Blueprint class"), ShooterClass))
	{
		return false;
	}
	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Movement test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	if (TestNotNull(TEXT("Shooter fixture"), Shooter))
	{
		UCharacterMovementComponent* CharacterMovement = Shooter->GetCharacterMovement();
		UShooterMovementComponent* Movement = Shooter->FindComponentByClass<UShooterMovementComponent>();
		if (TestNotNull(TEXT("Shooter movement component"), Movement))
		{
			AOutlierPlayerState* SuitPlayerState = World->SpawnActor<AOutlierPlayerState>();
			if (TestNotNull(TEXT("Jump suit acquisition fixture"), SuitPlayerState))
			{
				Shooter->SetPlayerState(SuitPlayerState);
				SuitPlayerState->SetAcquiredSuit(false);
				Shooter->JumpMaxCount = 2;
				Shooter->JumpMaxHoldTime = 0.0f;
				Shooter->JumpCurrentCount = 0;
				Shooter->bWasJumping = false;
				Shooter->bPressedJump = false;
				CharacterMovement->SetMovementMode(MOVE_Walking);
				TestTrue(TEXT("PreSuit permits the ground jump"), Shooter->CanJumpInternal_Implementation());
				CharacterMovement->SetMovementMode(MOVE_Falling);
				Shooter->JumpCurrentCount = 1;
				TestFalse(TEXT("PreSuit rejects the second jump"), Shooter->CanJumpInternal_Implementation());
				// 획득/저장 복원은 같은 PlayerState 플래그를 사용한다. Partner 거리 비활성화와는 분리한다.
				SuitPlayerState->SetAcquiredSuit(true);
				TestTrue(TEXT("Acquired Suit permits the second jump"), Shooter->CanJumpInternal_Implementation());
				Shooter->bSuitDisabledByPartnerBoundary = true;
				TestTrue(TEXT("Partner boundary does not revoke acquired double jump"), Shooter->CanJumpInternal_Implementation());
				Shooter->bSuitDisabledByPartnerBoundary = false;
				Shooter->JumpCurrentCount = 2;
				TestFalse(TEXT("Suit still rejects a third jump"), Shooter->CanJumpInternal_Implementation());
				SuitPlayerState->SetAcquiredSuit(false);
				Shooter->JumpCurrentCount = 1;
				Shooter->JumpMaxHoldTime = 0.2f;
				Shooter->JumpKeyHoldTime = 0.1f;
				Shooter->bWasJumping = true;
				Shooter->bPressedJump = true;
				TestTrue(TEXT("PreSuit preserves the first jump hold"), Shooter->CanJumpInternal_Implementation());
				Shooter->bWasJumping = false;
				TestFalse(TEXT("PreSuit rejects a new jump during the hold window"), Shooter->CanJumpInternal_Implementation());
				Shooter->SetPlayerState(nullptr);
				TestFalse(TEXT("Missing PlayerState cannot enable double jump"), Shooter->CanJumpInternal_Implementation());
				Shooter->StopJumping();
				Shooter->JumpMaxHoldTime = ShooterClass->GetDefaultObject<AShooterCharacter>()->JumpMaxHoldTime;
				CharacterMovement->SetMovementMode(MOVE_Walking);
				Shooter->JumpCurrentCount = 0;
			}
			TestTrue(TEXT("Native crouched ledge departure default is enabled"),
				GetDefault<AShooterCharacter>()->GetCharacterMovement()->bCanWalkOffLedgesWhenCrouching);
			CharacterMovement->SetMovementMode(MOVE_Walking);
			CharacterMovement->Velocity = FVector(100.0f, 0.0f, 0.0f);
			TestTrue(TEXT("Grounded sprint is allowed"), Movement->CanSprint());
			Shooter->CombatState = ECombatState::Fire;
			Movement->SetSprintIntent(true);
			TestTrue(TEXT("Sprint starts on ground"), Movement->WantsToSprint());
			TestTrue(TEXT("Sprint interrupts fire state"), Shooter->GetCombatState() != ECombatState::Fire);
			CharacterMovement->SetMovementMode(MOVE_Falling);
			TestTrue(TEXT("Falling preserves Sprint toggle intent"), Movement->WantsToSprint());
			TestFalse(TEXT("Falling suspends actual sprint"), Movement->IsSprinting());
			TestFalse(TEXT("Falling sprint is rejected"), Movement->CanSprint());
			TestEqual(TEXT("Falling uses walk speed limit"), CharacterMovement->MaxWalkSpeed, Shooter->WalkSpeed);
			const float AirSpeedLimit = CharacterMovement->MaxWalkSpeed;
			Movement->SetSprintIntent(true);
			TestEqual(TEXT("Airborne sprint does not increase speed limit"), CharacterMovement->MaxWalkSpeed, AirSpeedLimit);
			Movement->RequestCrouchOrSlide();
			TestFalse(TEXT("Airborne crouch intent is rejected"), Movement->WantsToCrouch());
			TestFalse(TEXT("Airborne crouch is not queued in engine"), CharacterMovement->bWantsToCrouch != 0);

			CharacterMovement->SetMovementMode(MOVE_Walking);
			TestTrue(TEXT("Landing resumes Sprint toggle"), Movement->IsSprinting());
			TestEqual(TEXT("Landing restores sprint speed"), CharacterMovement->MaxWalkSpeed, Shooter->SprintSpeed);
			UShooterCombatComponent* Combat = Shooter->FindComponentByClass<UShooterCombatComponent>();
			if (TestNotNull(TEXT("Combat fixture"), Combat))
			{
				// 이동 잠금 검증은 몽타주 재생 조건과 분리하고, 완료 처리는 실제 경로를 사용한다.
				const auto BeginReloadFixture = [&]()
				{
					Combat->bIsReloading = true;
					Shooter->BeginActionLock(EShooterActionLock::Reload);
					Shooter->CombatState = ECombatState::Reload;
					Movement->RefreshMovementState();
				};
				BeginReloadFixture();
				TestTrue(TEXT("Reload fixture is active"), Shooter->IsReloading());
				TestFalse(TEXT("Sprint is blocked with reload lock"), Movement->CanSprint());
				Movement->RefreshMovementState();
				Combat->ResolveStateConflicts();
				TestFalse(TEXT("Reload suspends actual sprint"), Movement->IsSprinting());
				TestTrue(TEXT("Reload preserves Sprint toggle intent"), Movement->WantsToSprint());
				TestEqual(TEXT("Reload uses walk speed"), CharacterMovement->MaxWalkSpeed, Shooter->WalkSpeed);
				Combat->FinishReloadInternal();
				TestTrue(TEXT("Reload completion resumes Sprint toggle"), Movement->IsSprinting());
				TestEqual(TEXT("Reload completion restores sprint speed"), CharacterMovement->MaxWalkSpeed, Shooter->SprintSpeed);
				BeginReloadFixture();
				Movement->HandleSprintReleased();
				TestTrue(TEXT("Ordinary release during reload preserves Sprint toggle"), Movement->WantsToSprint());
				Movement->HandleSprintPressed();
				Combat->FinishReloadInternal();
				TestFalse(TEXT("Toggle OFF during reload prevents sprint resume"), Movement->IsSprinting());
				TestFalse(TEXT("Toggle OFF during reload clears intent"), Movement->WantsToSprint());

				CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
				Movement->SetSprintIntent(true);
				BeginReloadFixture();
				Movement->HandleCrouchPressed();
				TestTrue(TEXT("Reload permits crouch hold"), Movement->WantsToCrouch());
				TestTrue(TEXT("Reload crouch reaches the engine movement request"), CharacterMovement->bWantsToCrouch != 0);
				TestTrue(TEXT("Crouching does not cancel reload"), Shooter->IsReloading());
				TestEqual(TEXT("Crouching preserves the reload lock"), Shooter->GetActionLock(), EShooterActionLock::Reload);
				TestFalse(TEXT("Reload crouch cannot start Slide"), Movement->IsSliding());
				TestFalse(TEXT("Reload still rejects direct Slide entry"), Movement->CanStartSlide());
				Movement->HandleCrouchReleased();
				TestFalse(TEXT("Reload permits releasing crouch"), CharacterMovement->bWantsToCrouch != 0);
				TestTrue(TEXT("Releasing crouch does not cancel reload"), Shooter->IsReloading());
				TestEqual(TEXT("Releasing crouch preserves the reload lock"), Shooter->GetActionLock(), EShooterActionLock::Reload);
				Movement->HandleCrouchPressed();
				Combat->FinishReloadInternal();
				TestTrue(TEXT("Reload completion preserves crouch hold"), Movement->WantsToCrouch());
				TestTrue(TEXT("Reload completion keeps the engine crouch request"), CharacterMovement->bWantsToCrouch != 0);
				TestFalse(TEXT("Held crouch prevents Sprint after reload"), Movement->IsSprinting());
				Movement->HandleCrouchReleased();
				TestTrue(TEXT("Releasing crouch after reload resumes Sprint toggle"), Movement->IsSprinting());
				Shooter->BeginActionLock(EShooterActionLock::Equip);
				Movement->HandleCrouchPressed();
				TestFalse(TEXT("Equip lock still rejects crouch hold"), Movement->WantsToCrouch());
				TestFalse(TEXT("Equip lock still rejects engine crouch request"), CharacterMovement->bWantsToCrouch != 0);
				Shooter->EndActionLock(EShooterActionLock::Equip);
			}
			Movement->SetSprintIntent(false);
			TestFalse(TEXT("Toggle OFF clears sprint intent"), Movement->WantsToSprint());
			CharacterMovement->Velocity = FVector::ZeroVector;
			Movement->HandleCrouchPressed();
			TestTrue(TEXT("Crouch press sets hold intent"), Movement->WantsToCrouch());
			Movement->HandleCrouchPressed();
			TestTrue(TEXT("Repeated press does not toggle crouch off"), Movement->WantsToCrouch());
			Movement->HandleCrouchReleased();
			TestFalse(TEXT("Crouch release clears engine crouch request"), CharacterMovement->bWantsToCrouch != 0);

			Shooter->SlideDuration = 1.0f;
			Shooter->MinSlideSpeed = 200.0f;
			Shooter->SlideSpeedMultiplier = 1.2f;
			UCurveFloat* Curve = NewObject<UCurveFloat>(Shooter);
			Curve->FloatCurve.AddKey(0.0f, 0.5f);
			Curve->FloatCurve.AddKey(1.0f, 0.5f);
			Shooter->SlideSpeedCurve = Curve;
			const float GroundFriction = CharacterMovement->GroundFriction;
			const float Braking = CharacterMovement->BrakingDecelerationWalking;
			const float BrakingFactor = CharacterMovement->BrakingFrictionFactor;
			const float Acceleration = CharacterMovement->MaxAcceleration;
			const float CrouchSpeed = CharacterMovement->MaxWalkSpeedCrouched;
			CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
			Movement->SetSprintIntent(true);
			Movement->HandleCrouchPressed();
			TestTrue(TEXT("Ground sprint crouch starts slide"), Movement->IsSliding());
			TestTrue(TEXT("Slide applies entry speed, multiplier, and curve immediately"),
				FMath::IsNearlyEqual(CharacterMovement->Velocity.X, 240.0, 0.01));
			TestEqual(TEXT("Slide removes walking friction"), CharacterMovement->GroundFriction, 0.0f);
			TestEqual(TEXT("Slide removes walking braking"), CharacterMovement->BrakingDecelerationWalking, 0.0f);
			TestEqual(TEXT("Slide removes separate braking friction too"), CharacterMovement->BrakingFrictionFactor, 0.0f);
			TestEqual(TEXT("Slide blocks input acceleration"), CharacterMovement->MaxAcceleration, 0.0f);
			TestEqual(TEXT("Slide crouch speed follows curve"), CharacterMovement->MaxWalkSpeedCrouched, 240.0f);
			Movement->HandleCrouchReleased();
			TestTrue(TEXT("Release does not interrupt the slide"), Movement->IsSliding());
			Movement->StopSlide(ESlideEndReason::Finished);
			TestFalse(TEXT("Released slide finishes standing"), CharacterMovement->bWantsToCrouch != 0);
			TestTrue(TEXT("Released crouch slide resumes Sprint toggle"), Movement->IsSprinting());
			TestEqual(TEXT("Slide completion restores sprint speed"), CharacterMovement->MaxWalkSpeed, Shooter->SprintSpeed);
			TestEqual(TEXT("Slide restores friction"), CharacterMovement->GroundFriction, GroundFriction);
			TestEqual(TEXT("Slide restores braking"), CharacterMovement->BrakingDecelerationWalking, Braking);
			TestEqual(TEXT("Slide restores braking factor"), CharacterMovement->BrakingFrictionFactor, BrakingFactor);
			TestEqual(TEXT("Slide restores acceleration"), CharacterMovement->MaxAcceleration, Acceleration);
			TestEqual(TEXT("Slide restores crouch speed"), CharacterMovement->MaxWalkSpeedCrouched, CrouchSpeed);

			CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
			Movement->SetSprintIntent(true);
			Movement->HandleCrouchPressed();
			Movement->HandleCrouchReleased();
			Movement->HandleCrouchPressed();
			TestTrue(TEXT("Re-press during slide restores hold intent"), Movement->WantsToCrouch());
			Movement->StopSlide(ESlideEndReason::Finished);
			TestTrue(TEXT("Held slide finishes crouched"), CharacterMovement->bWantsToCrouch != 0);
			TestTrue(TEXT("Crouch preserves Sprint toggle intent"), Movement->WantsToSprint());
			TestFalse(TEXT("Crouch takes priority over sprint"), Movement->IsSprinting());
			Movement->HandleCrouchReleased();
			TestTrue(TEXT("Releasing crouch resumes sprint"), Movement->IsSprinting());

			CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
			Movement->SetSprintIntent(true);
			Movement->HandleCrouchPressed();
			Movement->StopSlide(ESlideEndReason::ForcedCancel);
			TestEqual(TEXT("Canceled slide restores friction"), CharacterMovement->GroundFriction, GroundFriction);
			TestEqual(TEXT("Canceled slide restores acceleration"), CharacterMovement->MaxAcceleration, Acceleration);
			TestFalse(TEXT("Forced cancel clears crouch"), CharacterMovement->bWantsToCrouch != 0);

			CharacterMovement->Velocity = FVector(100.0f, 0.0f, 0.0f);
			Movement->SetSprintIntent(true);
			Movement->HandleCrouchPressed();
			TestFalse(TEXT("Below threshold does not start slide"), Movement->IsSliding());
			TestTrue(TEXT("Rejected slide falls back to crouch"), CharacterMovement->bWantsToCrouch != 0);
			Movement->HandleCrouchReleased();
			Movement->SetSprintIntent(false);
			CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
			Movement->SetSprintIntent(true);
			Movement->HandleCrouchPressed();
			Movement->HandleSprintReleased();
			TestTrue(TEXT("Ordinary release during Slide preserves Sprint toggle"), Movement->WantsToSprint());
			Movement->HandleSprintPressed();
			Movement->HandleCrouchReleased();
			Movement->StopSlide(ESlideEndReason::Finished);
			TestFalse(TEXT("Toggle OFF during slide prevents sprint resume"), Movement->WantsToSprint());
			TestFalse(TEXT("Finished slide with Toggle OFF stays walking"), Movement->IsSprinting());
			TestEqual(TEXT("Finished slide with Toggle OFF uses walk speed"), CharacterMovement->MaxWalkSpeed, Shooter->WalkSpeed);

			UCurveFloat* DecelerationCurve = NewObject<UCurveFloat>(Shooter);
			const FVector2D Samples[] = {
				{0.0, 1.0}, {0.1, 1.0}, {0.2, 1.0}, {0.3, 0.88},
				{0.4, 0.72}, {0.5, 0.58}, {0.6, 0.49}, {0.65, 0.47},
				{0.75, 0.45}, {0.85, 0.44}, {1.0, 0.44}
			};
			for (const FVector2D& Sample : Samples)
			{
				const FKeyHandle Key = DecelerationCurve->FloatCurve.AddKey(Sample.X, Sample.Y);
				DecelerationCurve->FloatCurve.SetKeyInterpMode(Key, RCIM_Cubic);
			}
			DecelerationCurve->FloatCurve.AutoSetTangents();
			Shooter->SlideSpeedCurve = DecelerationCurve;
			CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
			Movement->SetSprintIntent(true);
			Movement->HandleCrouchPressed();
			TestTrue(TEXT("Deceleration slide fixture starts"), Movement->IsSliding());
			auto* SlideMovement = CastChecked<UShooterCharacterMovementComponent>(CharacterMovement);
			float PreviousSpeed = SlideMovement->GetCurrentSlideSpeed();
			// 여기서는 커브 규칙만 검사한다. 실제 Walking의 시간/거리/자동 종료는 Slide.Simulation에서 검증한다.
			for (int32 Step = 0; Step <= 100; ++Step)
			{
				const float Progress = Step / 100.0f;
				const float Speed = SlideMovement->EvaluateSlideSpeed(Progress * Shooter->SlideDuration);
				TestTrue(TEXT("Cubic slide curve never reaccelerates"), Speed <= PreviousSpeed + KINDA_SMALL_NUMBER);
				SlideMovement->CurrentSlideSpeed = Speed;
				PreviousSpeed = Speed;
			}
			TestTrue(TEXT("Curve finishes at 44 percent of slide start speed"),
				FMath::IsNearlyEqual(SlideMovement->GetCurrentSlideSpeed(), 400.0f * 1.2f * 0.44f, 0.01f));
			Movement->HandleCrouchReleased();
			const FVector ExitVelocity = CharacterMovement->Velocity;
			Movement->StopSlide(ESlideEndReason::Finished);
			TestTrue(TEXT("Slide exit preserves remaining momentum"), CharacterMovement->Velocity.Equals(ExitVelocity));
			Movement->SetSprintIntent(false);

			// 초기 Refresh 없이 공개 진입 API를 호출해도 Slide의 임시 속도를 기본값으로 캐시하지 않는다.
			const float DirectEntryCrouchSpeed = CharacterMovement->MaxWalkSpeedCrouched;
			Movement->BaseMaxWalkSpeedCrouched = -1.0f;
			CharacterMovement->Velocity = FVector(400.0f, 0.0f, 0.0f);
			Movement->TrySlide();
			TestTrue(TEXT("Direct slide entry starts simulation"), SlideMovement->IsSlideSimulating());
			Movement->StopSlide(ESlideEndReason::ForcedCancel);
			TestEqual(TEXT("Direct slide entry preserves normal crouch speed"),
				CharacterMovement->MaxWalkSpeedCrouched, DirectEntryCrouchSpeed);
		}
	}
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
