#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerInputConfig.h"
#include "Drone/Partner/PartnerMovementComponent.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "InputAction.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerMovementRecoveryTest,
	"Outlier.Partner.Movement.SuitAndInputRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerMovementRecoveryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerMovementRecovery")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	APartnerPlayerController* Controller = World->SpawnActor<APartnerPlayerController>();
	if (!TestNotNull(TEXT("Partner exists"), Partner)
		|| !TestNotNull(TEXT("Controller exists"), Controller)) return false;
	UPartnerMovementComponent* Flight = Partner->MovementComponent;
	UCharacterMovementComponent* Movement = Partner->GetCharacterMovement();
	if (!TestNotNull(TEXT("Flight component exists"), Flight)) return false;
	Flight->RefreshCharacterRefsFromPlayerState();
	// Possess의 무기/세이브 초기화 없이 실제 로컬 Controller 경계만 구성한다.
	Partner->Controller = Controller;
	Controller->SetPawn(Partner);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);

	Movement->SetMovementMode(MOVE_Flying);
	const FGuid Id = FGuid::NewGuid();
	TestTrue(TEXT("Suit acquires the movement stop"), Partner->AcquireSuitTransitionBlock(Id));
	TestTrue(TEXT("Actual teleport succeeds"), Partner->TeleportTo(FVector(0, 0, 1000), FRotator::ZeroRotator, false, true));
	TestEqual(TEXT("Teleport retains the suit stop"), EMovementMode(Movement->MovementMode), MOVE_None);
	Partner->ReleaseSuitTransitionBlock(FGuid::NewGuid());
	TestTrue(TEXT("Foreign ID cannot release the block"), Partner->IsSuitTransitionBlocked());
	Partner->ReleaseSuitTransitionBlock(Id);
	TestEqual(TEXT("Teleport did not overwrite the original Flying mode"), EMovementMode(Movement->MovementMode), MOVE_Flying);

	// 이미 Falling에 남은 이전 경로도 종료 시 복구한다. 별도 전환 ID의 재진입과 구분한다.
	const FGuid FallingId = FGuid::NewGuid();
	Partner->AcquireSuitTransitionBlock(FallingId);
	Movement->SetMovementMode(MOVE_Falling);
	Partner->ReleaseSuitTransitionBlock(FallingId);
	TestEqual(TEXT("Legacy Falling residue recovers Flying"), EMovementMode(Movement->MovementMode), MOVE_Flying);

	const FGuid FailedId = FGuid::NewGuid();
	Partner->AcquireSuitTransitionBlock(FailedId);
	Movement->SetMovementMode(MOVE_Falling);
	// 정적 Root는 엔진의 명시적인 순간이동 실패 조건이다. 실패해도 차단을 유지해야 한다.
	Partner->GetCapsuleComponent()->SetMobility(EComponentMobility::Static);
	TestFalse(TEXT("Non-movable root rejects teleport"), Partner->TeleportTo(FVector(0, 0, 2000), FRotator::ZeroRotator));
	Partner->GetCapsuleComponent()->SetMobility(EComponentMobility::Movable);
	TestEqual(TEXT("Failed teleport also reapplies the stop"), EMovementMode(Movement->MovementMode), MOVE_None);
	Partner->ReleaseSuitTransitionBlock(FailedId);
	TestEqual(TEXT("Failed teleport preserves Flying recovery"), EMovementMode(Movement->MovementMode), MOVE_Flying);

	Movement->DisableMovement();
	const FGuid IndependentId = FGuid::NewGuid();
	Partner->AcquireSuitTransitionBlock(IndependentId);
	Partner->ReleaseSuitTransitionBlock(IndependentId);
	TestEqual(TEXT("An independent initial stop is not released"), EMovementMode(Movement->MovementMode), MOVE_None);

	for (FGameplayTag Tag : {OutlierGameplayTags::State::Dead(), OutlierGameplayTags::State::Rebooting()})
	{
		Movement->SetMovementMode(MOVE_Flying);
		const FGuid BlockedId = FGuid::NewGuid();
		Partner->AcquireSuitTransitionBlock(BlockedId);
		Movement->SetMovementMode(MOVE_Falling);
		Partner->GetOutlierAbilitySystemComponent()->AddLooseGameplayTag(Tag);
		Partner->ReleaseSuitTransitionBlock(BlockedId);
		TestEqual(TEXT("Dead/Reboot cannot resume Flying on unlock"), EMovementMode(Movement->MovementMode), MOVE_Falling);
		Partner->GetOutlierAbilitySystemComponent()->RemoveLooseGameplayTag(Tag);
	}
	Movement->SetMovementMode(MOVE_Flying);

	AFirstPersonCharacter* Other = World->SpawnActor<AFirstPersonCharacter>(FVector(3000, 0, 1000), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Non-Partner character exists"), Other)) return false;
	Other->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	const FGuid OtherId = FGuid::NewGuid();
	Other->AcquireSuitTransitionBlock(OtherId);
	Other->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Other->ReleaseSuitTransitionBlock(OtherId);
	TestEqual(TEXT("Non-Partner Falling is not overwritten"),
		EMovementMode(Other->GetCharacterMovement()->MovementMode), MOVE_Falling);

	const FVector Momentum(400, -200, 150);
	for (EPartnerMoveMode Mode : {EPartnerMoveMode::FreeMove, EPartnerMoveMode::CameraAssist, EPartnerMoveMode::SyncMove})
	{
		// 경로와 Held가 남은 상태를 직접 구성해 초기화의 중첩 정리 경로를 검사한다.
		Partner->MoveMode = Mode;
		Flight->FollowMode = Mode;
		Flight->bFollowWasActive = Mode != EPartnerMoveMode::FreeMove;
		Flight->CameraAssistInput.Press();
		Flight->SyncMoveInput.Press();
		Flight->FollowPath = {FVector::ZeroVector, FVector(1000, 0, 0)};
		Flight->CurrentMoveInput = FVector2D(1, 1);
		Flight->VerticalInput = 1.0f;
		Partner->AddMovementInput(FVector::ForwardVector, 1.0f, true);
		Movement->Velocity = Momentum;
		Partner->ResetMovementInput();
		TestEqual(TEXT("Input reset preserves external momentum"), Movement->Velocity, Momentum);
		TestTrue(TEXT("Horizontal input cleared"), Flight->CurrentMoveInput.IsNearlyZero());
		TestEqual(TEXT("Vertical input cleared"), Flight->VerticalInput, 0.0f);
		TestTrue(TEXT("Pending input consumed"), Partner->GetPendingMovementInputVector().IsNearlyZero());
		TestTrue(TEXT("Follow path cleared"), Flight->FollowPath.IsEmpty());
		TestFalse(TEXT("Camera assist hold released"), Flight->CameraAssistInput.bHeld);
		TestFalse(TEXT("Sync move hold released"), Flight->SyncMoveInput.bHeld);
		TestEqual(TEXT("Free/follow mode returns to Normal"), Partner->MoveMode, EPartnerMoveMode::Normal);
	}

	// 실제 네트워크 운송 대신 서버 수신 함수를 호출해 serial과 속도 정책을 각각 검사한다.
	Flight->CameraAssistInput.Press();
	const uint32 Serial = Flight->CameraAssistInput.Serial;
	Partner->MoveMode = Flight->FollowMode = EPartnerMoveMode::CameraAssist;
	Flight->bFollowWasActive = true;
	Flight->FollowPath = {FVector::ZeroVector, FVector(1000, 0, 0)};
	Movement->Velocity = Momentum;
	Flight->ServerSetAutoFollowInput_Implementation(EPartnerMoveMode::CameraAssist, false, Serial - 1, true);
	TestTrue(TEXT("Stale reset cannot release a newer held input"), Flight->CameraAssistInput.bHeld);
	Flight->ServerSetAutoFollowInput_Implementation(EPartnerMoveMode::CameraAssist, false, Serial, true);
	TestFalse(TEXT("Matching reset releases the server input"), Flight->CameraAssistInput.bHeld);
	TestEqual(TEXT("Server reset preserves momentum"), Movement->Velocity, Momentum);
	Flight->CameraAssistInput.Press();
	Partner->MoveMode = Flight->FollowMode = EPartnerMoveMode::CameraAssist;
	Flight->bFollowWasActive = true;
	Movement->Velocity = Momentum;
	Flight->ServerSetAutoFollowInput_Implementation(EPartnerMoveMode::CameraAssist, false, Flight->CameraAssistInput.Serial, false);
	TestTrue(TEXT("Ordinary follow release retains its immediate stop"), Movement->Velocity.IsNearlyZero());

	// 실제 Setup 바인딩을 실행한다. 캐시에 비영 값을 넣어 종료/취소가 정리하는지 확인한다.
	UPartnerInputConfig* Config = NewObject<UPartnerInputConfig>(Partner);
	Config->MoveAction = NewObject<UInputAction>(Config);
	Config->MoveAction->ValueType = EInputActionValueType::Axis2D;
	Config->LookAction = NewObject<UInputAction>(Config);
	Config->AttackAction = NewObject<UInputAction>(Config);
	Config->InteractionAction = NewObject<UInputAction>(Config);
	Config->AreaOfEffectAction = NewObject<UInputAction>(Config);
	Config->CameraAssistAction = NewObject<UInputAction>(Config);
	Config->HackingAction = NewObject<UInputAction>(Config);
	Config->ScanAction = NewObject<UInputAction>(Config);
	Config->ShieldAction = NewObject<UInputAction>(Config);
	Config->SyncMoveAction = NewObject<UInputAction>(Config);
	Config->AccelerateAction = NewObject<UInputAction>(Config);
	Config->FreeMoveAction = NewObject<UInputAction>(Config);
	Config->VerticalMoveAction = NewObject<UInputAction>(Config);
	Partner->InputConfig = Config;
	UEnhancedInputComponent* Input = NewObject<UEnhancedInputComponent>(Partner);
	Partner->SetupPlayerInputComponent(Input);
	for (const UInputAction* Action : {Config->MoveAction.Get(), Config->VerticalMoveAction, Config->FreeMoveAction})
	{
		for (ETriggerEvent Event : {ETriggerEvent::Completed, ETriggerEvent::Canceled})
		{
			Flight->CurrentMoveInput = FVector2D(1, 1);
			Flight->VerticalInput = 1.0f;
			Partner->ApplyMoveMode(EPartnerMoveMode::FreeMove);
			bool bExecuted = false;
			for (const auto& Binding : Input->GetActionEventBindings())
			{
				if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
				{
					Binding->Execute(FInputActionInstance(Action));
					bExecuted = true;
				}
			}
			TestTrue(TEXT("Each movement action binds Completed and Canceled"), bExecuted);
			if (Action == Config->MoveAction) TestTrue(TEXT("Move end clears direction"), Flight->CurrentMoveInput.IsNearlyZero());
			if (Action == Config->VerticalMoveAction) TestEqual(TEXT("Vertical end clears input"), Flight->VerticalInput, 0.0f);
			if (Action == Config->FreeMoveAction) TestEqual(TEXT("Free move end clears mode"), Partner->MoveMode, EPartnerMoveMode::Normal);
		}
	}
	Flight->CurrentMoveInput = FVector2D(1, 0);
	Flight->VerticalInput = 1.0f;
	Movement->Velocity = Momentum;
	Controller->FlushPressedKeys();
	TestTrue(TEXT("Focus flush clears cached direction"), Flight->CurrentMoveInput.IsNearlyZero());
	TestEqual(TEXT("Focus flush clears vertical input"), Flight->VerticalInput, 0.0f);
	TestEqual(TEXT("Focus flush preserves momentum"), Movement->Velocity, Momentum);
	return true;
}

#endif
