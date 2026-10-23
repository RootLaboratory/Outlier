#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "OutlierPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterCoyoteTimeTest,
	"Outlier.Shooter.CoyoteTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterCoyoteTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete Shooter Blueprint class"), ShooterClass))
	{
		return false;
	}
	TestFalse(TEXT("Shared FirstPerson base keeps its default engine movement class"),
		GetDefault<AFirstPersonCharacter>()->GetCharacterMovement()->IsA<UShooterCharacterMovementComponent>());

	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Coyote test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	const auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AOutlierPlayerState* PlayerState = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Shooter fixture"), Shooter)
		|| !TestNotNull(TEXT("Controller fixture"), Controller)
		|| !TestNotNull(TEXT("Suit acquisition fixture"), PlayerState))
	{
		CleanupWorld();
		return false;
	}
	Controller->Possess(Shooter);
	Shooter->SetPlayerState(PlayerState);
	UShooterCharacterMovementComponent* Movement = Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
	if (!TestNotNull(TEXT("Shooter replaces the engine movement subobject"), Movement))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Shooter keeps the packed movement path required by its correction snapshot"),
		Movement->ShouldUsePackedMovementRPCs());

	const auto ResetFixture = [Shooter, Movement, PlayerState](bool bAcquiredSuit)
	{
		PlayerState->SetAcquiredSuit(bAcquiredSuit);
		Shooter->CoyoteTime = 0.1f;
		Shooter->JumpMaxCount = 2;
		Shooter->JumpMaxHoldTime = 0.0f;
		Shooter->StopJumping();
		Shooter->bWasJumping = false;
		Shooter->JumpCurrentCount = 0;
		Shooter->JumpKeyHoldTime = 0.0f;
		Shooter->bIsCrouched = false;
		Movement->SetPlaneConstraintEnabled(false);
		Movement->ClearAccumulatedForces();
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = FVector::ZeroVector;
		Movement->ResetCoyoteTime();
		Movement->bExternalMovementPending = false;
	};
	// 바닥 형상과 프레임 길이의 영향을 분리한다. 유예 이력을 주입하되 실제 점프/횟수 처리는 그대로 실행한다.
	const auto SetGraceAtElapsed = [Shooter, Movement](float Elapsed)
	{
		Movement->SetMovementMode(MOVE_Falling);
		Movement->CoyoteState.RemainingTime = Shooter->CoyoteTime - Elapsed;
		Movement->CoyoteState.bAvailable = true;
	};
	const auto PressJump = [Shooter]()
	{
		Shooter->Jump();
		Shooter->CheckJumpInput(0.0f);
	};
	const auto ReleaseJump = [Shooter]()
	{
		Shooter->StopJumping();
		Shooter->ClearJumpInput(0.0f);
	};

	for (bool bAcquiredSuit : {false, true})
	{
		const FString SuitContext = bAcquiredSuit ? TEXT("PostSuit") : TEXT("PreSuit");
		for (float Elapsed : {0.0f, 0.05f, 0.1f})
		{
			const FString Case = FString::Printf(TEXT("%s elapsed %.3f"), *SuitContext, Elapsed);
			ResetFixture(bAcquiredSuit);
			SetGraceAtElapsed(Elapsed);
			TestTrue(Case + TEXT(" has an opportunity including the exact boundary"), Movement->HasCoyoteOpportunity());
			PressJump();
			TestTrue(Case + TEXT(" actually jumps"), Shooter->bWasJumping);
			TestEqual(Case + TEXT(" counts the coyote jump as the first jump"), Shooter->JumpCurrentCount, 1);
			TestTrue(Case + TEXT(" applies upward jump velocity"), Movement->Velocity.Z > 0.0f);
			TestFalse(Case + TEXT(" consumes the opportunity"), Movement->HasCoyoteOpportunity());

			ReleaseJump();
			PressJump();
			TestEqual(Case + TEXT(" preserves the suit-specific extra jump limit"),
				Shooter->JumpCurrentCount, bAcquiredSuit ? 2 : 1);
			TestEqual(Case + TEXT(" only the acquired suit permits a second success"),
				Shooter->bWasJumping != 0, bAcquiredSuit);
			ReleaseJump();
			PressJump();
			TestFalse(Case + TEXT(" rejects a further jump"), Shooter->bWasJumping);
			TestEqual(Case + TEXT(" never adds a third count"), Shooter->JumpCurrentCount, bAcquiredSuit ? 2 : 1);
		}

		ResetFixture(bAcquiredSuit);
		SetGraceAtElapsed(0.1001f);
		TestFalse(SuitContext + TEXT(" expires beyond the boundary"), Movement->HasCoyoteOpportunity());
		PressJump();
		TestFalse(SuitContext + TEXT(" expired first jump is rejected"), Shooter->bWasJumping);
		TestEqual(SuitContext + TEXT(" rejection preserves zero successful jumps"), Shooter->JumpCurrentCount, 0);
		ReleaseJump();
		PressJump();
		TestFalse(SuitContext + TEXT(" repeated input cannot restore an expired opportunity"), Shooter->bWasJumping);
		TestEqual(SuitContext + TEXT(" repeated rejection does not consume a count"), Shooter->JumpCurrentCount, 0);
	}

	for (float DisabledDuration : {0.0f, -0.1f})
	{
		ResetFixture(true);
		SetGraceAtElapsed(0.0f);
		Shooter->CoyoteTime = DisabledDuration;
		TestFalse(TEXT("Non-positive CoyoteTime disables an existing opportunity"), Movement->HasCoyoteOpportunity());
		PressJump();
		TestFalse(TEXT("Disabled coyote cannot perform an airborne first jump"), Shooter->bWasJumping);
		TestEqual(TEXT("Disabled coyote preserves zero successful jumps"), Shooter->JumpCurrentCount, 0);
	}

	// 유예 시간은 입력 횟수가 아닌 이동 시뮬레이션으로 줄어야 한다. 정확한 만료 경계도 두 이동으로 확인한다.
	ResetFixture(false);
	SetGraceAtElapsed(0.0f);
	Movement->PerformMovement(0.05f);
	TestTrue(TEXT("First simulated movement retains half the opportunity"), Movement->HasCoyoteOpportunity());
	TestTrue(TEXT("Movement simulation advances the opportunity by its delta"),
		FMath::IsNearlyEqual(Movement->CoyoteState.RemainingTime, 0.05f));
	Movement->PerformMovement(0.05f);
	TestTrue(TEXT("Exactly one tenth simulated second remains eligible"), Movement->HasCoyoteOpportunity());
	Movement->PerformMovement(0.0001f);
	TestFalse(TEXT("Additional simulated time expires the opportunity"), Movement->HasCoyoteOpportunity());
	PressJump();
	TestFalse(TEXT("An input after simulated expiration cannot jump"), Shooter->bWasJumping);
	TestEqual(TEXT("Simulated expiration preserves zero successful jumps"), Shooter->JumpCurrentCount, 0);

	ResetFixture(false);
	SetGraceAtElapsed(0.05f);
	Shooter->bIsCrouched = true;
	PressJump();
	TestFalse(TEXT("Coyote cannot bypass crouch"), Shooter->bWasJumping);
	TestEqual(TEXT("Crouch rejection does not consume a jump"), Shooter->JumpCurrentCount, 0);
	TestTrue(TEXT("Crouch rejection preserves the unconsumed opportunity"), Movement->HasCoyoteOpportunity());
	TestEqual(TEXT("Failed input does not extend the opportunity"), Movement->CoyoteState.RemainingTime, 0.05f);

	ResetFixture(false);
	SetGraceAtElapsed(0.05f);
	Movement->SetPlaneConstraintNormal(FVector::UpVector);
	Movement->SetPlaneConstraintEnabled(true);
	PressJump();
	TestFalse(TEXT("A physically blocked DoJump remains unsuccessful"), Shooter->bWasJumping);
	TestEqual(TEXT("Failed DoJump restores the count"), Shooter->JumpCurrentCount, 0);
	TestTrue(TEXT("Failed DoJump does not consume the opportunity"), Movement->HasCoyoteOpportunity());
	TestEqual(TEXT("Failed DoJump does not extend the opportunity"), Movement->CoyoteState.RemainingTime, 0.05f);

	ResetFixture(false);
	SetGraceAtElapsed(0.05f);
	Shooter->JumpMaxHoldTime = 0.2f;
	PressJump();
	Shooter->JumpKeyHoldTime = 0.05f;
	Shooter->CheckJumpInput(0.0f);
	TestTrue(TEXT("PreSuit preserves coyote first-jump hold"), Shooter->bWasJumping);
	TestEqual(TEXT("Holding does not create a second jump"), Shooter->JumpCurrentCount, 1);
	ReleaseJump();
	PressJump();
	TestFalse(TEXT("Re-press after first-jump hold is still restricted by suit"), Shooter->bWasJumping);
	TestEqual(TEXT("Rejected hold re-press preserves the first count"), Shooter->JumpCurrentCount, 1);

	ResetFixture(false);
	PressJump();
	TestTrue(TEXT("Direct ground jump still succeeds"), Shooter->bWasJumping);
	TestEqual(TEXT("Direct ground jump counts once"), Shooter->JumpCurrentCount, 1);
	TestFalse(TEXT("Direct ground jump does not create a coyote opportunity"), Movement->HasCoyoteOpportunity());

	ResetFixture(true);
	Movement->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("Forced Falling alone does not create an opportunity"), Movement->HasCoyoteOpportunity());
	PressJump();
	TestFalse(TEXT("Forced Falling does not permit an airborne first jump"), Shooter->bWasJumping);
	TestEqual(TEXT("Forced Falling rejection preserves the count"), Shooter->JumpCurrentCount, 0);

	ResetFixture(true);
	SetGraceAtElapsed(0.05f);
	Movement->ResetCoyoteTime();
	TestFalse(TEXT("Explicit reset clears the previous opportunity"), Movement->HasCoyoteOpportunity());
	TestFalse(TEXT("Explicit reset clears a pending coyote attempt"), Movement->bCoyoteJumpAttempt);
	SetGraceAtElapsed(0.05f);
	Movement->SetMovementMode(MOVE_Walking);
	TestFalse(TEXT("Landing movement mode clears the previous opportunity"), Movement->HasCoyoteOpportunity());
	SetGraceAtElapsed(0.05f);
	Movement->SetMovementMode(MOVE_Flying);
	TestFalse(TEXT("Unsupported movement mode clears the opportunity"), Movement->HasCoyoteOpportunity());

	ResetFixture(true);
	SetGraceAtElapsed(0.05f);
	Shooter->LaunchCharacter(FVector(0.0f, 0.0f, 500.0f), false, true);
	TestFalse(TEXT("External launch invalidates the previous opportunity immediately"), Movement->HasCoyoteOpportunity());
	TestTrue(TEXT("External launch suppresses natural-fall opportunity creation"), Movement->bExternalMovementPending);

	ResetFixture(true);
	SetGraceAtElapsed(0.05f);
	Movement->AddImpulse(FVector::ZeroVector);
	TestTrue(TEXT("A zero impulse preserves the opportunity"), Movement->HasCoyoteOpportunity());
	Movement->AddImpulse(FVector(100.0f, 0.0f, 0.0f));
	TestFalse(TEXT("An external impulse invalidates the previous opportunity"), Movement->HasCoyoteOpportunity());
	TestTrue(TEXT("An external impulse suppresses natural-fall opportunity creation"), Movement->bExternalMovementPending);
	const FShooterJumpSnapshot ImpulseSnapshot = Movement->CaptureJumpSnapshot();
	ResetFixture(true);
	Movement->RestoreJumpSnapshot(ImpulseSnapshot);
	TestTrue(TEXT("Restoring a movement snapshot preserves external-impulse suppression"), Movement->bExternalMovementPending);
	TestFalse(TEXT("Restoring an impulse snapshot cannot revive grace"), Movement->CoyoteState.bAvailable);

	ResetFixture(true);
	SetGraceAtElapsed(0.05f);
	Movement->AddForce(FVector::ZeroVector);
	TestTrue(TEXT("A zero force preserves the opportunity"), Movement->HasCoyoteOpportunity());
	Movement->AddForce(FVector(100.0f, 0.0f, 0.0f));
	TestFalse(TEXT("An external force invalidates the previous opportunity"), Movement->HasCoyoteOpportunity());
	TestTrue(TEXT("An external force suppresses natural-fall opportunity creation"), Movement->bExternalMovementPending);

	ResetFixture(true);
	SetGraceAtElapsed(0.05f);
	Movement->OnTeleported();
	TestFalse(TEXT("Teleport notification clears the previous opportunity"), Movement->CoyoteState.bAvailable);

	ResetFixture(true);
	SetGraceAtElapsed(0.05f);
	Controller->UnPossess();
	Controller->Possess(Shooter);
	Shooter->SetPlayerState(PlayerState);
	TestFalse(TEXT("Regaining control in midair does not restore the old opportunity"), Movement->HasCoyoteOpportunity());

	// 네트워크 역할을 위조하지 않고 프로젝트의 SavedMove 할당 경로를 사용한다.
	// 엔진의 Client 전용 접근자는 독립 Game World에서 checkSlow에 걸릴 수 있어 가상 할당자를 직접 호출한다.
	auto* ClientData = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	if (TestNotNull(TEXT("Shooter allocates custom client prediction data"), ClientData))
	{
		ResetFixture(true);
		SetGraceAtElapsed(0.05f);
		PressJump();
		FSavedMovePtr CoyoteMove = ClientData->AllocateNewMove();
		CoyoteMove->Clear();
		CoyoteMove->SetMoveFor(Shooter, 1.0f / 60.0f, FVector::ZeroVector, *ClientData);
		TestEqual(TEXT("SavedMove retains the count before the predicted coyote jump"), CoyoteMove->JumpCurrentCount, 0);
		TestFalse(TEXT("SavedMove retains the hold state before the predicted jump"), CoyoteMove->bWasJumping);
		TestEqual(TEXT("Recording does not undo the successful predicted first jump"), Shooter->JumpCurrentCount, 1);
		TestTrue(TEXT("SavedMove records the pressed jump input"),
			(CoyoteMove->GetCompressedFlags() & FSavedMove_Character::FLAG_JumpPressed) != 0);
		TestTrue(TEXT("A coyote jump move cannot be merged across its consumption boundary"), CoyoteMove->bForceNoCombine);

		ResetFixture(true);
		SetGraceAtElapsed(0.02f);
		Shooter->CheckJumpInput(0.0f);
		FSavedMovePtr ActiveGraceMove = ClientData->AllocateNewMove();
		ActiveGraceMove->Clear();
		ActiveGraceMove->SetMoveFor(Shooter, 1.0f / 60.0f, FVector::ZeroVector, *ClientData);
		TestTrue(TEXT("An active grace move prevents combination even without jump input"), ActiveGraceMove->bForceNoCombine);
		TestFalse(TEXT("Active grace and consumed grace moves cannot combine"),
			ActiveGraceMove->CanCombineWith(CoyoteMove, Shooter, 0.125f));
		TestFalse(TEXT("Consumed grace and active grace moves cannot combine"),
			CoyoteMove->CanCombineWith(ActiveGraceMove, Shooter, 0.125f));

		ResetFixture(true);
		Movement->SetMovementMode(MOVE_Falling);
		Shooter->CheckJumpInput(0.0f);
		FSavedMovePtr FallingMove = ClientData->AllocateNewMove();
		FallingMove->Clear();
		FallingMove->SetMoveFor(Shooter, 1.0f / 60.0f, FVector::ZeroVector, *ClientData);
		TestTrue(TEXT("Falling without grace also preserves the server simulation boundary"), FallingMove->bForceNoCombine);
		TestFalse(TEXT("Falling and active grace moves cannot combine"),
			FallingMove->CanCombineWith(ActiveGraceMove, Shooter, 0.125f));

		// 서버가 거부한 첫 점프를 재생한다고 가정한다. 저장 당시의 유예가 보정된 현재 상태를 덮으면 안 된다.
		Movement->ResetCoyoteTime();
		CoyoteMove->PrepMoveFor(Shooter);
		TestFalse(TEXT("Replay cannot revive grace absent from the authoritative state"), Movement->HasCoyoteOpportunity());
		PressJump();
		TestFalse(TEXT("Replaying a rejected opportunity cannot create a first jump"), Shooter->bWasJumping);
		TestEqual(TEXT("Rejected replay preserves zero successful jumps"), Shooter->JumpCurrentCount, 0);

		// 반대로 서버가 첫 점프를 승인했다면 과거 SavedMove의 0회로 되돌리지 않고 실제 두 번째 점프를 이어간다.
		ReleaseJump();
		Shooter->JumpCurrentCount = 1;
		CoyoteMove->PrepMoveFor(Shooter);
		TestEqual(TEXT("Replay preserves the authoritative first-jump count"), Shooter->JumpCurrentCount, 1);
		PressJump();
		TestTrue(TEXT("Replay can continue with the legitimate suit second jump"), Shooter->bWasJumping);
		TestEqual(TEXT("Replayed second jump counts as two rather than a revived first jump"), Shooter->JumpCurrentCount, 2);
	}

	// 실제 보정 패킷과 같은 비트 스트림으로 왕복시켜 입력/외부 힘 억제 상태까지 함께 전달되는지 확인한다.
	for (bool bJumpConsumed : {false, true})
	{
		FShooterMoveResponse SentResponse;
		SentResponse.ClientAdjustment = FClientAdjustment();
		SentResponse.ClientAdjustment.TimeStamp = 1.25f;
		SentResponse.ClientAdjustment.MovementMode = MOVE_Falling;
		SentResponse.JumpSnapshot.Coyote.RemainingTime = bJumpConsumed ? 0.0f : 0.04f;
		SentResponse.JumpSnapshot.Coyote.bAvailable = !bJumpConsumed;
		SentResponse.JumpSnapshot.JumpCount = bJumpConsumed ? 1 : 0;
		SentResponse.JumpSnapshot.HoldTime = bJumpConsumed ? 0.025f : 0.0f;
		SentResponse.JumpSnapshot.ForceTime = bJumpConsumed ? 0.175f : 0.0f;
		SentResponse.JumpSnapshot.bWasJumping = bJumpConsumed;
		SentResponse.JumpSnapshot.bPressedJump = bJumpConsumed;
		SentResponse.JumpSnapshot.bExternalMovementPending = bJumpConsumed;
		FNetBitWriter Writer(nullptr, 2048);
		if (TestTrue(TEXT("Correction response serializes into the network bit stream"),
			SentResponse.Serialize(*Movement, Writer, nullptr)))
		{
			FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
			FShooterMoveResponse ReceivedResponse;
			TestTrue(TEXT("Correction response deserializes from the network bit stream"),
				ReceivedResponse.Serialize(*Movement, Reader, nullptr));
			TestFalse(TEXT("Correction response roundtrip has no archive errors"), Writer.IsError() || Reader.IsError());
			TestTrue(TEXT("Correction response remains a correction"), ReceivedResponse.IsCorrection());
			const FShooterJumpSnapshot& Sent = SentResponse.JumpSnapshot;
			const FShooterJumpSnapshot& Received = ReceivedResponse.JumpSnapshot;
			TestEqual(TEXT("Correction preserves remaining coyote time"), Received.Coyote.RemainingTime, Sent.Coyote.RemainingTime);
			TestEqual(TEXT("Correction preserves coyote availability"), Received.Coyote.bAvailable, Sent.Coyote.bAvailable);
			TestEqual(TEXT("Correction preserves actual jump count"), Received.JumpCount, Sent.JumpCount);
			TestEqual(TEXT("Correction preserves jump hold time"), Received.HoldTime, Sent.HoldTime);
			TestEqual(TEXT("Correction preserves remaining jump force"), Received.ForceTime, Sent.ForceTime);
			TestEqual(TEXT("Correction preserves successful jump state"), Received.bWasJumping, Sent.bWasJumping);
			TestEqual(TEXT("Correction preserves the held input baseline"), Received.bPressedJump, Sent.bPressedJump);
			TestEqual(TEXT("Correction preserves external movement suppression"), Received.bExternalMovementPending, Sent.bExternalMovementPending);
		}
	}

	Shooter->SetPlayerState(nullptr);
	CleanupWorld();
	return true;
}

#endif
