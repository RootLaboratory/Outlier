#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"
#include "Weapon/WeaponBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterDefaultMeleeTest,
	"Outlier.Shooter.Inventory.DefaultMelee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterDefaultMeleeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* HammerClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
	UClass* RifleClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	FClassProperty* DefaultClassProperty = FindFProperty<FClassProperty>(
		AShooterCharacter::StaticClass(), TEXT("DefaultMeleeWeaponClass"));
	if (!TestNotNull(TEXT("Shooter BP"), ShooterClass)
		|| !TestNotNull(TEXT("Hammer BP"), HammerClass)
		|| !TestNotNull(TEXT("Rifle BP"), RifleClass)
		|| !TestNotNull(TEXT("BP default melee setting"), DefaultClassProperty))
	{
		return false;
	}

	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Inventory test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	auto CleanupWorld = [World]()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	AOutlierPlayerState* PlayerState = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Shooter"), Shooter) || !TestNotNull(TEXT("PlayerState"), PlayerState))
	{
		CleanupWorld();
		return false;
	}
	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}
	PlayerState->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PlayerState->SetShooterCharacter(Shooter);
	UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent();
	if (!TestNotNull(TEXT("Inventory"), Inventory))
	{
		CleanupWorld();
		return false;
	}

	DefaultClassProperty->SetPropertyValue_InContainer(Shooter, nullptr);
	TestFalse(TEXT("Missing class is rejected"), Inventory->InitializeDefaultMeleeWeapon(PlayerState));
	DefaultClassProperty->SetPropertyValue_InContainer(Shooter, RifleClass);
	TestFalse(TEXT("Non-melee class is rejected"), Inventory->InitializeDefaultMeleeWeapon(PlayerState));
	TestNull(TEXT("Rejected settings leave current weapon empty"), Shooter->GetCurrentWeapon());
	TestTrue(TEXT("Rejected settings leave the snapshot empty"), PlayerState->GetLoadoutSnapshot().IsEmpty());

	DefaultClassProperty->SetPropertyValue_InContainer(Shooter, HammerClass);
	TestTrue(TEXT("Server grants a hammer before Possess"), Inventory->InitializeDefaultMeleeWeapon(PlayerState));
	AWeaponBase* Hammer = Inventory->GetWeaponInSlot(EWeaponSlot::Melee);
	if (!TestNotNull(TEXT("Granted hammer"), Hammer))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Hammer is immediately current"), Shooter->GetCurrentWeapon() == Hammer);
	TestTrue(TEXT("Initial snapshot records Melee"), PlayerState->GetLoadoutSnapshot().CurrentSlot == EWeaponSlot::Melee);
	TestTrue(TEXT("Initial snapshot records the hammer class"),
		PlayerState->GetLoadoutSnapshot().SlotSnapshots[static_cast<int32>(EWeaponSlot::Melee)].WeaponClass.Get() == HammerClass);
	TestTrue(TEXT("Repeated initialization succeeds without granting again"), Inventory->InitializeDefaultMeleeWeapon(PlayerState));
	TestTrue(TEXT("Repeated initialization preserves the actor"), Inventory->GetWeaponInSlot(EWeaponSlot::Melee) == Hammer);
	TestFalse(TEXT("Initial grant does not start an action lock"), Shooter->IsActionLocked());

	AWeaponBase* Pickup = World->SpawnActor<AWeaponBase>(HammerClass);
	if (!TestNotNull(TEXT("Replacement pickup"), Pickup))
	{
		CleanupWorld();
		return false;
	}
	TestFalse(TEXT("Protected hammer replacement is not a successful interaction"), Pickup->Interact(Shooter));
	TestFalse(TEXT("Rejected pickup remains in the world"), Pickup->IsActorBeingDestroyed());
	TestTrue(TEXT("Rejected pickup remains available"), Pickup->CanBePickedUpBy(Shooter));
	TestTrue(TEXT("Original hammer stays current"), Shooter->GetCurrentWeapon() == Hammer);

	FOutlierLoadoutSnapshot Saved = PlayerState->GetLoadoutSnapshot();
	Saved.SlotSnapshots[static_cast<int32>(EWeaponSlot::Primary)].WeaponClass = RifleClass;
	Saved.CurrentSlot = EWeaponSlot::Primary;
	Inventory->RestoreLoadout(Saved);
	TestTrue(TEXT("Restoration cleans the original hammer"), Hammer->IsActorBeingDestroyed());
	AWeaponBase* RestoredHammer = Inventory->GetWeaponInSlot(EWeaponSlot::Melee);
	if (!TestNotNull(TEXT("Restored hammer"), RestoredHammer)
		|| !TestNotNull(TEXT("Restored rifle"), Inventory->GetWeaponInSlot(EWeaponSlot::Primary)))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Restoration preserves the saved current slot"),
		Shooter->GetCurrentWeapon() == Inventory->GetWeaponInSlot(EWeaponSlot::Primary));
	TestTrue(TEXT("Repeated initialization does not force Melee after restore"), Inventory->InitializeDefaultMeleeWeapon(PlayerState));
	TestTrue(TEXT("Rifle stays current after repeated initialization"),
		Shooter->GetCurrentWeapon() == Inventory->GetWeaponInSlot(EWeaponSlot::Primary));
	Inventory->HandleEquipWeapon(Pickup);
	TestTrue(TEXT("Restored default hammer is protected"), Inventory->GetWeaponInSlot(EWeaponSlot::Melee) == RestoredHammer);
	Inventory->RestoreLoadout(Saved);
	if (TestNotNull(TEXT("First restored hammer"), RestoredHammer))
	{
		TestTrue(TEXT("Repeated restore cleans the previous hammer"), RestoredHammer->IsActorBeingDestroyed());
	}

	// 이전 저장의 빈 Melee 슬롯은 별도 호환 정책 승인 전까지 그대로 둔다.
	Saved.SlotSnapshots[static_cast<int32>(EWeaponSlot::Melee)].WeaponClass = nullptr;
	Inventory->RestoreLoadout(Saved);
	PlayerState->SetLoadoutSnapshot(Saved);
	TestFalse(TEXT("Existing saved loadout is not overwritten by an initial grant"), Inventory->InitializeDefaultMeleeWeapon(PlayerState));
	TestNull(TEXT("Old save is not silently supplemented"), Inventory->GetWeaponInSlot(EWeaponSlot::Melee));

	CleanupWorld();
	return true;
}

#endif
