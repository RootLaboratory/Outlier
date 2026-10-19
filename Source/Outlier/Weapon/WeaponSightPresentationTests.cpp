#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierWeaponFirstPersonRenderingDefaultsTest,
	"Outlier.Weapon.FirstPerson.RenderingDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWeaponFirstPersonRenderingDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ARangedWeaponBase* Weapon = GetMutableDefault<ARangedWeaponBase>();
	TestTrue(TEXT("Native FP weapon uses first-person rendering"),
		Weapon->GetFirstPersonWeaponMesh()->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson);
	TestTrue(TEXT("Native FP sight uses first-person rendering"),
		Weapon->GetFirstSightMesh()->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson);
	const FName ComponentNames[] = { TEXT("FirstHandMagazine"), TEXT("ThirdHandMagazine"), TEXT("ShadowHandMagazine"), TEXT("ThirdSight"), TEXT("ShadowSight") };
	for (const FName ComponentName : ComponentNames)
	{
		const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Weapon->GetDefaultSubobjectByName(ComponentName));
		if (TestNotNull(*ComponentName.ToString(), Component))
		{
			TestTrue(*FString::Printf(TEXT("%s retains its intended rendering type"), *ComponentName.ToString()),
				Component->FirstPersonPrimitiveType == (ComponentName == TEXT("FirstHandMagazine")
					? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None));
		}
	}
	TestTrue(TEXT("Native TP weapon is not a first-person primitive"),
		Weapon->GetThirdPersonWeaponMesh()->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::None);
	return true;
}

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
	FObjectProperty* SightMeshProperty = FindFProperty<FObjectProperty>(ARangedWeaponBase::StaticClass(), TEXT("SightMesh"));
	UStaticMesh* TestSightMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Optional sight mesh setting"), SightMeshProperty)
		|| !TestNotNull(TEXT("Explicit sight fixture mesh"), TestSightMesh))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	UStaticMeshComponent* ShadowSight = Cast<UStaticMeshComponent>(
		Weapon->GetDefaultSubobjectByName(TEXT("ShadowSight")));
	TestNotNull(TEXT("Pistol has a first-person sight component"), FirstSight);
	TestNotNull(TEXT("Pistol has a shadow sight component"), ShadowSight);
	if (FirstSight)
	{
		TestTrue(TEXT("Sight is hidden while the weapon is a pickup"), FirstSight->bHiddenInGame);
	}

	// 실제 권총 BP의 선택적 SightMesh 유무와 무관하게 두 설정의 수명주기를 검사한다.
	for (const FName ComponentName : { FName(TEXT("FirstSight")), FName(TEXT("ThirdSight")), FName(TEXT("ShadowSight")) })
	{
		if (UStaticMeshComponent* Sight = Cast<UStaticMeshComponent>(Weapon->GetDefaultSubobjectByName(ComponentName)))
		{
			Sight->SetStaticMesh(nullptr);
		}
	}
	SightMeshProperty->SetObjectPropertyValue_InContainer(Weapon, nullptr);
	Weapon->OnEquipped(Shooter);
	Weapon->ShowEquippedPresentation();
	if (FirstSight)
	{
		TestTrue(TEXT("Unconfigured sight stays hidden while equipped"), FirstSight->bHiddenInGame);
	}
	Weapon->OnUnequipped();
	SightMeshProperty->SetObjectPropertyValue_InContainer(Weapon, TestSightMesh);
	Weapon->OnEquipped(Shooter);
	Weapon->ShowEquippedPresentation();
	if (FirstSight)
	{
		TestTrue(TEXT("Configured sight mesh is applied"), FirstSight->GetStaticMesh() == TestSightMesh);
		TestFalse(TEXT("Current weapon sight is shown after equip presentation"), FirstSight->bHiddenInGame);
	}

	if (ShadowSight)
	{
		ShadowSight->SetCastShadow(true);
		ShadowSight->SetCastHiddenShadow(true);
	}
	Weapon->OnUnequipped();
	if (FirstSight)
	{
		TestTrue(TEXT("Previous weapon sight is hidden after unequip"), FirstSight->bHiddenInGame);
	}
	if (ShadowSight)
	{
		TestFalse(TEXT("Previous sight no longer casts a shadow"), ShadowSight->CastShadow);
		TestFalse(TEXT("Previous sight no longer casts a hidden shadow"), ShadowSight->bCastHiddenShadow);
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
