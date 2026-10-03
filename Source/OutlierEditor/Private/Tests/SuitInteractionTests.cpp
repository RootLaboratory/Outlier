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
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpointSnapshot.h"
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

bool LoadSuitPresentationConfiguration(FAutomationTestBase& Test, FShooterPresentationConfiguration& Configuration)
{
	Configuration.FirstPersonMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/1P/Suit/1_Meshes/SKM_Player1_POV01.SKM_Player1_POV01"));
	Configuration.FirstPersonAnimClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Characters/1P/Suit/1_Meshes/Animations/ABP_FPS_ShooterArm.ABP_FPS_ShooterArm_C"));
	Configuration.ThirdPersonMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/1P/Suit/3_Meshes/SKM_Player_01.SKM_Player_01"));
	Configuration.ThirdPersonAnimClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Characters/1P/Suit/3_Meshes/Animations/ABP_Shooter.ABP_Shooter_C"));
	FString Error;
	if (!Test.TestTrue(TEXT("Explicit Suit test assets form valid Mesh/ABP pairs"), Configuration.Validate(Error)))
	{
		Test.AddError(Error);
		return false;
	}
	return true;
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
	FShooterPresentationConfiguration PreSuitConfiguration;
	if (!LoadSuitPresentationConfiguration(*this, PreSuitConfiguration))
	{
		return false;
	}
	FShooterPresentationConfiguration SuitConfiguration = PreSuitConfiguration;
	SuitConfiguration.FirstPersonMesh = DuplicateObject<USkeletalMesh>(PreSuitConfiguration.FirstPersonMesh, GetTransientPackage());
	SuitConfiguration.ThirdPersonMesh = DuplicateObject<USkeletalMesh>(PreSuitConfiguration.ThirdPersonMesh, GetTransientPackage());
	SuitConfiguration.FirstPersonAnimClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Characters/1P/Suit/1_Meshes/Animations/ABP_FP_ArmsProcedural.ABP_FP_ArmsProcedural_C"));
	FString ConfigurationError;
	if (!TestTrue(TEXT("Alternate Suit ABP is compatible"), SuitConfiguration.Validate(ConfigurationError)))
	{
		AddError(ConfigurationError);
		return false;
	}
	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActorDeferred<AShooterCharacter>(
		ShooterClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
	if (Shooter)
	{
		Shooter->PreSuitPresentation = PreSuitConfiguration;
		Shooter->SuitPresentation = SuitConfiguration;
		Shooter->FinishSpawning(FTransform::Identity);
	}
	APartnerCharacter* Partner = PartnerClass
		? World->SpawnActor<APartnerCharacter>(PartnerClass)
		: nullptr;
	UStaticMesh* SuitDisplayAsset = NewObject<UStaticMesh>(GetTransientPackage());
	USkeletalMesh* FirstPersonSuitMesh = SuitConfiguration.FirstPersonMesh;
	USkeletalMesh* ThirdPersonSuitMesh = SuitConfiguration.ThirdPersonMesh;

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
	AOutlierPlayerState* ShooterPS = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerPS = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Shooter PlayerState spawns"), ShooterPS)
		|| !TestNotNull(TEXT("Partner PlayerState spawns"), PartnerPS))
	{
		return false;
	}
	Shooter->SetPlayerState(ShooterPS);
	Partner->SetPlayerState(PartnerPS);
	ShooterPS->SetShooterCharacter(Shooter);
	ShooterPS->SetPartnerCharacter(Partner);
	PartnerPS->SetShooterCharacter(Shooter);
	PartnerPS->SetPartnerCharacter(Partner);
	TestTrue(TEXT("Before acquisition the complete PreSuit configuration is used"),
		Shooter->GetFirstPersonMesh()->GetSkeletalMeshAsset() == PreSuitConfiguration.FirstPersonMesh
		&& Shooter->GetFirstPersonMesh()->GetAnimClass() == PreSuitConfiguration.FirstPersonAnimClass.Get());

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

	// 일부만 지정된 외형은 무기 지급/소비 이전에 거부되어 재시도 가능한 상태를 유지한다.
	Shooter->SuitPresentation.ThirdPersonAnimClass = nullptr;
	TestFalse(TEXT("Invalid Suit presentation rejects acquisition"), Suit->Interact(Shooter));
	TestNull(TEXT("Rejected acquisition grants no Shooter weapon"), Shooter->GetCurrentWeapon());
	TestNull(TEXT("Rejected acquisition grants no Partner weapon"), Partner->GetCurrentWeapon());
	TestFalse(TEXT("Rejected acquisition does not unlock Suit"), ShooterPS->GetAcquiredSuit());
	TestTrue(TEXT("Rejected acquisition preserves the PreSuit Mesh/ABP and selection"),
		Shooter->GetPresentation() == EShooterPresentation::PreSuit
		&& Shooter->GetFirstPersonMesh()->GetSkeletalMeshAsset() == PreSuitConfiguration.FirstPersonMesh
		&& Shooter->GetFirstPersonMesh()->GetAnimClass() == PreSuitConfiguration.FirstPersonAnimClass.Get());
	TestFalse(TEXT("Rejected acquisition does not consume the Interaction"), Suit->IsHidden());
	TestTrue(TEXT("Rejected acquisition keeps stored Shooter Rifle"), StoredShooterRifle && StoredShooterRifle->IsHidden());
	Shooter->SuitPresentation = SuitConfiguration;
	TestTrue(TEXT("Shooter completes the Suit interaction immediately"), Suit->Interact(Shooter));
	TestTrue(TEXT("Acquisition replaces the PreSuit ABP with the Suit ABP"),
		Shooter->GetFirstPersonMesh()->GetAnimClass() == SuitConfiguration.FirstPersonAnimClass.Get());
	TestTrue(TEXT("Acquisition publishes the Suit presentation"), Shooter->GetPresentation() == EShooterPresentation::Suit);
	TestTrue(TEXT("Successful acquisition unlocks both PlayerStates"),
		ShooterPS->GetAcquiredSuit() && PartnerPS->GetAcquiredSuit());
	TestTrue(TEXT("Compatibility save fields record the applied BP meshes"),
		ShooterPS->GetSuitFirstPersonMesh() == SuitConfiguration.FirstPersonMesh
		&& ShooterPS->GetSuitThirdPersonMesh() == SuitConfiguration.ThirdPersonMesh);

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
	FString Error;
	if (!LoadSuitPresentationConfiguration(*this, TestConfiguration))
	{
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
	Shooter->bInitialPresentationCaptured = false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterPresentationReplicationTest,
	"Outlier.Animation.Shooter.PresentationReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPresentationReplicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedSuitInteractionTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}
	FShooterPresentationConfiguration PreSuitConfiguration;
	if (!LoadSuitPresentationConfiguration(*this, PreSuitConfiguration))
	{
		return false;
	}
	FShooterPresentationConfiguration SuitConfiguration = PreSuitConfiguration;
	SuitConfiguration.FirstPersonMesh = DuplicateObject<USkeletalMesh>(PreSuitConfiguration.FirstPersonMesh, GetTransientPackage());
	SuitConfiguration.ThirdPersonMesh = DuplicateObject<USkeletalMesh>(PreSuitConfiguration.ThirdPersonMesh, GetTransientPackage());
	SuitConfiguration.FirstPersonAnimClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Characters/1P/Suit/1_Meshes/Animations/ABP_FP_ArmsProcedural.ABP_FP_ArmsProcedural_C"));
	FString Error;
	if (!TestTrue(TEXT("Different Suit ABP forms a complete configuration"), SuitConfiguration.Validate(Error)))
	{
		AddError(Error);
		return false;
	}
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Shooter Blueprint is available"), ShooterClass))
	{
		return false;
	}

	// 실제 네트워크 전송 대신 RepNotify와 초기화 순서를 명시적으로 재현한다.
	const auto SpawnShooter = [&](bool bReplica, const FShooterPresentationState* ReceivedState)
	{
		AShooterCharacter* Character = TestWorld.World->SpawnActorDeferred<AShooterCharacter>(ShooterClass,
			FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Character)
		{
			return Character;
		}
		Character->PreSuitPresentation = PreSuitConfiguration;
		Character->SuitPresentation = SuitConfiguration;
		Character->GetFirstPersonMesh()->SetSkeletalMesh(PreSuitConfiguration.FirstPersonMesh, true);
		Character->GetFirstPersonMesh()->SetAnimInstanceClass(PreSuitConfiguration.FirstPersonAnimClass.Get());
		Character->GetMesh()->SetSkeletalMesh(PreSuitConfiguration.ThirdPersonMesh, true);
		Character->GetMesh()->SetAnimInstanceClass(PreSuitConfiguration.ThirdPersonAnimClass.Get());
		if (bReplica)
		{
			Character->SetRole(ROLE_SimulatedProxy);
		}
		if (ReceivedState)
		{
			Character->PresentationState = *ReceivedState;
			Character->OnRep_PresentationState();
			TestTrue(TEXT("Pre-BeginPlay RepNotify does not overwrite the initial Mesh"),
				Character->GetFirstPersonMesh()->GetSkeletalMeshAsset() == PreSuitConfiguration.FirstPersonMesh);
		}
		Character->FinishSpawning(FTransform::Identity);
		if (!Character->HasActorBegunPlay())
		{
			Character->DispatchBeginPlay();
		}
		return Character;
	};

	AShooterCharacter* ServerShooter = SpawnShooter(false, nullptr);
	if (!TestNotNull(TEXT("Authority Shooter spawns"), ServerShooter))
	{
		return false;
	}
	// 설정된 BP 구성은 오래된 저장 Mesh가 비호환이어도 그 참조를 사용하지 않는다.
	USkeletalMesh* OldSavedMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	TestTrue(TEXT("Explicit Suit configuration takes priority over old saved meshes"),
		ServerShooter->SetSuitPresentation(true, OldSavedMesh, OldSavedMesh));
	TestTrue(TEXT("Authority uses the complete Suit Mesh/ABP"),
		ServerShooter->GetFirstPersonMesh()->GetSkeletalMeshAsset() == SuitConfiguration.FirstPersonMesh
		&& ServerShooter->GetFirstPersonMesh()->GetAnimClass() == SuitConfiguration.FirstPersonAnimClass.Get());
	TestFalse(TEXT("Explicit configuration needs no replicated legacy assets"), ServerShooter->PresentationState.bUseLegacyMeshes);
	TestNull(TEXT("Explicit configuration clears legacy 1P payload"), ServerShooter->PresentationState.LegacyFirstPersonMesh.Get());
	UAnimInstance* SuitInstance = ServerShooter->GetFirstPersonMesh()->GetAnimInstance();
	TestTrue(TEXT("Repeated authoritative state succeeds"), ServerShooter->SetSuitPresentation(true));
	TestTrue(TEXT("Repeated state preserves the AnimInstance"), ServerShooter->GetFirstPersonMesh()->GetAnimInstance() == SuitInstance);
	ServerShooter->SuitPresentation.ThirdPersonAnimClass = nullptr;
	TestFalse(TEXT("Invalid authoritative configuration is rejected"), ServerShooter->SetSuitPresentation(true));
	TestTrue(TEXT("Rejected authority update preserves the state and instance"),
		ServerShooter->GetPresentation() == EShooterPresentation::Suit
		&& ServerShooter->GetFirstPersonMesh()->GetAnimInstance() == SuitInstance);
	ServerShooter->SuitPresentation = SuitConfiguration;

	AShooterCharacter* LateStateReplica = SpawnShooter(true, nullptr);
	if (!TestNotNull(TEXT("Replica without an initial selection spawns"), LateStateReplica))
	{
		return false;
	}
	LateStateReplica->PresentationState = ServerShooter->PresentationState;
	LateStateReplica->OnRep_PresentationState();
	TestTrue(TEXT("A state arriving after BeginPlay applies the complete Suit"),
		LateStateReplica->GetFirstPersonMesh()->GetSkeletalMeshAsset() == SuitConfiguration.FirstPersonMesh
		&& LateStateReplica->GetFirstPersonMesh()->GetAnimClass() == SuitConfiguration.FirstPersonAnimClass.Get());

	AShooterCharacter* Replica = SpawnShooter(true, &ServerShooter->PresentationState);
	if (!TestNotNull(TEXT("Replica Shooter spawns"), Replica))
	{
		return false;
	}
	TestTrue(TEXT("BeginPlay preserves an already received Suit selection"),
		Replica->GetFirstPersonMesh()->GetSkeletalMeshAsset() == SuitConfiguration.FirstPersonMesh
		&& Replica->GetFirstPersonMesh()->GetAnimClass() == SuitConfiguration.FirstPersonAnimClass.Get());
	TestTrue(TEXT("Initial fallback was captured before applying the received state"),
		Replica->InitialPresentation.FirstPersonMesh == PreSuitConfiguration.FirstPersonMesh);
	UAnimInstance* ReplicaInstance = Replica->GetFirstPersonMesh()->GetAnimInstance();
	TestFalse(TEXT("Non-authority cannot change the presentation selection"), Replica->SetSuitPresentation(false));
	TestTrue(TEXT("Rejected client write preserves its instance"), Replica->GetFirstPersonMesh()->GetAnimInstance() == ReplicaInstance);
	Replica->OnRep_PresentationState();
	TestTrue(TEXT("Repeated RepNotify is idempotent"), Replica->GetFirstPersonMesh()->GetAnimInstance() == ReplicaInstance);

	AOutlierPlayerState* LatePlayerState = TestWorld.World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Late PlayerState spawns"), LatePlayerState))
	{
		return false;
	}
	Replica->SetPlayerState(LatePlayerState);
	Replica->OnRep_PlayerState();
	TestTrue(TEXT("An older PlayerState flag cannot overwrite the replicated Suit"),
		Replica->GetFirstPersonMesh()->GetAnimClass() == SuitConfiguration.FirstPersonAnimClass.Get());

	ASuitInteractionTestRifle* LateWeapon = TestWorld.World->SpawnActor<ASuitInteractionTestRifle>();
	if (!TestNotNull(TEXT("Late weapon spawns"), LateWeapon))
	{
		return false;
	}
	LateWeapon->OnEquipped(Replica);
	LateWeapon->GetFirstPersonWeaponMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Replica->CurrentWeapon = LateWeapon;
	Replica->OnRep_CurrentWeapon();
	TestTrue(TEXT("Late CurrentWeapon is attached to the new first-person mesh"),
		LateWeapon->GetFirstPersonWeaponMesh()->GetAttachParent() == Replica->GetFirstPersonMesh());
	TestTrue(TEXT("Late weapon keeps third-person and Shadow attachments"),
		LateWeapon->GetThirdPersonWeaponMesh()->GetAttachParent() == Replica->GetMesh()
		&& LateWeapon->GetShadowWeaponMesh()->GetAttachParent() == Replica->GetShadowMesh());

	// 호환 경로의 한쪽 참조가 미해결이면 전체 적용을 보류한다.
	Replica->SuitPresentation = FShooterPresentationConfiguration();
	Replica->PresentationState.bUseLegacyMeshes = true;
	Replica->PresentationState.LegacyFirstPersonMesh = SuitConfiguration.FirstPersonMesh;
	Replica->PresentationState.LegacyThirdPersonMesh = nullptr;
	Replica->OnRep_PresentationState();
	TestTrue(TEXT("Incomplete legacy payload preserves the current instance"),
		Replica->GetFirstPersonMesh()->GetAnimInstance() == ReplicaInstance);
	Replica->PresentationState.LegacyThirdPersonMesh = SuitConfiguration.ThirdPersonMesh;
	Replica->OnRep_PresentationState();
	TestTrue(TEXT("Complete legacy payload uses the compatible initial ABP"),
		Replica->GetFirstPersonMesh()->GetAnimClass() == PreSuitConfiguration.FirstPersonAnimClass.Get());
	TestTrue(TEXT("Shadow follows the final third-person mesh"),
		Replica->GetShadowMesh()->GetSkeletalMeshAsset() == Replica->GetMesh()->GetSkeletalMeshAsset()
		&& Replica->GetShadowMesh()->LeaderPoseComponent.Get() == Replica->GetMesh());

	// 저장 상태와 Pawn 링크의 순서를 바꿔도 서버는 획득 여부로 같은 외형을 복원한다.
	AOutlierPlayerState* SavedPlayerState = TestWorld.World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Restore PlayerState spawns"), SavedPlayerState))
	{
		return false;
	}
	FOutlierReconnectGameplayState SavedState;
	FOutlierSuitSnapshot LegacySnapshot;
	LegacySnapshot.bAcquired = true;
	LegacySnapshot.FirstPersonMesh = OldSavedMesh;
	LegacySnapshot.ThirdPersonMesh = OldSavedMesh;
	SavedState.bHasAcquiredSuit = LegacySnapshot.bAcquired;
	SavedState.SuitFirstPersonMesh = LegacySnapshot.FirstPersonMesh;
	SavedState.SuitThirdPersonMesh = LegacySnapshot.ThirdPersonMesh;
	SavedPlayerState->RestoreReconnectGameplayState(SavedState);
	ServerShooter->SetSuitPresentation(false);
	ServerShooter->SetPlayerState(SavedPlayerState);
	SavedPlayerState->SetShooterCharacter(ServerShooter);
	TestTrue(TEXT("Acquired reconnect state restores the complete Suit after Pawn linking"),
		ServerShooter->GetFirstPersonMesh()->GetSkeletalMeshAsset() == SuitConfiguration.FirstPersonMesh
		&& ServerShooter->GetFirstPersonMesh()->GetAnimClass() == SuitConfiguration.FirstPersonAnimClass.Get());
	SavedState.bHasAcquiredSuit = false;
	SavedPlayerState->RestoreReconnectGameplayState(SavedState);
	TestTrue(TEXT("Unacquired reconnect state restores the complete PreSuit"),
		ServerShooter->GetFirstPersonMesh()->GetSkeletalMeshAsset() == PreSuitConfiguration.FirstPersonMesh
		&& ServerShooter->GetFirstPersonMesh()->GetAnimClass() == PreSuitConfiguration.FirstPersonAnimClass.Get());
	TestNull(TEXT("Restore does not grant a new weapon"), ServerShooter->GetCurrentWeapon());
	TestFalse(TEXT("Restore does not change the saved acquisition flag"), SavedPlayerState->GetAcquiredSuit());
	return true;
}

#endif
