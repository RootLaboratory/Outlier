#if WITH_DEV_AUTOMATION_TESTS

#include "Interaction/PanelMover.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
	struct FPanelSafetyWorld
	{
		UWorld* World = nullptr;
		FPanelSafetyWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PanelSafetyTest")));
			if (World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}
		~FPanelSafetyWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
		void SyncPhysics() const
		{
			World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
			World->GetPhysicsScene()->Flush();
		}
		void PlacePlayer(ACharacter* Character, const FVector& Location, bool bFlying) const
		{
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Controller->Possess(Character);
			Character->SetActorLocation(Location);
			Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Character->GetCapsuleComponent()->SetGenerateOverlapEvents(false);
			Character->GetCharacterMovement()->SetMovementMode(bFlying ? MOVE_Flying : MOVE_Walking);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanelPlayerCarryTest,
	"Outlier.Interaction.Panel.PlayerCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanelPlayerCarryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPanelSafetyWorld Fixture;
	UWorld* World = Fixture.World;
	if (!TestNotNull(TEXT("Physics world exists"), World)) return false;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	APanelMover* Panel = World->SpawnActor<APanelMover>();
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	ATargetPoint* Target = World->SpawnActor<ATargetPoint>();
	if (!TestNotNull(TEXT("Panel exists"), Panel) || !TestNotNull(TEXT("Cube exists"), Cube)
		|| !TestNotNull(TEXT("Shooter exists"), Shooter) || !TestNotNull(TEXT("Partner exists"), Partner)
		|| !TestNotNull(TEXT("Target exists"), Target)) return false;
	Panel->Supporter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->PanelMesh->SetStaticMesh(Cube);
	Panel->PanelMesh->SetRelativeScale3D(FVector(6.0f, 6.0f, 0.2f));
	Panel->PanelMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Panel->TargetLocationActor = Target;
	Target->SetActorLocation(FVector(0.0f, 0.0f, 200.0f));
	Panel->MoveDuration = 1.0f;
	Panel->DispatchBeginPlay();
	const float ShooterZ = 12.0f + Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float PartnerZ = 12.0f + Partner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Fixture.PlacePlayer(Shooter, FVector(-90.0f, 0.0f, ShooterZ), false);
	Fixture.PlacePlayer(Partner, FVector(90.0f, 0.0f, PartnerZ), true);
	Fixture.SyncPhysics();
	TestTrue(TEXT("Both roles are supported by actual child mesh geometry"),
		Panel->FindSupport(Shooter) == Panel->PanelMesh && Panel->FindSupport(Partner) == Panel->PanelMesh);
	if (!TestTrue(TEXT("Safe move starts"), Panel->MoveToTarget())) return false;
	static_cast<AActor*>(Panel)->Tick(0.25f);
	TestTrue(TEXT("Panel advances"), FMath::IsNearlyEqual(Panel->GetActorLocation().Z, 50.0f, 0.1f));
	TestTrue(TEXT("Shooter follows once"), FMath::IsNearlyEqual(Shooter->GetActorLocation().Z, ShooterZ + 50.0f, 0.1f));
	TestTrue(TEXT("Flying Partner follows once"), FMath::IsNearlyEqual(Partner->GetActorLocation().Z, PartnerZ + 50.0f, 0.1f));
	const FVector AfterCarry = Shooter->GetActorLocation();
	Shooter->GetCharacterMovement()->UpdateBasedMovement(0.0f);
	TestTrue(TEXT("Normal movement-base update does not duplicate carry"), Shooter->GetActorLocation().Equals(AfterCarry, 0.1f));
	TestEqual(TEXT("Partner remains flying"), Partner->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Flying);
	Partner->SetActorLocation(FVector(900.0f, 0.0f, PartnerZ + 50.0f));
	Fixture.SyncPhysics();
	static_cast<AActor*>(Panel)->Tick(0.25f);
	TestTrue(TEXT("Partner can leave without being pulled back"), FMath::IsNearlyEqual(Partner->GetActorLocation().Z, PartnerZ + 50.0f));
	TestNull(TEXT("Leaving releases temporary flying base"), Partner->GetMovementBase());
	static_cast<AActor*>(Panel)->Tick(0.5f);
	TestTrue(TEXT("Final placement reaches target"), Panel->GetActorLocation().Equals(Target->GetActorLocation(), 0.1f));
	TestFalse(TEXT("Completion disables motion Tick"), Panel->IsActorTickEnabled());

	const auto ResetMotion = [&]()
	{
		Shooter->SetBase(nullptr);
		Partner->SetBase(nullptr);
		Shooter->SetActorLocation(FVector(3000.0f, 0.0f, 500.0f));
		Partner->SetActorLocation(FVector(3500.0f, 0.0f, 500.0f));
		Panel->SetActorLocation(FVector::ZeroVector);
		Panel->bIsMoved = false;
		Panel->bMoveStarted = false;
		Panel->bMoveFinished = false;
		Panel->bPaused = false;
		Panel->MoveCurve = nullptr;
	};
	ResetMotion();
	// 탑승하지 않은 비행 몸체를 상승면이 아래에서 만나도 실제 이동 경로가 밀어낸다.
	Partner->SetActorLocation(FVector(0.0f, 0.0f, PartnerZ + 60.0f));
	Fixture.SyncPhysics();
	TestTrue(TEXT("Rise toward a hovering body starts"), Panel->MoveToTarget());
	static_cast<AActor*>(Panel)->Tick(0.5f);
	TestTrue(TEXT("Rising face pushes an initially hovering Partner"),
		Partner->GetActorLocation().Z > PartnerZ + 60.0f);
	TestFalse(TEXT("Rising face never ends inside Partner"), Panel->PanelMesh->OverlapComponent(
		Partner->GetActorLocation(), Partner->GetCapsuleComponent()->GetComponentQuat(),
		Partner->GetCapsuleComponent()->GetCollisionShape(-0.1f)));

	ResetMotion();
	Panel->PanelMesh->SetRelativeScale3D(FVector(0.2f, 6.0f, 6.0f));
	Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Partner->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Fixture.SyncPhysics();
	TestTrue(TEXT("Side-contact panel starts"), Panel->MoveToTarget());
	static_cast<AActor*>(Panel)->Tick(0.5f);
	TestTrue(TEXT("Side face separates the body along its checked path"), Partner->GetActorLocation().X > 100.0f);
	TestFalse(TEXT("Side contact never ends inside the body"), Panel->PanelMesh->OverlapComponent(
		Partner->GetActorLocation(), Partner->GetCapsuleComponent()->GetComponentQuat(),
		Partner->GetCapsuleComponent()->GetCollisionShape(-0.1f)));

	ResetMotion();
	Panel->PanelMesh->SetRelativeScale3D(FVector(6.0f, 6.0f, 0.2f));
	Target->SetActorLocation(FVector(0.0f, 0.0f, -200.0f));
	Partner->SetActorLocation(FVector(0.0f, 0.0f, -PartnerZ - 60.0f));
	Fixture.SyncPhysics();
	TestTrue(TEXT("Underside-contact panel starts"), Panel->MoveToTarget());
	static_cast<AActor*>(Panel)->Tick(0.5f);
	TestTrue(TEXT("Descending underside separates the body downward"), Partner->GetActorLocation().Z < -PartnerZ - 60.0f);
	TestFalse(TEXT("Underside never ends inside the body"), Panel->PanelMesh->OverlapComponent(
		Partner->GetActorLocation(), Partner->GetCapsuleComponent()->GetComponentQuat(),
		Partner->GetCapsuleComponent()->GetCollisionShape(-0.1f)));

	ResetMotion();
	Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Panel->MoveCurve = NewObject<UCurveFloat>(Panel);
	Panel->MoveCurve->FloatCurve.AddKey(0.0f, 0.0f);
	Panel->MoveCurve->FloatCurve.AddKey(0.5f, 1.2f);
	Panel->MoveCurve->FloatCurve.AddKey(1.0f, 0.4f);
	Fixture.SyncPhysics();
	TestTrue(TEXT("Overshooting curve starts"), Panel->MoveToTarget());
	static_cast<AActor*>(Panel)->Tick(0.5f);
	TestTrue(TEXT("Authored overshoot is preserved"), Panel->GetActorLocation().X > Target->GetActorLocation().X);
	static_cast<AActor*>(Panel)->Tick(0.5f);
	static_cast<AActor*>(Panel)->Tick(0.01f);
	TestTrue(TEXT("Non-unit curve endpoint still reaches exact target safely"),
		Panel->GetActorLocation().Equals(Target->GetActorLocation(), 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanelCrushSafetyTest,
	"Outlier.Interaction.Panel.CrushAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanelCrushSafetyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPanelSafetyWorld Fixture;
	if (!TestNotNull(TEXT("Safety world exists"), Fixture.World)) return false;
	UWorld* World = Fixture.World;
	APanelMover* Panel = World->SpawnActor<APanelMover>();
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	ATargetPoint* Target = World->SpawnActor<ATargetPoint>();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Panel || !Partner || !Target || !Cube) return false;
	Panel->Supporter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->PanelMesh->SetStaticMesh(Cube);
	Panel->PanelMesh->SetRelativeScale3D(FVector(10.0f, 10.0f, 0.2f));
	Panel->PanelMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Panel->TargetLocationActor = Target;
	Panel->MoveDuration = 1.0f;
	Target->SetActorLocation(FVector(0.0f, 0.0f, 200.0f));
	Panel->DispatchBeginPlay();
	const float HalfHeight = Partner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Fixture.PlacePlayer(Partner, FVector(0.0f, 0.0f, HalfHeight + 12.0f), true);
	Fixture.SyncPhysics();
	if (!TestTrue(TEXT("Unobstructed start succeeds"), Panel->MoveToTarget())) return false;
	static_cast<AActor*>(Panel)->Tick(0.25f);
	// 넓은 천장은 국소 분리 후보도 모두 막는다. 이미 진행한 이동만 유지해야 한다.
	AActor* Ceiling = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Ceiling);
	Ceiling->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(1000.0f, 1000.0f, 10.0f));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->RegisterComponent();
	Box->SetWorldLocation(FVector(0.0f, 0.0f, Partner->GetActorLocation().Z + HalfHeight + 12.0f));
	Fixture.SyncPhysics();
	static_cast<AActor*>(Panel)->Tick(1.0f);
	TestTrue(TEXT("Crush pauses motion"), Panel->bPaused);
	TestFalse(TEXT("Crush never completes"), Panel->bMoveFinished);
	const FVector PausedLocation = Panel->GetActorLocation();
	const float PausedElapsed = Panel->MoveElapsed;
	static_cast<AActor*>(Panel)->Tick(5.0f);
	TestTrue(TEXT("Blocked time never accumulates"), FMath::IsNearlyEqual(Panel->MoveElapsed, PausedElapsed));
	TestTrue(TEXT("Final snap cannot bypass ceiling"), Panel->GetActorLocation().Equals(PausedLocation));
	Ceiling->Destroy();
	Fixture.SyncPhysics();
	static_cast<AActor*>(Panel)->Tick(0.05f);
	TestFalse(TEXT("Clearance resumes motion"), Panel->bPaused);
	TestTrue(TEXT("Resume consumes only current frame"), Panel->MoveElapsed <= PausedElapsed + 0.051f);
	Partner->SetActorLocation(FVector(900.0f, 0.0f, 500.0f));
	Fixture.SyncPhysics();
	static_cast<AActor*>(Panel)->Tick(0.5f);
	static_cast<AActor*>(Panel)->Tick(0.5f);
	TestTrue(TEXT("Safe motion eventually finishes"), Panel->bMoveFinished);

	// 복원은 경로를 따라 플레이어를 운반하지 않지만 목적지 몸체를 관통해서도 안 된다.
	Panel->SetActorLocation(FVector::ZeroVector);
	Panel->bMoveFinished = false;
	Partner->SetActorLocation(Target->GetActorLocation());
	Fixture.SyncPhysics();
	Panel->SnapToTarget();
	TestTrue(TEXT("Occupied restore waits"), Panel->bRestorePending);
	TestTrue(TEXT("Occupied restore leaves mesh at old location"), Panel->GetActorLocation().IsNearlyZero());
	Partner->SetActorLocation(FVector(900.0f, 0.0f, 500.0f));
	Fixture.SyncPhysics();
	static_cast<AActor*>(Panel)->Tick(0.01f);
	TestFalse(TEXT("Clear restore releases pending state"), Panel->bRestorePending);
	TestTrue(TEXT("Clear restore places destination"), Panel->GetActorLocation().Equals(Target->GetActorLocation()));

	Panel->SetActorLocation(FVector::ZeroVector);
	Panel->bIsMoved = false;
	Panel->bMoveStarted = false;
	Panel->bMoveFinished = false;
	Panel->MoveCurve = NewObject<UCurveFloat>(Panel);
	Panel->MoveCurve->FloatCurve.AddKey(0.0f, 0.0f);
	Panel->MoveCurve->FloatCurve.AddKey(1.0f, 0.0f);
	Partner->SetActorLocation(FVector(0.0f, 0.0f, HalfHeight + 12.0f));
	AActor* FinalCeiling = World->SpawnActor<AActor>();
	UBoxComponent* FinalBox = NewObject<UBoxComponent>(FinalCeiling);
	FinalCeiling->SetRootComponent(FinalBox);
	FinalBox->SetBoxExtent(FVector(1000.0f, 1000.0f, 10.0f));
	FinalBox->SetCollisionProfileName(TEXT("BlockAll"));
	FinalBox->RegisterComponent();
	FinalBox->SetWorldLocation(FVector(0.0f, 0.0f, 2.0f * HalfHeight + 24.0f));
	Fixture.SyncPhysics();
	TestTrue(TEXT("Flat curve accepts stationary initial phase"), Panel->MoveToTarget());
	static_cast<AActor*>(Panel)->Tick(0.5f);
	static_cast<AActor*>(Panel)->Tick(0.5f);
	const float FinalBlockedElapsed = Panel->MoveElapsed;
	static_cast<AActor*>(Panel)->Tick(0.01f);
	TestTrue(TEXT("Final correction keeps elapsed frozen across shorter frames"),
		FMath::IsNearlyEqual(Panel->MoveElapsed, FinalBlockedElapsed));
	TestTrue(TEXT("Large final correction uses the same crush check"), Panel->bPaused);
	TestFalse(TEXT("Rejected final correction never emits completion"), Panel->bMoveFinished);
	TestTrue(TEXT("Rejected final correction never tunnels to target"), Panel->GetActorLocation().IsNearlyZero());
	FinalCeiling->Destroy();
	Fixture.SyncPhysics();
	static_cast<AActor*>(Panel)->Tick(0.01f);
	TestTrue(TEXT("Final correction completes after clearance returns"), Panel->bMoveFinished);
	TestTrue(TEXT("Retried final correction reaches the exact target"),
		Panel->GetActorLocation().Equals(Target->GetActorLocation(), 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanelMotionReplicationTest,
	"Outlier.Interaction.Panel.MotionSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanelMotionReplicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPanelSafetyWorld Fixture;
	if (!TestNotNull(TEXT("Snapshot world exists"), Fixture.World)) return false;
	APanelMover* Panel = Fixture.World->SpawnActor<APanelMover>();
	if (!TestNotNull(TEXT("Snapshot receiver exists"), Panel)) return false;
	FPanelMoverMotionState Moving;
	Moving.Revision = 3;
	Moving.Target = FVector(0.0f, 0.0f, 200.0f);
	Moving.Location = FVector(0.0f, 0.0f, 50.0f);
	Moving.Elapsed = 0.25f;
	Moving.ServerTime = Fixture.World->GetTimeSeconds();
	Moving.bMoving = true;
	Panel->ApplyMotion(Moving);
	TestTrue(TEXT("Late receiver uses server progress"), Panel->GetActorLocation().Equals(Moving.Location));
	FPanelMoverMotionState Paused = Moving;
	Paused.Revision = 4;
	Paused.bPaused = true;
	Paused.Location.Z = 60.0f;
	Paused.Elapsed = 0.3f;
	Panel->ApplyMotion(Paused);
	Panel->ApplyMotion(Moving);
	TestTrue(TEXT("Stale active snapshot cannot undo pause"), Panel->bPaused);
	TestTrue(TEXT("Paused snapshot uses exact authoritative position"), Panel->GetActorLocation().Equals(Paused.Location));
	FPanelMoverMotionState Finished = Paused;
	Finished.Revision = 5;
	Finished.bMoving = false;
	Finished.bPaused = false;
	Finished.Location = Finished.Target;
	Panel->ApplyMotion(Finished);
	TestTrue(TEXT("Latest completed state places destination"), Panel->GetActorLocation().Equals(Finished.Target));
	TestFalse(TEXT("Completed snapshot has no idle Tick"), Panel->IsActorTickEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPanelLegacyMotionTest,
	"Outlier.Interaction.Panel.LegacyWithoutQueryCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPanelLegacyMotionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPanelSafetyWorld Fixture;
	if (!TestNotNull(TEXT("Legacy motion world exists"), Fixture.World)) return false;
	for (const ECollisionEnabled::Type Collision : { ECollisionEnabled::NoCollision, ECollisionEnabled::PhysicsOnly })
	{
		APanelMover* Panel = Fixture.World->SpawnActor<APanelMover>();
		ATargetPoint* Target = Fixture.World->SpawnActor<ATargetPoint>();
		if (!TestNotNull(TEXT("Legacy panel exists"), Panel)
			|| !TestNotNull(TEXT("Legacy target exists"), Target)) return false;
		Panel->Supporter->SetCollisionEnabled(Collision);
		Panel->PanelMesh->SetCollisionEnabled(Collision);
		Panel->TargetLocationActor = Target;
		Target->SetActorLocation(FVector(200.0f, 0.0f, 100.0f));
		Panel->MoveDuration = 1.0f;
		Panel->DispatchBeginPlay();
		TestTrue(TEXT("Legacy panel has no query collision"), Panel->MovingCollision.IsEmpty());
		if (!TestTrue(TEXT("Legacy move is accepted"), Panel->MoveToTarget())) return false;
		static_cast<AActor*>(Panel)->Tick(0.25f);
		TestTrue(TEXT("Legacy panel interpolates"), Panel->GetActorLocation().Equals(Target->GetActorLocation() * 0.25f, 0.1f));
		static_cast<AActor*>(Panel)->Tick(0.5f);
		static_cast<AActor*>(Panel)->Tick(0.25f);
		TestTrue(TEXT("Legacy panel reaches target"), Panel->GetActorLocation().Equals(Target->GetActorLocation(), 0.1f));
		TestTrue(TEXT("Legacy move completes"), Panel->bMoveFinished);
		TestFalse(TEXT("Completed legacy panel stops ticking"), Panel->IsActorTickEnabled());
		TestTrue(TEXT("Completion publishes final position"), Panel->ReplicatedMotion.Location.Equals(Target->GetActorLocation(), 0.1f));
		TestFalse(TEXT("Completed snapshot stops motion"), Panel->ReplicatedMotion.bMoving);
		TestFalse(TEXT("Legacy panel remains one-shot"), Panel->MoveToTarget());

		Panel->SetActorLocation(FVector::ZeroVector);
		Panel->bIsMoved = false;
		Panel->bMoveStarted = false;
		Panel->bMoveFinished = false;
		Panel->MoveDuration = 0.0f;
		TestTrue(TEXT("Instant legacy move is accepted"), Panel->MoveToTarget());
		TestTrue(TEXT("Instant legacy move reaches target"), Panel->GetActorLocation().Equals(Target->GetActorLocation()));
		TestTrue(TEXT("Instant legacy move completes"), Panel->bMoveFinished);

		Panel->SetActorLocation(FVector::ZeroVector);
		Panel->bMoveFinished = false;
		Panel->SnapToTarget();
		TestTrue(TEXT("Legacy checkpoint restore reaches target"), Panel->GetActorLocation().Equals(Target->GetActorLocation()));
		TestFalse(TEXT("Legacy checkpoint restore does not wait"), Panel->bRestorePending);
		TestTrue(TEXT("Legacy checkpoint restore completes"), Panel->bMoveFinished);
	}
	return true;
}

#endif
