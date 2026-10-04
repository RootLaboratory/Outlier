#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
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
	UClass* PistolClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	FClassProperty* DefaultClassProperty = FindFProperty<FClassProperty>(
		AShooterCharacter::StaticClass(), TEXT("DefaultMeleeWeaponClass"));
	if (!TestNotNull(TEXT("Shooter BP"), ShooterClass)
		|| !TestNotNull(TEXT("Hammer BP"), HammerClass)
		|| !TestNotNull(TEXT("Rifle BP"), RifleClass)
		|| !TestNotNull(TEXT("Pistol BP"), PistolClass)
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

	// Possess 전 원격 표현과 Possess 후 소유자 표현을 분리한다. 실제 화면/복제 검증은 PIE에서 수행한다.
	USkeletalMeshComponent* FirstMesh = Hammer->GetFirstPersonWeaponMesh();
	USkeletalMeshComponent* ThirdMesh = Hammer->GetThirdPersonWeaponMesh();
	USkeletalMeshComponent* ShadowMesh = Hammer->GetShadowWeaponMesh();
	if (!TestNotNull(TEXT("Hammer FP mesh"), FirstMesh)
		|| !TestNotNull(TEXT("Hammer TP mesh"), ThirdMesh)
		|| !TestNotNull(TEXT("Hammer shadow mesh"), ShadowMesh))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(TEXT("Remote hammer casts its TP shadow"), ThirdMesh->CastShadow);
	TestFalse(TEXT("Remote hammer does not duplicate the owner shadow"), ShadowMesh->CastShadow);

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Local inventory controller"), Controller))
	{
		CleanupWorld();
		return false;
	}
	Controller->Possess(Shooter);
	TestTrue(TEXT("Presentation fixture is locally controlled"), Shooter->IsLocallyControlled());
	Hammer->ShowEquippedPresentation();

	auto CheckHammerPresentation = [this, Shooter, Hammer, FirstMesh, ThirdMesh, ShadowMesh](bool bEquipped)
	{
		TestTrue(TEXT("Hammer FP attaches to the FP character mesh"),
			FirstMesh->GetAttachParent() == Shooter->GetFirstPersonMesh());
		TestTrue(TEXT("Hammer TP attaches to the TP character mesh"), ThirdMesh->GetAttachParent() == Shooter->GetMesh());
		TestTrue(TEXT("Hammer shadow attaches to the shadow character mesh"), ShadowMesh->GetAttachParent() == Shooter->GetShadowMesh());
		TestTrue(TEXT("Hammer FP uses the configured Melee socket"),
			FirstMesh->GetAttachSocketName() == Shooter->GetFirstPersonWeaponSocketByType(EWeaponType::Melee));
		TestTrue(TEXT("Hammer TP uses the configured Melee socket"),
			ThirdMesh->GetAttachSocketName() == Shooter->GetThirdPersonWeaponSocketByType(EWeaponType::Melee));
		TestTrue(TEXT("Hammer shadow uses the TP Melee socket"), ShadowMesh->GetAttachSocketName() == ThirdMesh->GetAttachSocketName());
		TestTrue(TEXT("Hammer FP socket exists on the character"),
			Shooter->GetFirstPersonMesh()->DoesSocketExist(FirstMesh->GetAttachSocketName()));
		TestTrue(TEXT("Hammer TP socket exists on the character"),
			Shooter->GetMesh()->DoesSocketExist(ThirdMesh->GetAttachSocketName()));
		TestNotNull(TEXT("Hammer FP has a skeletal mesh asset"), FirstMesh->GetSkeletalMeshAsset());
		TestNotNull(TEXT("Hammer TP has a skeletal mesh asset"), ThirdMesh->GetSkeletalMeshAsset());
		TestNotNull(TEXT("Hammer shadow has a skeletal mesh asset"), ShadowMesh->GetSkeletalMeshAsset());
		TestTrue(TEXT("Hammer FP is owner-only"), FirstMesh->bOnlyOwnerSee);
		TestTrue(TEXT("Hammer TP is hidden from its owner"), ThirdMesh->bOwnerNoSee);
		TestTrue(TEXT("Hammer FP rendering remains FirstPerson"),
			FirstMesh->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson);
		TestTrue(TEXT("Hammer FP visibility matches the current slot"), FirstMesh->bHiddenInGame == !bEquipped);
		TestTrue(TEXT("Hammer TP visibility matches the current slot"), ThirdMesh->bHiddenInGame == !bEquipped);
		TestTrue(TEXT("Owner shadow mesh remains hidden"), ShadowMesh->bHiddenInGame);
		TestFalse(TEXT("Owner FP mesh never casts a shadow"), FirstMesh->CastShadow);
		TestFalse(TEXT("Owner TP mesh does not duplicate the shadow"), ThirdMesh->CastShadow);
		TestTrue(TEXT("Owner hammer shadow matches the current slot"), ShadowMesh->CastShadow == bEquipped);
		TestTrue(TEXT("Owner hidden shadow matches the current slot"), ShadowMesh->bCastHiddenShadow == bEquipped);
		TestFalse(TEXT("Owned hammer is not a world pickup"), Hammer->CanBePickedUpBy(Shooter));
	};
	CheckHammerPresentation(true);

	AWeaponBase* RiflePickup = World->SpawnActor<AWeaponBase>(RifleClass);
	AWeaponBase* PistolPickup = World->SpawnActor<AWeaponBase>(PistolClass);
	AWeaponBase* SuitRifle = AWeaponBase::SpawnLoadoutWeapon(World, RifleClass, Shooter);
	AWeaponBase* ReplacementRifle = AWeaponBase::SpawnLoadoutWeapon(World, RifleClass, Shooter);
	if (!TestNotNull(TEXT("Rifle pickup"), RiflePickup)
		|| !TestNotNull(TEXT("Pistol pickup"), PistolPickup)
		|| !TestNotNull(TEXT("Suit rifle"), SuitRifle)
		|| !TestNotNull(TEXT("Replacement suit rifle"), ReplacementRifle))
	{
		CleanupWorld();
		return false;
	}
	// 일시 월드에는 자동 BeginPlay가 없을 수 있다. 장착 전 타입 검사도 실제 DataTable 초기화 후 수행한다.
	for (AWeaponBase* Weapon : { RiflePickup, PistolPickup, SuitRifle, ReplacementRifle })
	{
		if (!Weapon->HasActorBegunPlay())
		{
			Weapon->DispatchBeginPlay();
		}
	}
	TestTrue(TEXT("Rifle pickup succeeds alongside the default hammer"), RiflePickup->Interact(Shooter));
	AWeaponBase* GrantedRifle = Inventory->GetWeaponInSlot(EWeaponSlot::Primary);
	TestTrue(TEXT("Rifle pickup selects Primary"), GrantedRifle && Shooter->GetCurrentWeapon() == GrantedRifle);
	TestTrue(TEXT("Rifle pickup starts ordinary equip presentation"), Shooter->GetActionLock() == EShooterActionLock::Equip);
	CheckHammerPresentation(false);
	// 상태 전이를 검사하는 동기 테스트이므로 몽타주 잠금 시간은 건너뛴다. 실제 재생 시간은 PIE 검증 대상이다.
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TestTrue(TEXT("Pistol pickup succeeds alongside the default hammer"), PistolPickup->Interact(Shooter));
	AWeaponBase* GrantedPistol = Inventory->GetWeaponInSlot(EWeaponSlot::Secondary);
	TestTrue(TEXT("Pistol occupies Secondary"), GrantedPistol && GrantedPistol == Shooter->GetCurrentWeapon());
	Shooter->EndActionLock(EShooterActionLock::Equip);

	auto SwitchToSlot = [this, Shooter, Inventory, Hammer, &CheckHammerPresentation](EWeaponSlot Slot)
	{
		AWeaponBase* PreviousWeapon = Shooter->GetCurrentWeapon();
		AWeaponBase* TargetWeapon = Inventory->GetWeaponInSlot(Slot);
		Inventory->SelectWeaponSlot(Slot);
		TestTrue(TEXT("Ordinary switch starts an equip lock"), Shooter->GetActionLock() == EShooterActionLock::Equip);
		TestTrue(TEXT("Lower phase keeps the previous weapon current"), Shooter->GetCurrentWeapon() == PreviousWeapon);
		// 로컬 Lower 완료 -> 서버 슬롯 교체 -> 로컬 Raise의 기존 경로를 호출한다. SwitchId를 추측하지 않는다.
		Shooter->UpdateLocalProceduralWeaponSwitch(FMath::Max(Shooter->GetFirstPersonSwitchLowerDuration(), 0.05f) + 0.01f);
		TestTrue(TEXT("Lower confirmation selects the requested weapon"), Shooter->GetCurrentWeapon() == TargetWeapon);
		TestTrue(TEXT("Raise phase retains the equip lock"), Shooter->GetActionLock() == EShooterActionLock::Equip);
		Shooter->UpdateLocalProceduralWeaponSwitch(0.01f);
		Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
		Shooter->EndActionLock(EShooterActionLock::Equip);
		TestTrue(TEXT("Switch retains the original hammer actor"), Inventory->GetWeaponInSlot(EWeaponSlot::Melee) == Hammer);
		CheckHammerPresentation(Slot == EWeaponSlot::Melee);
	};
	SwitchToSlot(EWeaponSlot::Melee);
	SwitchToSlot(EWeaponSlot::Primary);
	SwitchToSlot(EWeaponSlot::Secondary);
	SwitchToSlot(EWeaponSlot::Melee);

	TestTrue(TEXT("Suit rifle is granted alongside the default hammer"), Inventory->EquipSuitRifle(SuitRifle));
	TestTrue(TEXT("Suit grant makes Rifle current"), Shooter->GetCurrentWeapon() == SuitRifle);
	if (TestNotNull(TEXT("Previously granted rifle"), GrantedRifle))
	{
		TestTrue(TEXT("Suit grant cleans the previous Primary only"), GrantedRifle->IsActorBeingDestroyed());
	}
	TestTrue(TEXT("Suit grant preserves the original Secondary pistol"),
		GrantedPistol && Inventory->GetWeaponInSlot(EWeaponSlot::Secondary) == GrantedPistol);
	CheckHammerPresentation(false);
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TestTrue(TEXT("Suit rifle replacement succeeds"), Inventory->EquipSuitRifle(ReplacementRifle));
	TestTrue(TEXT("Previous suit rifle is cleaned up"), SuitRifle->IsActorBeingDestroyed());
	TestTrue(TEXT("Suit replacement preserves the original hammer"), Inventory->GetWeaponInSlot(EWeaponSlot::Melee) == Hammer);
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TestFalse(TEXT("Hammer replacement is also rejected while Rifle is current"), Pickup->Interact(Shooter));
	TestTrue(TEXT("Rejected Melee pickup does not change the current Rifle"), Shooter->GetCurrentWeapon() == ReplacementRifle);
	TestFalse(TEXT("Rejected Melee pickup does not start an action lock"), Shooter->IsActionLocked());
	TestFalse(TEXT("Rejected Melee pickup is still not consumed"), Pickup->IsActorBeingDestroyed());
	TestTrue(TEXT("Rejected Melee pickup stays available after suit grant"), Pickup->CanBePickedUpBy(Shooter));
	SwitchToSlot(EWeaponSlot::Melee);
	Controller->UnPossess();

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
