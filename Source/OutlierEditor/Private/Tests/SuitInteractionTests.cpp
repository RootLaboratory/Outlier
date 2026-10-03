#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/SuitInteractionTestActors.h"

#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
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
#include "ReferenceSkeleton.h"
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

	if (StoredPartnerWeapon && StoredShooterRifle)
	{
		TestEqual(TEXT("Equipped Partner weapon belongs to Partner"), StoredPartnerWeapon->GetOwner(), static_cast<AActor*>(Partner));
		TestTrue(TEXT("Partner can be destroyed"), Partner->Destroy());
		TestNull(TEXT("Partner teardown clears CurrentWeapon"), Partner->GetCurrentWeapon());
		TestNull(TEXT("Partner teardown clears weapon ownership"), StoredPartnerWeapon->GetOwner());
		TestTrue(TEXT("Partner teardown destroys its equipped weapon"), StoredPartnerWeapon->IsActorBeingDestroyed());
		TestFalse(TEXT("Partner teardown preserves Shooter Rifle"), StoredShooterRifle->IsActorBeingDestroyed());
		TestEqual(TEXT("Shooter keeps its equipped Rifle"), Shooter->GetCurrentWeapon(), StoredShooterRifle);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterPresentationConfigurationTest,
	"Outlier.Animation.Shooter.PresentationConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPresentationConfigurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedSuitInteractionTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}

	// Shooter BP의 기본 3P 설정은 비어 있을 수 있다. 테스트 전제는 기존 Suit 에셋으로 명시한다.
	FShooterPresentationConfiguration TestConfiguration;
	TestConfiguration.FirstPersonMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/1P/Suit/1_Meshes/SKM_Player1_POV01.SKM_Player1_POV01"));
	TestConfiguration.FirstPersonAnimClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Characters/1P/Suit/1_Meshes/Animations/ABP_FPS_ShooterArm.ABP_FPS_ShooterArm_C"));
	TestConfiguration.ThirdPersonMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/1P/Suit/3_Meshes/SKM_Player_01.SKM_Player_01"));
	TestConfiguration.ThirdPersonAnimClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Characters/1P/Suit/3_Meshes/Animations/ABP_Shooter.ABP_Shooter_C"));
	FString Error;
	if (!TestTrue(TEXT("Explicit Suit test assets form valid Mesh/ABP pairs"), TestConfiguration.Validate(Error)))
	{
		AddError(Error);
		return false;
	}

	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass
		? TestWorld.World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter Blueprint spawns"), Shooter))
	{
		return false;
	}

	Shooter->GetFirstPersonMesh()->SetSkeletalMesh(TestConfiguration.FirstPersonMesh, true);
	Shooter->GetFirstPersonMesh()->SetAnimInstanceClass(TestConfiguration.FirstPersonAnimClass.Get());
	Shooter->GetMesh()->SetSkeletalMesh(TestConfiguration.ThirdPersonMesh, true);
	Shooter->GetMesh()->SetAnimInstanceClass(TestConfiguration.ThirdPersonAnimClass.Get());

	// 저장 에셋은 변경하지 않고 이 Pawn에서만 미설정 상태와 완전한 구성을 비교한다.
	Shooter->PreSuitPresentation = FShooterPresentationConfiguration();
	Shooter->SuitPresentation = FShooterPresentationConfiguration();
	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}
	else
	{
		// 월드가 이미 시작된 경우에도 fixture 적용 후의 구성을 fallback 기준으로 캡처한다.
		Shooter->CaptureInitialPresentation();
	}
	const FShooterPresentationConfiguration Baseline = Shooter->InitialPresentation;
	if (!TestTrue(TEXT("Captured test Mesh/ABP pairs are valid"), Baseline.Validate(Error)))
	{
		AddError(Error);
		return false;
	}

	USkeletalMeshComponent* Mesh1P = Shooter->GetFirstPersonMesh();
	USkeletalMeshComponent* Mesh3P = Shooter->GetMesh();
	USkeletalMeshComponent* Shadow = Shooter->GetShadowMesh();
	UAnimInstance* Initial1PInstance = Mesh1P->GetAnimInstance();
	UAnimInstance* Initial3PInstance = Mesh3P->GetAnimInstance();
	TestTrue(TEXT("Unset PreSuit falls back to the captured initial configuration"),
		Shooter->ApplyPresentationConfiguration(false));
	TestTrue(TEXT("Fallback preserves 1P AnimInstance"), Mesh1P->GetAnimInstance() == Initial1PInstance);
	TestTrue(TEXT("Fallback preserves 3P AnimInstance"), Mesh3P->GetAnimInstance() == Initial3PInstance);

	FShooterPresentationConfiguration Invalid = Baseline;
	Invalid.ThirdPersonAnimClass = nullptr;
	TestFalse(TEXT("Partial configuration is rejected"), Invalid.Validate(Error));
	Shooter->SuitPresentation = Invalid;
	TestFalse(TEXT("Rejected configuration cannot change either component"),
		Shooter->ApplyPresentationConfiguration(true));
	TestTrue(TEXT("Rejected configuration preserves 1P instance"), Mesh1P->GetAnimInstance() == Initial1PInstance);
	TestTrue(TEXT("Rejected configuration preserves 3P instance"), Mesh3P->GetAnimInstance() == Initial3PInstance);
	TestTrue(TEXT("Rejected configuration preserves 1P mesh"), Mesh1P->GetSkeletalMeshAsset() == Baseline.FirstPersonMesh);
	TestTrue(TEXT("Rejected configuration preserves 3P mesh"), Mesh3P->GetSkeletalMeshAsset() == Baseline.ThirdPersonMesh);

	Invalid = Baseline;
	Invalid.FirstPersonAnimClass = UAnimInstance::StaticClass();
	TestFalse(TEXT("Wrong 1P AnimInstance parent is rejected"), Invalid.Validate(Error));
	Invalid = Baseline;
	Invalid.ThirdPersonAnimClass = Baseline.FirstPersonAnimClass;
	TestFalse(TEXT("Wrong 3P AnimInstance parent is rejected"), Invalid.Validate(Error));

	USkeleton* ForeignSkeleton = NewObject<USkeleton>(GetTransientPackage());
	USkeletalMesh* ForeignMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	ForeignMesh->SetSkeleton(ForeignSkeleton);
	{
		FReferenceSkeletonModifier Modifier(ForeignMesh->GetRefSkeleton(), ForeignSkeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("PresentationTestUnrelatedRoot"), TEXT("PresentationTestUnrelatedRoot"), INDEX_NONE),
			FTransform::Identity);
	}
	Invalid = Baseline;
	Invalid.FirstPersonMesh = ForeignMesh;
	TestFalse(TEXT("Incompatible Skeleton is rejected"), Invalid.Validate(Error));

	// 두 Mesh만 transient 복제하여 실제 교체/재초기화를 검사한다. 원본 BP/에셋은 보존한다.
	Shooter->SuitPresentation = Baseline;
	Shooter->SuitPresentation.FirstPersonMesh = DuplicateObject<USkeletalMesh>(Baseline.FirstPersonMesh, GetTransientPackage());
	Shooter->SuitPresentation.ThirdPersonMesh = DuplicateObject<USkeletalMesh>(Baseline.ThirdPersonMesh, GetTransientPackage());
	if (!TestTrue(TEXT("Complete Suit configuration is applied"), Shooter->ApplyPresentationConfiguration(true)))
	{
		return false;
	}
	TestTrue(TEXT("Suit 1P Mesh is selected"), Mesh1P->GetSkeletalMeshAsset() == Shooter->SuitPresentation.FirstPersonMesh);
	TestTrue(TEXT("Suit 3P Mesh is selected"), Mesh3P->GetSkeletalMeshAsset() == Shooter->SuitPresentation.ThirdPersonMesh);
	TestTrue(TEXT("Suit 1P ABP is selected"), Mesh1P->GetAnimClass() == Baseline.FirstPersonAnimClass.Get());
	TestTrue(TEXT("Suit 3P ABP is selected"), Mesh3P->GetAnimClass() == Baseline.ThirdPersonAnimClass.Get());
	TestTrue(TEXT("Shadow uses the new 3P Mesh"), Shadow->GetSkeletalMeshAsset() == Mesh3P->GetSkeletalMeshAsset());
	TestTrue(TEXT("Shadow follows the 3P pose"), Shadow->LeaderPoseComponent.Get() == Mesh3P);
	UAnimInstance* Suit1PInstance = Mesh1P->GetAnimInstance();
	UAnimInstance* Suit3PInstance = Mesh3P->GetAnimInstance();
	TestNotNull(TEXT("New 1P AnimInstance is initialized"), Suit1PInstance);
	TestNotNull(TEXT("New 3P AnimInstance is initialized"), Suit3PInstance);
	TestTrue(TEXT("Mesh change replaces the 1P instance"), Suit1PInstance != Initial1PInstance);
	TestTrue(TEXT("Mesh change replaces the 3P instance"), Suit3PInstance != Initial3PInstance);
	TestTrue(TEXT("Repeated Suit application succeeds"), Shooter->ApplyPresentationConfiguration(true));
	TestTrue(TEXT("Repeated application preserves 1P instance"), Mesh1P->GetAnimInstance() == Suit1PInstance);
	TestTrue(TEXT("Repeated application preserves 3P instance"), Mesh3P->GetAnimInstance() == Suit3PInstance);
	TestTrue(TEXT("PreSuit fallback restores the original configuration"), Shooter->ApplyPresentationConfiguration(false));
	TestTrue(TEXT("Original 1P Mesh is restored"), Mesh1P->GetSkeletalMeshAsset() == Baseline.FirstPersonMesh);
	TestTrue(TEXT("Original 3P Mesh is restored"), Mesh3P->GetSkeletalMeshAsset() == Baseline.ThirdPersonMesh);
	TestTrue(TEXT("Shadow follows the restored Mesh"), Shadow->GetSkeletalMeshAsset() == Baseline.ThirdPersonMesh);
	TestFalse(TEXT("Presentation does not write legacy replicated Suit Mesh state"),
		Shooter->AppliedSuitFirstPersonMesh || Shooter->AppliedSuitThirdPersonMesh);
	return true;
}

#endif
