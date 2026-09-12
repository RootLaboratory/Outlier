#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/SuitInteractionTestActors.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"

namespace
{
struct FScopedSuitInteractionTestWorld
{
	UWorld* World = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Transient Suit interaction world is created"), World))
		{
			return false;
		}

		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		if (!Test.TestTrue(TEXT("Transient Suit world creates an authority game mode"), World->SetGameMode(FURL())))
		{
			return false;
		}
		World->InitializeActorsForPlay(FURL());
		return true;
	}

	void Shutdown()
	{
		if (!World)
		{
			return;
		}

		if (World->AreActorsInitialized())
		{
			for (AActor* Actor : FActorRange(World))
			{
				if (Actor)
				{
					Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
				}
			}
		}

		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		if (World->IsRooted())
		{
			World->RemoveFromRoot();
		}
		World = nullptr;
	}

	~FScopedSuitInteractionTestWorld()
	{
		Shutdown();
	}
};

template <typename TObjectType>
TObjectType* ReadObjectProperty(const UObject* Object, FName PropertyName)
{
	const FObjectProperty* Property = Object
		? FindFProperty<FObjectProperty>(Object->GetClass(), PropertyName)
		: nullptr;
	return Property
		? Cast<TObjectType>(Property->GetObjectPropertyValue_InContainer(Object))
		: nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierSuitInteractionEquipTest,
	"Outlier.Interaction.Suit.Equip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSuitInteractionEquipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FScopedSuitInteractionTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}

	UWorld* World = TestWorld.World;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	APartnerCharacter* Partner = PartnerClass
		? World->SpawnActor<APartnerCharacter>(PartnerClass)
		: nullptr;
	UStaticMesh* SuitDisplayAsset = NewObject<UStaticMesh>(GetTransientPackage());
	USkeletalMesh* FirstPersonSuitMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	USkeletalMesh* ThirdPersonSuitMesh = NewObject<USkeletalMesh>(GetTransientPackage());

	ASuitInteractionTestActor* Suit = World->SpawnActorDeferred<ASuitInteractionTestActor>(
		ASuitInteractionTestActor::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Suit)
	{
		Suit->Configure(SuitDisplayAsset, FirstPersonSuitMesh, ThirdPersonSuitMesh);
		Suit->FinishSpawning(FTransform::Identity);
	}

	if (!TestNotNull(TEXT("Shooter Blueprint spawns"), Shooter)
		|| !TestNotNull(TEXT("Partner spawns"), Partner)
		|| !TestNotNull(TEXT("Suit interaction spawns"), Suit))
	{
		return false;
	}
	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}
	if (!Partner->HasActorBegunPlay())
	{
		Partner->DispatchBeginPlay();
	}
	if (!Suit->HasActorBegunPlay())
	{
		Suit->DispatchBeginPlay();
	}

	Shooter->SetPartnerCharacter(Partner);
	Partner->SetShooterCharacter(Shooter);

	UStaticMeshComponent* SuitDisplayMesh = ReadObjectProperty<UStaticMeshComponent>(Suit, TEXT("SuitDisplayMesh"));
	AWeaponBase* StoredShooterRifle = ReadObjectProperty<AWeaponBase>(Suit, TEXT("StoredShooterRifle"));
	ARangedWeaponBase* StoredPartnerWeapon = ReadObjectProperty<ARangedWeaponBase>(Suit, TEXT("StoredPartnerWeapon"));
	TestNotNull(TEXT("Suit owns a world display Static Mesh"), SuitDisplayMesh);
	if (SuitDisplayMesh)
	{
		TestEqual(
			TEXT("Suit display mesh blocks the interaction trace channel"),
			SuitDisplayMesh->GetCollisionResponseToChannel(ECC_Visibility),
			ECR_Block);
	}
	TestNotNull(TEXT("Suit pre-spawns the Shooter Rifle"), StoredShooterRifle);
	TestNotNull(TEXT("Suit pre-spawns the Partner weapon"), StoredPartnerWeapon);
	if (StoredShooterRifle && StoredPartnerWeapon)
	{
		TestTrue(TEXT("Stored Shooter Rifle is hidden"), StoredShooterRifle->IsHidden());
		TestFalse(TEXT("Stored Shooter Rifle collision is disabled"), StoredShooterRifle->GetActorEnableCollision());
		TestFalse(
			TEXT("Stored Shooter Rifle does not cast a hidden third-person shadow"),
			StoredShooterRifle->GetThirdPersonWeaponMesh()->bCastHiddenShadow);
		TestTrue(TEXT("Stored Partner weapon is hidden"), StoredPartnerWeapon->IsHidden());
		TestFalse(TEXT("Stored Partner weapon collision is disabled"), StoredPartnerWeapon->GetActorEnableCollision());
		TestFalse(
			TEXT("Stored Partner weapon does not cast a hidden third-person shadow"),
			StoredPartnerWeapon->GetThirdPersonWeaponMesh()->bCastHiddenShadow);
	}

	TestFalse(TEXT("Partner cannot activate the Suit interaction"), Suit->Interact(Partner));
	TestTrue(TEXT("Shooter completes the Suit interaction immediately"), Suit->Interact(Shooter));

	TestEqual(
		TEXT("Shooter first-person mesh changes"),
		Shooter->GetFirstPersonMesh()->GetSkeletalMeshAsset(),
		FirstPersonSuitMesh);
	TestEqual(
		TEXT("Shooter third-person mesh changes"),
		Shooter->GetMesh()->GetSkeletalMeshAsset(),
		ThirdPersonSuitMesh);
	TestEqual(
		TEXT("Shooter shadow mesh follows the Suit mesh"),
		Shooter->GetShadowMesh()->GetSkeletalMeshAsset(),
		ThirdPersonSuitMesh);
	TestEqual(TEXT("Shooter equips the pre-spawned Rifle"), Shooter->GetCurrentWeapon(), StoredShooterRifle);
	TestEqual(
		TEXT("Shooter stores the Suit Rifle in the Primary slot"),
		Shooter->GetInventoryComponent()->GetWeaponInSlot(EWeaponSlot::Primary),
		StoredShooterRifle);
	TestTrue(
		TEXT("Partner equips the pre-spawned ranged weapon"),
		Partner->GetCurrentWeapon() == StoredPartnerWeapon);
	TestTrue(TEXT("Partner equipped weapon can attack"), StoredPartnerWeapon && StoredPartnerWeapon->CanAttack());
	TestTrue(TEXT("Consumed Suit is hidden immediately"), Suit->IsHidden());
	TestFalse(TEXT("Consumed Suit collision is disabled immediately"), Suit->GetActorEnableCollision());
	TestFalse(TEXT("Consumed Suit rejects another interaction"), Suit->Interact(Shooter));

	return true;
}

#endif
