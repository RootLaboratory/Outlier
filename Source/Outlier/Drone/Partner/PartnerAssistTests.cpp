#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/Partner/PartnerMovementComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerCameraAssistInputTest,
	"Outlier.Partner.AutoFollow.InputRearm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerCameraAssistInputTest::RunTest(const FString& Parameters)
{
	FPartnerFollowInputState Client;
	FPartnerFollowInputState Server;
	TestTrue(TEXT("Initial press starts an input session"), Client.Press());
	const uint32 FirstSerial = Client.Serial;
	TestTrue(TEXT("Server accepts initial session"), Server.AcceptPress(FirstSerial));
	TestFalse(TEXT("Held Triggered cannot start a second session"), Client.Press());
	TestFalse(TEXT("Duplicate server press is ignored"), Server.AcceptPress(FirstSerial));

	// 경로 실패/상태 중단은 버튼 해제가 아니다. 늦은 성공 경로도 기능을 재개하면 안 된다.
	Client.Stop();
	Server.Stop();
	TestFalse(TEXT("Failure keeps held input latched"), Client.Press());
	TestFalse(TEXT("Server also requires release after failure"), Server.AcceptPress(FirstSerial + 1));
	TestFalse(TEXT("Late success after failure is discarded"), Client.AcceptsPath(FirstSerial));
	TestFalse(TEXT("Old release cannot clear current input"), Server.Release(FirstSerial + 1));
	TestTrue(TEXT("Client release rearms"), Client.Release(FirstSerial));
	TestTrue(TEXT("Server release rearms"), Server.Release(FirstSerial));
	TestTrue(TEXT("A new press can start again"), Client.Press());
	TestTrue(TEXT("Server accepts the new session"), Server.AcceptPress(Client.Serial));
	TestFalse(TEXT("Old path cannot overwrite new session"), Client.AcceptsPath(FirstSerial));
	TestFalse(TEXT("Old release cannot stop the new session"), Server.Release(FirstSerial));
	TestTrue(TEXT("Current path is accepted"), Client.AcceptsPath(Client.Serial));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerFollowRetryTest,
	"Outlier.Partner.AutoFollow.RetryLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerFollowRetryTest::RunTest(const FString& Parameters)
{
	FPartnerFollowRetryState Retry;
	TestFalse(TEXT("A fresh session has no failure"), Retry.IsExhausted(10.0, 3, 2.0));
	Retry.Fail(10.0, 0.25);
	TestFalse(TEXT("One blocked segment or budget does not disable assist"), Retry.IsExhausted(10.0, 3, 2.0));
	TestEqual(TEXT("Retry has a minimum delay"), Retry.NextAttemptAt, 10.25);
	Retry.Fail(10.25, 0.25);
	TestFalse(TEXT("Two failures remain retryable"), Retry.IsExhausted(10.25, 3, 2.0));
	Retry.Fail(10.5, 0.25);
	TestTrue(TEXT("Third failed attempt exhausts the configured limit"), Retry.IsExhausted(10.5, 3, 2.0));
	Retry.Reset();
	Retry.Fail(20.0, 0.25);
	TestTrue(TEXT("Time limit also ends the failure episode"), Retry.IsExhausted(22.0, 10, 2.0));
	Retry.Reset();
	TestFalse(TEXT("Real movement or reactivation clears the failure episode"), Retry.IsExhausted(30.0, 3, 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerAssistNavigationTest,
	"Outlier.Partner.AutoFollow.Navigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerAssistNavigationTest::RunTest(const FString& Parameters)
{
	// PIE/에셋 없이 실제 Partner 캡슐과 Component의 선정/실패/추종 흐름을 검사한다.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerAssistTest")));
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	AActor* Wall = World->SpawnActor<AActor>();
	UPartnerMovementComponent* Move = Partner ? Partner->FindComponentByClass<UPartnerMovementComponent>() : nullptr;
	if (!TestNotNull(TEXT("Partner movement exists"), Move) || !TestNotNull(TEXT("Wall exists"), Wall)
		|| !TestNotNull(TEXT("Physics scene exists"), World->GetPhysicsScene()))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	Move->RefreshCharacterRefsFromPlayerState();
	Move->FollowMaxSearchNodes = 4096;
	Move->FollowMaxCollisionChecks = 20000;
	Move->FollowMaxSearchMilliseconds = 10000.0f;
	Move->FollowPathMargin = 400.0f;
	Move->FollowGoalSearchRadius = 0.0f;
	UCapsuleComponent* Capsule = Partner->GetCapsuleComponent();
	Capsule->SetCapsuleSize(20.0f, 40.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Partner->SetActorLocation(FVector(-300.0, 0.0, 0.0));
	const FVector Goal(300.0, 0.0, 0.0);

	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(30.0, 150.0, 500.0));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->RegisterComponent();
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();

	Move->ApplyCameraAssist();
	const FFlightPathRequest Request = Move->MakeFollowPathRequest(Goal);
	TestFalse(TEXT("Partner request never restricts RoomTag"), Request.bRequireRoom);
	Move->FindFollowPath(0.0, Goal);
	TestEqual(TEXT("Open space uses a direct path"), Move->FollowPath.Num(), 2);
	Move->FollowStoredPath(0.016f);
	TestFalse(TEXT("A route produces normal CharacterMovement input"), Partner->ConsumeMovementInputVector().IsNearlyZero());

	// 선정 이후 생긴 장애물은 저장 경로를 취소하고 지연된 재탐색으로 연결한다.
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
	World->GetPhysicsScene()->Flush();
	Move->UpdateFollowPath(0.1, Goal);
	TestTrue(TEXT("Dynamic obstruction clears the old route"), Move->FollowPath.IsEmpty());
	TestEqual(TEXT("Dynamic obstruction starts one retry"), Move->FollowRetry.Failures, 1);
	Move->UpdateFollowPath(0.2, Goal);
	TestTrue(TEXT("Retry does not run before the delay"), Move->FollowPath.IsEmpty());
	Move->UpdateFollowPath(0.4, Goal);
	TestTrue(TEXT("Pawn-blocking wall is detoured despite ignoring Visibility"), Move->FollowPath.Num() > 2);
	for (int32 Index = 1; Index < Move->FollowPath.Num(); ++Index)
	{
		FFlightPathRequest Segment = Move->MakeFollowPathRequest(Move->FollowPath[Index]);
		Segment.Start = Move->FollowPath[Index - 1];
		TestEqual(TEXT("Every stored segment clears the actual capsule"),
			FlightPathfinding::CheckSegment(*World, *Capsule, Segment), EFlightPathStatus::Success);
	}

	// 예산 실패와 경로 없음은 서로 다른 결과이며, 둘 다 단일 실패로 즉시 종료하지 않는다.
	Move->FollowMaxCollisionChecks = 1;
	TestEqual(TEXT("Low budget reports budget rather than unreachable"),
		FlightPathfinding::FindPath(*World, *Capsule, Move->MakeFollowPathRequest(Goal)).Status,
		EFlightPathStatus::BudgetExceeded);
	Move->FollowMaxCollisionChecks = 20000;
	Box->SetBoxExtent(FVector(30.0, 1000.0, 1000.0));
	World->GetPhysicsScene()->Flush();
	TestEqual(TEXT("Sealed route reports no path within bounds"),
		FlightPathfinding::FindPath(*World, *Capsule, Move->MakeFollowPathRequest(Goal)).Status,
		EFlightPathStatus::NoPathWithinBounds);
	Move->FollowRetry.Reset();
	Move->FindFollowPath(1.0, Goal);
	TestEqual(TEXT("First failure leaves assist active"), Partner->MoveMode, EPartnerMoveMode::CameraAssist);
	Move->UpdateFollowPath(1.3, Goal);
	Move->UpdateFollowPath(1.6, Goal);
	Move->UpdateFollowPath(1.9, Goal);
	TestEqual(TEXT("Exhaustion returns to manual movement"), Partner->MoveMode, EPartnerMoveMode::Normal);
	Move->ApplyCameraAssist();
	TestEqual(TEXT("Held input cannot restart assist"), Partner->MoveMode, EPartnerMoveMode::Normal);
	Move->ReleaseCameraAssistInput();
	Move->ApplyCameraAssist();
	TestEqual(TEXT("Release and press allow a fresh attempt"), Partner->MoveMode, EPartnerMoveMode::CameraAssist);

	// 막힌 추종 오프셋은 제한된 근접 목적지로 보정한다. 장애물 자체를 무시하지 않는다.
	Box->SetBoxExtent(FVector(40.0));
	Wall->SetActorLocation(Goal);
	World->GetPhysicsScene()->Flush();
	Move->FollowGoalSearchRadius = 150.0f;
	const FFlightPathResult Adjusted = FlightPathfinding::FindPath(*World, *Capsule, Move->MakeFollowPathRequest(Goal));
	TestTrue(TEXT("Occupied goal has a validated nearby replacement"),
		Adjusted.Status == EFlightPathStatus::Success && Adjusted.bGoalAdjusted);
	if (!Adjusted.Waypoints.IsEmpty())
	{
		TestTrue(TEXT("Replacement remains within the configured radius"),
			FVector::Distance(Goal, Adjusted.Waypoints.Last()) <= 150.01);
	}
	Move->FindFollowPath(2.0, Goal);
	if (!Move->FollowPath.IsEmpty())
	{
		Partner->SetActorLocation(Move->FollowPath.Last());
		Move->FollowWaypoint = Move->FollowPath.Num() - 1;
		Move->FollowStoredPath(0.016f);
		TestTrue(TEXT("Arrival stops input and velocity"), Partner->GetCharacterMovement()->Velocity.IsNearlyZero());
		Move->UpdateFollowPath(3.0, Goal);
		TestTrue(TEXT("Arrival at adjusted goal does not repeatedly replan"), Move->FollowPath.IsEmpty());
		Move->UpdateFollowPath(3.3, Goal + FVector(200.0, 0.0, 0.0));
		TestFalse(TEXT("Meaningful target movement creates a fresh route"), Move->FollowPath.IsEmpty());
	}
	Move->ResetAutoFollowForOwnerChange();
	TestTrue(TEXT("Owner change clears route and held input"), Move->FollowPath.IsEmpty() && !Move->CameraAssistInput.bHeld);

	// SyncMove도 목표 상대 위치/속도 설정만 다르고 우회와 실패 후 재입력 정책은 같다.
	Partner->SetActorLocation(FVector(-300.0, 0.0, 0.0));
	Box->SetBoxExtent(FVector(30.0, 150.0, 500.0));
	Wall->SetActorLocation(FVector::ZeroVector);
	World->GetPhysicsScene()->Flush();
	Move->SetSyncMove(true);
	TestEqual(TEXT("SyncMove enters its own follow mode"), Partner->MoveMode, EPartnerMoveMode::SyncMove);
	TestEqual(TEXT("SyncMove retains its original acceptance radius"), Move->GetFollowAcceptanceRadius(), 25.0f);
	Move->FindFollowPath(4.0, Goal);
	TestTrue(TEXT("SyncMove uses the same capsule detour"), Move->FollowPath.Num() > 2);
	Box->SetBoxExtent(FVector(30.0, 1000.0, 1000.0));
	World->GetPhysicsScene()->Flush();
	Move->FollowRetry.Reset();
	Move->FindFollowPath(5.0, Goal);
	Move->UpdateFollowPath(5.3, Goal);
	Move->UpdateFollowPath(5.6, Goal);
	Move->UpdateFollowPath(5.9, Goal);
	TestEqual(TEXT("Blocked SyncMove returns to manual mode"), Partner->MoveMode, EPartnerMoveMode::Normal);
	TestTrue(TEXT("Blocked SyncMove clears velocity"), Partner->GetCharacterMovement()->Velocity.IsNearlyZero());
	Move->SetSyncMove(true);
	TestEqual(TEXT("Held SyncMove cannot automatically restart"), Partner->MoveMode, EPartnerMoveMode::Normal);
	Move->SetSyncMove(false);
	Move->SetSyncMove(true);
	TestEqual(TEXT("Released SyncMove can be enabled again"), Partner->MoveMode, EPartnerMoveMode::SyncMove);
	Move->SetSyncMove(false);
	Move->ApplyCameraAssist();
	const uint32 OldCameraSerial = Move->CameraAssistInput.Serial;
	Move->SetSyncMove(true);
	const TArray<FVector> StaleCameraPath = { FVector(-300.0, 0.0, 0.0), Goal };
	Move->ClientReceiveFollowPath_Implementation(EPartnerMoveMode::CameraAssist, OldCameraSerial, StaleCameraPath, true);
	TestEqual(TEXT("Old CameraAssist stop cannot cancel a newer SyncMove"), Partner->MoveMode, EPartnerMoveMode::SyncMove);
	Move->ReleaseCameraAssistInput();
	TestEqual(TEXT("Releasing the inactive CameraAssist button does not stop SyncMove"),
		Partner->MoveMode, EPartnerMoveMode::SyncMove);
	Move->ResetAutoFollowForOwnerChange();

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
