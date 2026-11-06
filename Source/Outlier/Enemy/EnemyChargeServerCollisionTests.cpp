#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/SelfDestructDrone.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

namespace
{
struct FDroneCollisionServerWorld
{
	UWorld* World = nullptr;
	FDroneCollisionServerWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DroneCollisionServer")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		// Use the engine's supported editor-hosted dedicated-world mode as well as
		// standalone -server launches. Listen still creates a real server NetDriver.
		Context.RunAsDedicated = true;
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		FURL URL;
		URL.Port = 0; // An OS-assigned port; never share the running game's server port.
		World->Listen(URL);
	}
	~FDroneCollisionServerWorld()
	{
		if (!World) return;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

// Characterize the real server capsule before changing any response. In particular, a
// successful characterization can contain a reproduced penetration: the paired Block
// control determines whether the collision response caused it, independently of dt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyChargeServerCollisionTest,
	"Outlier.Enemy.SelfDestruct.ServerCollisionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FEnemyChargeServerCollisionTest::RunTest(const FString& Parameters)
{
	FDroneCollisionServerWorld Fixture;
	if (!TestNotNull(TEXT("Server world"), Fixture.World)) return false;
	if (!TestTrue(TEXT("Real dedicated server world and net driver are required"),
		Fixture.World->GetNetMode() == NM_DedicatedServer && Fixture.World->GetNetDriver() != nullptr)) return false;

	UClass* Classes[] = {
		ASelfDestructDrone::StaticClass(),
		LoadClass<ASelfDestructDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")),
		LoadClass<ASelfDestructDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion_Patrol.BP_VECDrone_Explosion_Patrol_C"))
	};
	struct FWallCase { const TCHAR* Name; ECollisionChannel Channel; bool bForceBlock; bool bDisableWall; };
	const FWallCase Cases[] = {
		{TEXT("WorldStatic"), ECC_WorldStatic, false, false},
		{TEXT("WorldDynamic"), ECC_WorldDynamic, false, false},
		{TEXT("PhysicsBody"), ECC_PhysicsBody, false, false},
		{TEXT("PhysicsBodyForceBlock"), ECC_PhysicsBody, true, false},
		{TEXT("WorldStaticNoCollision"), ECC_WorldStatic, false, true}
	};
	int32 RowCount = 0;
	int32 PhysicsBodyPenetrations = 0;
	for (UClass* Class : Classes)
	{
		if (!TestNotNull(TEXT("Production drone class"), Class)) return false;
		for (float DeltaTime : {1.0f / 60.0f, 1.0f / 15.0f, 0.25f})
		for (float Speed : {750.0f, 3000.0f})
		for (const FWallCase& Case : Cases)
		{
			const FVector Start(0, 0, 1000);
			ASelfDestructDrone* Drone = Fixture.World->SpawnActorDeferred<ASelfDestructDrone>(Class,
				FTransform(Start), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!TestNotNull(TEXT("Drone"), Drone)) return false;
			// Exercise the movement task in isolation; no parallel AI task or controller can
			// overwrite velocity. Blueprint construction and collision defaults still run.
			Drone->AutoPossessAI = EAutoPossessAI::Disabled;
			Drone->FinishSpawning(FTransform(Start));
			UCapsuleComponent* Capsule = Drone->GetCapsuleComponent();
			UCharacterMovementComponent* Movement = Drone->GetCharacterMovement();
			if (!TestNotNull(TEXT("Capsule"), Capsule) || !TestNotNull(TEXT("Movement"), Movement)) return false;
			Movement->bRunPhysicsWithNoController = true;
			if (Case.bForceBlock) Capsule->SetCollisionResponseToChannel(Case.Channel, ECR_Block);

			AActor* Wall = Fixture.World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
			Wall->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1.5f, 5000, 5000));
			Box->SetCollisionObjectType(Case.Channel);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->SetCollisionEnabled(Case.bDisableWall ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(300, 0, 1000));
			AActor* Target = Fixture.World->SpawnActor<AActor>();
			USceneComponent* TargetRoot = NewObject<USceneComponent>(Target);
			Target->SetRootComponent(TargetRoot);
			TargetRoot->RegisterComponent();
			Target->SetActorLocation(FVector(600, 0, 1000));
			Fixture.World->GetPhysicsScene()->ProcessDeferredCreatePhysicsState();
			Fixture.World->GetPhysicsScene()->Flush();

			const ECollisionResponse Response = Capsule->GetCollisionResponseToChannel(Case.Channel);
			const bool bShouldBlock = !Case.bDisableWall && Response == ECR_Block
				&& Capsule->IsQueryCollisionEnabled() && Drone->GetActorEnableCollision();
			const bool bPathClear = EnemyCharge::IsStraightPathClear(*Fixture.World, *Capsule, *Target, 5.0f);
			const FString Label = FString::Printf(TEXT("%s/%s/dt=%.4f/speed=%.0f"), *Class->GetName(), Case.Name, DeltaTime, Speed);
			TestEqual(*(Label + TEXT(" entry sweep follows the movement response")), bPathClear, !bShouldBlock);
			if (!TestTrue(*(Label + TEXT(" commit")), Drone->BeginCommittedSelfDestruct(Target, 2.0f, Speed))) return false;
			for (int32 Step = 0; Step < FMath::RoundToInt(2.0f / DeltaTime); ++Step)
			{
				Drone->UpdateCommittedSelfDestructMovement(DeltaTime, Speed);
				// Full CharacterMovement PhysFlying / swept move / slide, not a manually
				// teleported actor, NotifyHit injection, or a substituted collision capsule.
				Movement->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			}
			const float FinalX = Drone->GetActorLocation().X;
			const float Radius = Capsule->GetScaledCapsuleRadius();
			const bool bCrossed = FinalX > 301.5f + Radius;
			if (bShouldBlock)
				TestTrue(*(Label + TEXT(" server capsule stops before the wall")), FinalX <= 298.5f - Radius + 1.0f);
			else
				TestTrue(*(Label + TEXT(" ignored/disabled wall reproduces passage")), bCrossed);
			if (Case.Channel == ECC_PhysicsBody && !Case.bForceBlock && bCrossed) ++PhysicsBodyPenetrations;
			AddInfo(FString::Printf(TEXT("[DroneWallTest] Class=%s Case=%s NetMode=%d Authority=%d dt=%.4f Speed=%.0f CapsuleEnabled=%d ActorCollision=%d CapsuleObject=%d Response=%d Radius=%.2f HalfHeight=%.2f EntryClear=%d FinalX=%.2f Crossed=%d"),
				*Class->GetName(), Case.Name, int32(Fixture.World->GetNetMode()), Drone->HasAuthority(), DeltaTime, Speed,
				int32(Capsule->GetCollisionEnabled()), Drone->GetActorEnableCollision(), int32(Capsule->GetCollisionObjectType()),
				int32(Response), Radius, Capsule->GetScaledCapsuleHalfHeight(), bPathClear, FinalX, bCrossed));
			++RowCount;
			Drone->CancelCommittedSelfDestruct();
			Drone->Destroy();
			Wall->Destroy();
			Target->Destroy();
		}
	}
	TestEqual(TEXT("All class/channel/frame/speed cases executed"), RowCount, 90);
	AddInfo(FString::Printf(TEXT("[DroneWallTest] Summary Rows=%d PhysicsBodyPenetrations=%d Scope=DedicatedServerMovementAndEntrySweep_NoClientOrStateTreeScheduler"), RowCount, PhysicsBodyPenetrations));
	return true;
}

#endif
