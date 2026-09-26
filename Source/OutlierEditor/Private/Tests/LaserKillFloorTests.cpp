#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/LaserKillFloorTestPawn.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Environment/LaserKillFloor.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"

ALaserKillFloorTestPawn::ALaserKillFloorTestPawn()
{
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->SetSphereRadius(30.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
}

float ALaserKillFloorTestPawn::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	++DamageCount;
	LastDamageRequest = Request;
	return Request.DamageAmount;
}

namespace
{
struct FScopedLaserKillFloorTestWorld
{
	UWorld* World = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Laser floor test world is created"), World))
		{
			GEngine->DestroyWorldContext(World);
			return false;
		}

		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		FURL TestURL;
		TestURL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		if (!Test.TestTrue(TEXT("Test world has server authority"), World->SetGameMode(TestURL)))
		{
			return false;
		}
		World->InitializeActorsForPlay(TestURL);
		World->BeginPlay();
		return true;
	}

	~FScopedLaserKillFloorTestWorld()
	{
		if (!World)
		{
			return;
		}

		if (World->HasBegunPlay())
		{
			World->BeginTearingDown();
			World->EndPlay(EEndPlayReason::LevelTransition);
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierLaserKillFloorOverlapTest,
	"Outlier.Environment.LaserKillFloor.OverlapDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLaserKillFloorOverlapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedLaserKillFloorTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}

	UWorld* World = TestWorld.World;
	ALaserKillFloor* Floor = World->SpawnActor<ALaserKillFloor>();
	ALaserKillFloorTestPawn* Pawn = World->SpawnActor<ALaserKillFloorTestPawn>(
		ALaserKillFloorTestPawn::StaticClass(), FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Laser floor spawns"), Floor)
		|| !TestNotNull(TEXT("Damage receiver Pawn spawns"), Pawn))
	{
		return false;
	}
	if (!Floor->HasActorBegunPlay())
	{
		Floor->DispatchBeginPlay();
	}
	if (!Pawn->HasActorBegunPlay())
	{
		Pawn->DispatchBeginPlay();
	}

	UBoxComponent* KillVolume = Cast<UBoxComponent>(Floor->GetRootComponent());
	UStaticMeshComponent* LaserPlane = Cast<UStaticMeshComponent>(
		Floor->GetDefaultSubobjectByName(TEXT("LaserPlane")));
	if (!TestNotNull(TEXT("Kill volume exists"), KillVolume)
		|| !TestNotNull(TEXT("Visual plane exists"), LaserPlane))
	{
		return false;
	}

	TestTrue(TEXT("Floor is replicated"), Floor->GetIsReplicated());
	TestTrue(TEXT("Hazard starts enabled"), Floor->IsHazardEnabled());
	TestEqual(TEXT("Volume overlaps Pawns"), KillVolume->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	TestEqual(TEXT("Volume does not block WorldStatic"), KillVolume->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Ignore);
	TestEqual(TEXT("Visual plane has no collision"), LaserPlane->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Visual plane produces no overlaps"), LaserPlane->GetGenerateOverlapEvents());
	TestEqual(TEXT("Pawn starts outside the floor"), Pawn->DamageCount, 0);
	auto MovePawn = [Pawn, KillVolume](const FVector& Location)
	{
		Pawn->SetActorLocation(Location);
		CastChecked<USphereComponent>(Pawn->GetRootComponent())->UpdateOverlaps();
		KillVolume->UpdateOverlaps();
	};

	MovePawn(FVector::ZeroVector);
	TestTrue(TEXT("Moved Pawn overlaps the active kill volume"), KillVolume->IsOverlappingActor(Pawn));
	TestEqual(TEXT("Entering active floor applies damage once"), Pawn->DamageCount, 1);
	if (Pawn->DamageCount > 0)
	{
		TestEqual(TEXT("Damage uses the environment tag"), Pawn->LastDamageRequest.DamageTag,
			OutlierGameplayTags::Damage::Environment());
		TestEqual(TEXT("Damage uses the floor as causer"), Pawn->LastDamageRequest.DamageCauser,
			static_cast<AActor*>(Floor));
		TestTrue(TEXT("Damage is immediately lethal in ordinary vitality ranges"),
			Pawn->LastDamageRequest.DamageAmount >= 1000000.0f);
	}

	Floor->SetHazardEnabled(false);
	TestFalse(TEXT("Floor disables"), Floor->IsHazardEnabled());
	TestEqual(TEXT("Disabled volume has no collision"), KillVolume->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	MovePawn(FVector(1000.0f, 0.0f, 0.0f));
	MovePawn(FVector::ZeroVector);
	TestEqual(TEXT("Disabled floor does not damage on entry"), Pawn->DamageCount, 1);

	Floor->SetHazardEnabled(true);
	TestTrue(TEXT("Floor re-enables"), Floor->IsHazardEnabled());
	TestEqual(TEXT("Enabled volume restores overlap queries"), KillVolume->GetCollisionEnabled(),
		ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Re-enabling damages a Pawn already inside"), Pawn->DamageCount, 2);
	Floor->SetHazardEnabled(true);
	TestEqual(TEXT("Setting the same state does not damage again"), Pawn->DamageCount, 2);
	MovePawn(FVector(1000.0f, 0.0f, 0.0f));
	MovePawn(FVector::ZeroVector);
	TestTrue(TEXT("Re-entered Pawn overlaps the active kill volume"), KillVolume->IsOverlappingActor(Pawn));
	TestEqual(TEXT("Leaving and entering active floor damages again"), Pawn->DamageCount, 3);
	return true;
}

#endif
