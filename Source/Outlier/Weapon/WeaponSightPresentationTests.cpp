#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierWeaponSightPresentationTest,
	"Outlier.Weapon.Sight.PresentationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWeaponSightPresentationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient sight presentation world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* WeaponClass = LoadClass<ARangedWeaponBase>(
		nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	ARangedWeaponBase* Weapon = WeaponClass
		? World->SpawnActor<ARangedWeaponBase>(WeaponClass)
		: nullptr;

	if (!TestNotNull(TEXT("Shooter is spawned"), Shooter)
		|| !TestNotNull(TEXT("Pistol is spawned"), Weapon))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	UStaticMeshComponent* FirstSight = Weapon->GetFirstSightMesh();
	TestNotNull(TEXT("Pistol has a first-person sight component"), FirstSight);
	if (FirstSight)
	{
		TestTrue(TEXT("Sight is hidden while the weapon is a pickup"), FirstSight->bHiddenInGame);
	}

	Weapon->OnEquipped(Shooter);
	Weapon->ShowEquippedPresentation();
	if (FirstSight)
	{
		TestFalse(TEXT("Current weapon sight is shown after equip presentation"), FirstSight->bHiddenInGame);
	}

	Weapon->OnUnequipped();
	if (FirstSight)
	{
		TestTrue(TEXT("Previous weapon sight is hidden after unequip"), FirstSight->bHiddenInGame);
	}

	Weapon->OnEquipped(Shooter);
	Weapon->ShowEquippedPresentation();
	Weapon->OnDropped(FTransform::Identity, Shooter);
	if (FirstSight)
	{
		TestTrue(TEXT("Sight is hidden again after dropping the weapon"), FirstSight->bHiddenInGame);
	}

	Weapon->Destroy(true);
	Shooter->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

#endif
