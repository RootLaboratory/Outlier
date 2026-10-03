#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/SuitInteractionTestActors.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
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
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "ReferenceSkeleton.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpointSnapshot.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterAnimInstance.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
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

	void AdvanceTime(float Duration)
	{
		// 동기 테스트는 엔진 프레임을 넘기지 않는다. TimerManager의 프레임당 1회 Tick 제한을
		// 피하도록 테스트 프레임을 진행하고, 종료 시 전역 카운터를 원래 값으로 복구한다.
		TGuardValue<uint64> RestoreFrameCounter(GFrameCounter, GFrameCounter);
		// 첫 Tick은 Pending 타이머를 활성화한다. 큰 Delta 한 번 대신 작은 프레임들로 만료까지 진행한다.
		constexpr float Step = 1.0f / 60.0f;
		for (float Remaining = Duration; Remaining > KINDA_SMALL_NUMBER; Remaining -= Step)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, FMath::Min(Step, Remaining));
		}
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
	ASuitTransitionTestPlayerController* ShooterController = World->SpawnActor<ASuitTransitionTestPlayerController>();
	ASuitTransitionTestPlayerController* PartnerController = World->SpawnActor<ASuitTransitionTestPlayerController>();
	if (!TestNotNull(TEXT("Shooter Controller spawns"), ShooterController)
		|| !TestNotNull(TEXT("Partner Controller spawns"), PartnerController))
	{
		return false;
	}
	ShooterController->Possess(Shooter);
	PartnerController->Possess(Partner);
	AOutlierPlayerState* ShooterPS = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerPS = World->SpawnActor<AOutlierPlayerState>();
	if (!TestNotNull(TEXT("Shooter PlayerState spawns"), ShooterPS)
		|| !TestNotNull(TEXT("Partner PlayerState spawns"), PartnerPS))
	{
		return false;
	}
	Shooter->SetPlayerState(ShooterPS);
	Partner->SetPlayerState(PartnerPS);
	ShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
	ShooterPS->SetPairId(0);
	PartnerPS->SetPairId(0);
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
	// 완료 신호는 실제 Controller 응답 경계에 명시적으로 넣는다. 빈 연출 연결 지점은 응답하지 않는다.
	Shooter->SuitFadeOutDuration = 0.0f;
	Shooter->SuitBlackHoldDuration = 0.0f;
	Shooter->SuitFadeInDuration = 0.0f;
	TestTrue(TEXT("Shooter reserves the Suit interaction"), Suit->Interact(Shooter));
	const FGuid TransitionId = Shooter->GetSuitTransitionId();
	TestFalse(TEXT("Reservation is not acquisition"), ShooterPS->GetAcquiredSuit());
	TestNull(TEXT("Reservation grants no weapon"), Shooter->GetCurrentWeapon());
	TestTrue(TEXT("Reservation blocks both participants"), Shooter->IsSuitTransitionBlocked() && Partner->IsSuitTransitionBlocked());
	TestTrue(TEXT("Fade out request reaches both owning controllers with the same ID"),
		ShooterController->FadeOutRequests == 1 && PartnerController->FadeOutRequests == 1
		&& ShooterController->LastRequestedId == TransitionId && PartnerController->LastRequestedId == TransitionId);
	ShooterController->NotifySuitFadeOutFinished(TransitionId);
	TestFalse(TEXT("One ready participant cannot commit"), ShooterPS->GetAcquiredSuit());
	PartnerController->NotifySuitFadeOutFinished(TransitionId);
	TestTrue(TEXT("Committed transition waits for applied presentation"), Shooter->GetSuitTransitionPhase() == ESuitTransitionPhase::Applying);
	TestTrue(TEXT("Applying requests do not automatically report readiness"),
		ShooterController->PresentationRequests == 1 && PartnerController->PresentationRequests == 1
		&& ShooterController->FadeInRequests == 0 && PartnerController->FadeInRequests == 0);
	ShooterController->NotifySuitPresentationReady(TransitionId);
	TestTrue(TEXT("One applied participant cannot start fade in"), Shooter->GetSuitTransitionPhase() == ESuitTransitionPhase::Applying);
	PartnerController->NotifySuitPresentationReady(TransitionId);
	TestTrue(TEXT("Fade in request reaches both controllers"),
		ShooterController->FadeInRequests == 1 && PartnerController->FadeInRequests == 1);
	ShooterController->NotifySuitFadeInFinished(TransitionId);
	TestTrue(TEXT("One restored screen cannot release both blocks"), Shooter->IsSuitTransitionBlocked() && Partner->IsSuitTransitionBlocked());
	PartnerController->NotifySuitFadeInFinished(TransitionId);
	TestTrue(TEXT("Normal completion cleans up both local transitions exactly once"),
		ShooterController->CleanupRequests == 1 && PartnerController->CleanupRequests == 1
		&& ShooterController->LastCleanupId == TransitionId && PartnerController->LastCleanupId == TransitionId);
	TestFalse(TEXT("Finished transition releases Shooter block"), Shooter->IsSuitTransitionBlocked());
	TestFalse(TEXT("Finished transition releases Partner block"), Partner->IsSuitTransitionBlocked());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierShooterPresentationAnimationTest,
	"Outlier.Animation.Shooter.PresentationAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierShooterPresentationAnimationTest::RunTest(const FString& Parameters)
{
	FScopedSuitInteractionTestWorld TestWorld;
	FShooterPresentationConfiguration Configuration;
	if (!TestWorld.Initialize(*this) || !LoadSuitPresentationConfiguration(*this, Configuration))
	{
		return false;
	}
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Shooter BP exists"), ShooterClass))
	{
		return false;
	}
	AShooterCharacter* Shooter = TestWorld.World->SpawnActorDeferred<AShooterCharacter>(ShooterClass,
		FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Animation test Shooter spawns"), Shooter))
	{
		return false;
	}
	// 같은 Mesh/ABP를 사용해 인스턴스 재생성 없이 상태만 바뀌는 경계도 검증한다.
	Shooter->PreSuitPresentation = Configuration;
	Shooter->SuitPresentation = Configuration;
	Shooter->FinishSpawning(FTransform::Identity);
	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}
	TestTrue(TEXT("PreSuit applies"), Shooter->SetSuitPresentation(false));
	ASuitInteractionTestRifle* Weapon = TestWorld.World->SpawnActor<ASuitInteractionTestRifle>();
	if (!TestNotNull(TEXT("Procedural test weapon spawns"), Weapon))
	{
		return false;
	}
	UProceduralAnimValues* Common = NewObject<UProceduralAnimValues>(Weapon);
	UProceduralAnimValues* PreSuit = NewObject<UProceduralAnimValues>(Weapon);
	UProceduralAnimValues* Suit = NewObject<UProceduralAnimValues>(Weapon);
	PreSuit->WeaponValues.FirstPersonRecoilMultiplier = 2.0f;
	PreSuit->WeaponValues.ThirdPersonRecoilMultiplier = 3.0f;
	PreSuit->WeaponValues.ThirdPersonSprintMultiplier = 4.0f;
	PreSuit->WeaponValues.ThirdPersonWallOffsetMultiplier = 5.0f;
	Suit->WeaponValues.FirstPersonRecoilMultiplier = 6.0f;
	Suit->WeaponValues.ThirdPersonRecoilMultiplier = 7.0f;
	Suit->WeaponValues.ThirdPersonSprintMultiplier = 8.0f;
	Suit->WeaponValues.ThirdPersonWallOffsetMultiplier = 9.0f;
	Weapon->ConfigureProceduralValues(Common, PreSuit, Suit);
	TestTrue(TEXT("Ownerless weapon uses common DA"), Weapon->GetFirstPersonProceduralValues() == Common);
	Shooter->CurrentWeapon = Weapon;
	Weapon->OnEquipped(Shooter);
	TestTrue(TEXT("PreSuit selects own DA"), Weapon->GetFirstPersonProceduralValues() == PreSuit);
	TestEqual(TEXT("PreSuit FP recoil"), Weapon->GetFirstPersonProceduralRecoilMultiplier(), 2.0f);
	TestEqual(TEXT("PreSuit TP recoil"), Weapon->GetThirdPersonProceduralRecoilMultiplier(), 3.0f);
	TestEqual(TEXT("PreSuit TP sprint"), Weapon->GetThirdPersonProceduralSprintMultiplier(), 4.0f);
	TestEqual(TEXT("PreSuit TP wall"), Weapon->GetThirdPersonProceduralWallOffsetMultiplier(), 5.0f);

	UShooterFirstPersonAnimInstance* FP = Cast<UShooterFirstPersonAnimInstance>(Shooter->GetFirstPersonMesh()->GetAnimInstance());
	UShooterAnimInstance* TP = Cast<UShooterAnimInstance>(Shooter->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("FP instance exists"), FP) || !TestNotNull(TEXT("TP instance exists"), TP))
	{
		return false;
	}
	FP->RefreshPresentationState();
	FP->ViewModelRecoilLoc = FVector(10.0f);
	FP->ReloadAimAlpha = 1.0f;
	FP->LastLeftHandActionGripOffsetLoc = FVector(20.0f);
	FP->bWeaponSwitchPoseActive = true;
	TP->ThirdPersonRecoilLocTarget = FVector(30.0f);
	TestTrue(TEXT("Suit applies with same weapon and ABP"), Shooter->SetSuitPresentation(true));
	TestTrue(TEXT("FP instance is retained"), Shooter->GetFirstPersonMesh()->GetAnimInstance() == FP);
	TestTrue(TEXT("Same weapon now selects Suit DA"), Weapon->GetFirstPersonProceduralValues() == Suit);
	TestTrue(TEXT("FP immediately reads Suit DA"), FP->CurrentProceduralValues == Suit);
	TestTrue(TEXT("FP recoil resets"), FP->ViewModelRecoilLoc.IsNearlyZero());
	TestEqual(TEXT("Old reload aim blend resets"), FP->ReloadAimAlpha, 0.0f);
	TestTrue(TEXT("Old IK return cache resets"), FP->LastLeftHandActionGripOffsetLoc.IsNearlyZero());
	TestFalse(TEXT("Old weapon pose blend resets"), FP->bWeaponSwitchPoseActive);
	TestTrue(TEXT("TP recoil resets"), TP->ThirdPersonRecoilLocTarget.IsNearlyZero());
	TestEqual(TEXT("Suit FP recoil"), Weapon->GetFirstPersonProceduralRecoilMultiplier(), 6.0f);
	TestEqual(TEXT("Suit TP recoil"), Weapon->GetThirdPersonProceduralRecoilMultiplier(), 7.0f);
	TestEqual(TEXT("Suit TP sprint"), Weapon->GetThirdPersonProceduralSprintMultiplier(), 8.0f);
	TestEqual(TEXT("Suit TP wall"), Weapon->GetThirdPersonProceduralWallOffsetMultiplier(), 9.0f);
	FP->ViewModelRecoilLoc = FVector(11.0f);
	TestTrue(TEXT("Same state reapplies"), Shooter->SetSuitPresentation(true));
	TestEqual(TEXT("Idempotent apply retains live recoil"), FP->ViewModelRecoilLoc, FVector(11.0f));
	TestEqual(TEXT("Shared PreSuit asset is not modified"), PreSuit->WeaponValues.FirstPersonRecoilMultiplier, 2.0f);
	Shooter->PreSuitPresentation.ThirdPersonAnimClass = nullptr;
	AddExpectedError(TEXT("rejected authoritative PreSuit presentation"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid presentation update is rejected"), Shooter->SetSuitPresentation(false));
	TestTrue(TEXT("Rejected update retains applied Suit DA"), Weapon->GetFirstPersonProceduralValues() == Suit);
	TestEqual(TEXT("Rejected update retains animation cache"), FP->ViewModelRecoilLoc, FVector(11.0f));
	Shooter->PreSuitPresentation = Configuration;

	Weapon->ConfigureProceduralValues(Common, PreSuit, nullptr);
	TestTrue(TEXT("Missing Suit uses common, not PreSuit"), Weapon->GetFirstPersonProceduralValues() == Common);
	Weapon->ConfigureProceduralValues(nullptr, PreSuit, nullptr);
	TestNull(TEXT("Missing Suit/common does not use PreSuit"), Weapon->GetFirstPersonProceduralValues());
	TestEqual(TEXT("Missing DA preserves default multiplier"), Weapon->GetThirdPersonProceduralSprintMultiplier(), 1.0f);
	Weapon->ConfigureProceduralValues(Common, nullptr, Suit);
	TestTrue(TEXT("PreSuit restore applies"), Shooter->SetSuitPresentation(false));
	TestTrue(TEXT("Missing PreSuit uses common, not Suit"), Weapon->GetFirstPersonProceduralValues() == Common);
	Weapon->ConfigureProceduralValues(Common, Suit, Suit);
	FP->ViewModelRecoilLoc = FVector(12.0f);
	TestTrue(TEXT("Shared DA state change applies"), Shooter->SetSuitPresentation(true));
	TestTrue(TEXT("State change resets even with shared DA"), FP->ViewModelRecoilLoc.IsNearlyZero());
	Weapon->SetProceduralTestOwner(nullptr);
	TestTrue(TEXT("Owner loss restores common selection"), Weapon->GetFirstPersonProceduralValues() == Common);
	ACharacter* OtherOwner = TestWorld.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Non-Shooter owner spawns"), OtherOwner))
	{
		return false;
	}
	Weapon->SetProceduralTestOwner(OtherOwner);
	TestTrue(TEXT("Other roles keep common DA"), Weapon->GetFirstPersonProceduralValues() == Common);
	Weapon->SetProceduralTestOwner(Shooter);
	TestTrue(TEXT("Late Owner RepNotify selects applied Suit"), Weapon->GetFirstPersonProceduralValues() == Suit);

	const auto MakeMontage = [](USkeleton* Skeleton)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SetSkeleton(Skeleton);
		return Montage;
	};
	UAnimMontage* PreFP = MakeMontage(Configuration.FirstPersonMesh->GetSkeleton());
	UAnimMontage* SuitFP = MakeMontage(Configuration.FirstPersonMesh->GetSkeleton());
	UAnimMontage* PreTP = MakeMontage(Configuration.ThirdPersonMesh->GetSkeleton());
	UAnimMontage* SuitTP = MakeMontage(Configuration.ThirdPersonMesh->GetSkeleton());
	const auto ConfigureMontages = [](FShooterMontageConfiguration& Target, UAnimMontage* First, UAnimMontage* Third)
	{
		Target.FirstPersonFire = Target.FirstPersonReload = Target.FirstPersonEquip = First;
		Target.FirstPersonSlide = Target.FirstPersonMeleeAttack = First;
		Target.ThirdPersonFire = Target.ThirdPersonReload = Target.ThirdPersonEquip = Third;
		Target.ThirdPersonSlide = Target.ThirdPersonMeleeAttack = Target.ThirdPersonSwitch = Third;
	};
	ConfigureMontages(Shooter->PreSuitMontages, PreFP, PreTP);
	ConfigureMontages(Shooter->SuitMontages, SuitFP, SuitTP);
	for (EShooterMontageAction Action : { EShooterMontageAction::Fire, EShooterMontageAction::Reload,
		EShooterMontageAction::Equip, EShooterMontageAction::Slide, EShooterMontageAction::MeleeAttack })
	{
		TestTrue(TEXT("Suit FP action montage selected"), Shooter->GetActionMontage(Action, true) == SuitFP);
		TestTrue(TEXT("Suit TP action montage selected"), Shooter->GetActionMontage(Action, false) == SuitTP);
	}
	TestTrue(TEXT("Suit switch montage selected"), Shooter->GetThirdPersonSwitchMontage() == SuitTP);
	Shooter->SetSuitPresentation(false);
	TestTrue(TEXT("PreSuit FP selected"), Shooter->GetFirstPersonReloadMontage() == PreFP);
	TestTrue(TEXT("PreSuit TP selected"), Shooter->GetThirdPersonMeleeAttackMontage() == PreTP);
	TestTrue(TEXT("PreSuit switch selected"), Shooter->GetThirdPersonSwitchMontage() == PreTP);
	Shooter->PreSuitMontages.FirstPersonFire = nullptr;
	Shooter->FirstPersonFireMontage = PreFP;
	TestTrue(TEXT("Compatible legacy montage fallback works"), Shooter->GetActionMontage(EShooterMontageAction::Fire, true) == PreFP);
	UAnimMontage* Invalid = MakeMontage(NewObject<USkeleton>(GetTransientPackage()));
	Shooter->PreSuitMontages.FirstPersonFire = Invalid;
	AddExpectedError(TEXT("rejected incompatible 1P montage"), EAutomationExpectedErrorFlags::Contains, 2);
	TestNull(TEXT("Invalid explicit montage does not silently fallback"), Shooter->GetActionMontage(EShooterMontageAction::Fire, true));
	Shooter->PreSuitMontages.FirstPersonFire = nullptr;
	Shooter->FirstPersonFireMontage = Invalid;
	TestNull(TEXT("Invalid legacy montage is not played"), Shooter->GetActionMontage(EShooterMontageAction::Fire, true));

	// 실제 연결했던 인스턴스/몽타주를 보존하고, 상태 교체 시 종료 콜백을 먼저 제거한다.
	UShooterCombatComponent* Combat = Shooter->CombatComponent;
	Combat->BindReloadMontageEndedDelegates();
	TestTrue(TEXT("Reload captures selected PreSuit montage"), Combat->ActiveFirstPersonReloadMontage.Get() == PreFP);
	TestTrue(TEXT("Reload captures actual FP instance"), Combat->BoundFirstPersonReloadInstance.Get() == FP);
	Combat->bIsReloading = true;
	Shooter->BeginActionLock(EShooterActionLock::Reload);
	Shooter->SetSuitPresentation(true);
	TestFalse(TEXT("Old reload is cancelled"), Combat->IsReloading());
	TestFalse(TEXT("Old reload instance binding is cleared"), Combat->BoundFirstPersonReloadInstance.IsValid());
	Combat->HandleReloadMontageEnded(PreFP, false);
	TestFalse(TEXT("Old montage completion cannot restore reload"), Combat->IsReloading());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierSuitTransitionTest,
	"Outlier.Interaction.Suit.Transition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSuitTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedSuitInteractionTestWorld TestWorld;
	if (!TestWorld.Initialize(*this)) { return false; }
	FShooterPresentationConfiguration Configuration;
	if (!LoadSuitPresentationConfiguration(*this, Configuration)) { return false; }
	UWorld* World = TestWorld.World;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActorDeferred<AShooterCharacter>(
		ShooterClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
	if (!TestNotNull(TEXT("Shooter spawns"), Shooter)) { return false; }
	Shooter->PreSuitPresentation = Configuration;
	Shooter->SuitPresentation = Configuration;
	Shooter->FinishSpawning(FTransform::Identity);
	APartnerCharacter* Partner = PartnerClass ? World->SpawnActor<APartnerCharacter>(PartnerClass) : nullptr;
	ASuitTransitionTestPlayerController* ShooterController = World->SpawnActor<ASuitTransitionTestPlayerController>();
	ASuitTransitionTestPlayerController* PartnerController = World->SpawnActor<ASuitTransitionTestPlayerController>();
	ASuitTransitionTestPlayerController* OtherController = World->SpawnActor<ASuitTransitionTestPlayerController>();
	AOutlierPlayerState* ShooterPS = World->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerPS = World->SpawnActor<AOutlierPlayerState>();
	if (!Partner || !ShooterController || !PartnerController || !OtherController || !ShooterPS || !PartnerPS)
	{
		AddError(TEXT("Transition participants did not spawn"));
		return false;
	}
	if (!Shooter->HasActorBegunPlay()) { Shooter->DispatchBeginPlay(); }
	if (!Partner->HasActorBegunPlay()) { Partner->DispatchBeginPlay(); }
	ShooterController->Possess(Shooter);
	PartnerController->Possess(Partner);
	Shooter->SetPlayerState(ShooterPS);
	Partner->SetPlayerState(PartnerPS);
	ShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
	ShooterPS->SetPairId(0);
	PartnerPS->SetPairId(0);
	ShooterPS->SetShooterCharacter(Shooter);
	ShooterPS->SetPartnerCharacter(Partner);
	PartnerPS->SetShooterCharacter(Shooter);
	PartnerPS->SetPartnerCharacter(Partner);
	ASuitInteractionTestActor* Suit = World->SpawnActorDeferred<ASuitInteractionTestActor>(
		ASuitInteractionTestActor::StaticClass(), FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Suit spawns"), Suit)) { return false; }
	Suit->Configure(NewObject<UStaticMesh>(GetTransientPackage()), Configuration.FirstPersonMesh, Configuration.ThirdPersonMesh);
	Suit->FinishSpawning(FTransform::Identity);
	if (!Suit->HasActorBegunPlay()) { Suit->DispatchBeginPlay(); }
	Shooter->SuitFadeOutDuration = 0.2f;
	Shooter->SuitBlackHoldDuration = 0.0f;
	Shooter->SuitFadeInDuration = 0.0f;
	Shooter->SuitTransitionResponseTimeout = 0.5f;
	AWeaponBase* StoredRifle = ReadObjectProperty<AWeaponBase>(Suit, TEXT("StoredShooterRifle"));
	AWeaponBase* StoredPartnerWeapon = ReadObjectProperty<AWeaponBase>(Suit, TEXT("StoredPartnerWeapon"));
	if (!StoredRifle || !StoredPartnerWeapon) { AddError(TEXT("Stored weapons did not spawn")); return false; }

	Shooter->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Non-authority request cannot reserve"), Suit->Interact(Shooter));
	TestFalse(TEXT("Non-authority request grants nothing"), ShooterPS->GetAcquiredSuit());
	Shooter->SetRole(ROLE_Authority);
	Shooter->SuitFadeOutDuration = -1.0f;
	TestFalse(TEXT("Negative timing rejects reservation"), Suit->Interact(Shooter));
	Shooter->SuitFadeOutDuration = 0.2f;
	Shooter->SuitTransitionResponseTimeout = Shooter->SuitFadeOutDuration;
	TestFalse(TEXT("Timeout equal to minimum duration rejects reservation"), Suit->Interact(Shooter));
	Shooter->SuitTransitionResponseTimeout = 0.5f;
	ShooterController->bTestLocalController = false;
	ShooterController->ClientSetSuitTransitionPhase_Implementation(Shooter, FGuid::NewGuid(), ESuitTransitionPhase::FadingOut, 0.2f);
	TestTrue(TEXT("A non-local controller does not start local presentation"),
		ShooterController->FadeOutRequests == 0 && !ShooterController->LocalSuitTransitionId.IsValid());
	ShooterController->bTestLocalController = true;
	TestTrue(TEXT("Valid request reserves"), Suit->Interact(Shooter));
	FGuid Id = Shooter->GetSuitTransitionId();
	TestTrue(TEXT("Only participating controllers receive fade out requests"),
		ShooterController->FadeOutRequests == 1 && PartnerController->FadeOutRequests == 1 && OtherController->FadeOutRequests == 0);
	TestTrue(TEXT("Controllers cache the current ID without manufacturing ready responses"),
		ShooterController->LocalSuitTransitionId == Id && PartnerController->LocalSuitTransitionId == Id
		&& !Shooter->bSuitTransitionShooterReady && !Shooter->bSuitTransitionPartnerReady);
	TestEqual(TEXT("Fade out request uses the server duration"), ShooterController->LastFadeDuration, Shooter->SuitFadeOutDuration);
	TestTrue(TEXT("Reserved participants remain valid while transition owns their blocks"),
		Shooter->ValidateSuitTransitionParticipants(false));
	TestFalse(TEXT("Reserved participants cannot be treated as a new reservation"),
		Shooter->ValidateSuitTransitionParticipants(true));
	Shooter->SuitTransitionPartnerController = OtherController;
	TestFalse(TEXT("Different controller ownership invalidates participants"),
		Shooter->ValidateSuitTransitionParticipants(false));
	Shooter->SuitTransitionPartnerController = PartnerController;
	TestTrue(TEXT("Restored controller ownership validates participants"),
		Shooter->ValidateSuitTransitionParticipants(false));
	TestFalse(TEXT("Duplicate interaction rejects"), Suit->Interact(Shooter));
	TestTrue(TEXT("Both participants blocked"), Shooter->IsSuitTransitionBlocked() && Partner->IsSuitTransitionBlocked());
	TestFalse(TEXT("Shooter cannot interact"), Shooter->CanInteract());
	TestFalse(TEXT("Partner cannot accept input"), Partner->CanAcceptInput());
	TestFalse(TEXT("Equip requests are blocked"), Shooter->CanStartAction(EShooterActionLock::Equip));
	TestFalse(TEXT("Reload requests are blocked"), Shooter->CanReloadInCurrentState());
	TestFalse(TEXT("Suit abilities are blocked"), Shooter->IsSuitUsable());
	Shooter->ReleaseSuitTransitionBlock(FGuid::NewGuid());
	TestTrue(TEXT("Foreign handle cannot unlock"), Shooter->IsSuitTransitionBlocked());
	Shooter->AcknowledgeSuitTransition(OtherController, Id, ESuitTransitionPhase::FadingOut);
	Shooter->AcknowledgeSuitTransition(ShooterController, FGuid::NewGuid(), ESuitTransitionPhase::FadingOut);
	Shooter->AcknowledgeSuitTransition(ShooterController, Id, ESuitTransitionPhase::Applying);
	TestFalse(TEXT("Invalid ready responses ignored"), Shooter->bSuitTransitionShooterReady);
	OtherController->ServerNotifySuitTransitionPhaseFinished_Implementation(Shooter, Id, ESuitTransitionPhase::FadingOut);
	ShooterController->NotifySuitFadeOutFinished(FGuid::NewGuid());
	ShooterController->NotifySuitPresentationReady(Id);
	ShooterController->NotifySuitFadeInFinished(Id);
	TestFalse(TEXT("Controller transport rejects foreign sender, old ID and wrong local phase"), Shooter->bSuitTransitionShooterReady);
	ShooterController->SetRole(ROLE_SimulatedProxy);
	ShooterController->ServerNotifySuitTransitionPhaseFinished_Implementation(Shooter, Id, ESuitTransitionPhase::FadingOut);
	TestFalse(TEXT("Non-authority completion cannot change server readiness"), Shooter->bSuitTransitionShooterReady);
	ShooterController->SetRole(ROLE_Authority);
	ShooterController->NotifySuitFadeOutFinished(Id);
	ShooterController->NotifySuitFadeOutFinished(Id);
	ShooterController->ClientSetSuitTransitionPhase_Implementation(Shooter, Id, ESuitTransitionPhase::FadingOut, Shooter->SuitFadeOutDuration);
	TestTrue(TEXT("Duplicate phase request neither restarts fade nor resets its sent response"),
		ShooterController->FadeOutRequests == 1 && ShooterController->bLocalSuitTransitionReadySent);
	TestFalse(TEXT("Duplicate Shooter ready is not Partner ready"), Shooter->bSuitTransitionPartnerReady);
	PartnerController->NotifySuitFadeOutFinished(Id);
	TestFalse(TEXT("Readiness cannot bypass minimum server duration"), ShooterPS->GetAcquiredSuit());
	TestTrue(TEXT("Still fading out"), Shooter->GetSuitTransitionPhase() == ESuitTransitionPhase::FadingOut);
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	ShooterController->bNotifyDuringCleanup = true;
	Shooter->CancelSuitTransition();
	ShooterController->bNotifyDuringCleanup = false;
	TestTrue(TEXT("Cancel invalidates both local IDs before cleanup callbacks"),
		!ShooterController->LocalSuitTransitionId.IsValid() && !PartnerController->LocalSuitTransitionId.IsValid());
	TestTrue(TEXT("Cancel cleans up only the participating controllers"),
		ShooterController->CleanupRequests == 1 && PartnerController->CleanupRequests == 1 && OtherController->CleanupRequests == 0);
	TestTrue(TEXT("Cleanup preserves a separately assigned action lock"), Shooter->GetActionLock() == EShooterActionLock::Equip);
	Shooter->EndActionLock(EShooterActionLock::Equip);
	TestFalse(TEXT("Cancel releases both blocks"), Shooter->IsSuitTransitionBlocked() || Partner->IsSuitTransitionBlocked());
	TestTrue(TEXT("Cancel preserves stored weapons"), StoredRifle->IsHidden() && StoredPartnerWeapon->IsHidden());
	TestFalse(TEXT("Cancel does not acquire"), ShooterPS->GetAcquiredSuit());
	TestTrue(TEXT("Cancel releases reservation"), Suit->CanReserveFor(Shooter));

	const int32 PartnerFadeOutRequestsBeforeCancel = PartnerController->FadeOutRequests;
	ShooterController->bCancelDuringFadeOut = true;
	TestTrue(TEXT("Immediate local cancellation starts from a valid reservation"), Suit->Interact(Shooter));
	ShooterController->bCancelDuringFadeOut = false;
	TestTrue(TEXT("A reentrant local cancellation cannot send a stale phase to the other controller"),
		!Shooter->GetSuitTransitionId().IsValid()
		&& PartnerController->FadeOutRequests == PartnerFadeOutRequestsBeforeCancel
		&& !ShooterController->LocalSuitTransitionId.IsValid() && !PartnerController->LocalSuitTransitionId.IsValid());
	TestTrue(TEXT("Immediate local cancellation preserves acquisition and retry policy"),
		!ShooterPS->GetAcquiredSuit() && Suit->CanReserveFor(Shooter));

	TestTrue(TEXT("Unconnected transition can start"), Suit->Interact(Shooter));
	const FGuid UnconnectedId = Shooter->GetSuitTransitionId();
	ShooterController->NotifySuitFadeOutFinished(Id);
	ShooterController->ClientSetSuitTransitionPhase_Implementation(Shooter, Id, ESuitTransitionPhase::Idle, 0.0f);
	TestTrue(TEXT("Old callback and old cleanup cannot affect a new transition"),
		ShooterController->LocalSuitTransitionId == UnconnectedId && !Shooter->bSuitTransitionShooterReady);
	// 대괄호가 포함된 로그 접두사는 정규식이 아닌 일반 문자열로 비교한다.
	AddExpectedErrorPlain(TEXT("[SuitTransition] Timeout"), EAutomationExpectedErrorFlags::Contains, 2);
	const uint64 FrameCounterBeforeAdvance = GFrameCounter;
	TestWorld.AdvanceTime(0.6f);
	TestTrue(TEXT("Test clock restores the engine frame counter"), GFrameCounter == FrameCounterBeforeAdvance);
	TestTrue(TEXT("Unconnected timeout returns Idle"), Shooter->GetSuitTransitionPhase() == ESuitTransitionPhase::Idle);
	TestFalse(TEXT("Timeout grants nothing"), ShooterPS->GetAcquiredSuit());
	TestNull(TEXT("Timeout does not equip Rifle"), Shooter->GetCurrentWeapon());
	TestTrue(TEXT("Timeout makes reservation retryable"), Suit->CanReserveFor(Shooter));
	TestTrue(TEXT("Unconnected timeout cleans up both local transitions"),
		!ShooterController->LocalSuitTransitionId.IsValid() && !PartnerController->LocalSuitTransitionId.IsValid()
		&& ShooterController->LastCleanupId == UnconnectedId && PartnerController->LastCleanupId == UnconnectedId);

	TestTrue(TEXT("Pair departure starts from reserved state"), Suit->Interact(Shooter));
	PartnerPS->SetPairId(1);
	TestFalse(TEXT("Pair departure cancels immediately"), Shooter->GetSuitTransitionId().IsValid());
	PartnerPS->SetPairId(0);
	TestTrue(TEXT("Controller departure starts from reserved state"), Suit->Interact(Shooter));
	PartnerController->UnPossess();
	TestFalse(TEXT("Controller departure cancels immediately"), Shooter->GetSuitTransitionId().IsValid());
	TestFalse(TEXT("Pawn departure clears the local transition"), PartnerController->LocalSuitTransitionId.IsValid());
	PartnerController->Possess(Partner);
	Partner->SetPlayerState(PartnerPS);

	TestTrue(TEXT("Reboot case reserves"), Suit->Interact(Shooter));
	UAbilitySystemComponent* PartnerASC = Partner->GetAbilitySystemComponent();
	PartnerASC->AddLooseGameplayTag(OutlierGameplayTags::State::Rebooting());
	TestFalse(TEXT("Reboot cancels transition"), Shooter->GetSuitTransitionId().IsValid());
	TestFalse(TEXT("Transition cleanup does not release Reboot"), Partner->CanAcceptInput());
	PartnerASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::Rebooting());

	Shooter->SuitFadeOutDuration = 0.0f;
	TestTrue(TEXT("Configuration change case reserves"), Suit->Interact(Shooter));
	Id = Shooter->GetSuitTransitionId();
	Shooter->SuitPresentation.ThirdPersonAnimClass = nullptr;
	Shooter->AcknowledgeSuitTransition(ShooterController, Id, ESuitTransitionPhase::FadingOut);
	Shooter->AcknowledgeSuitTransition(PartnerController, Id, ESuitTransitionPhase::FadingOut);
	TestFalse(TEXT("Changed configuration cancels before grants"), Shooter->GetSuitTransitionId().IsValid());
	TestFalse(TEXT("Changed configuration does not acquire"), ShooterPS->GetAcquiredSuit());
	TestNull(TEXT("Changed configuration does not equip"), Shooter->GetCurrentWeapon());
	Shooter->SuitPresentation = Configuration;
	TestTrue(TEXT("Rejected commit releases reservation"), Suit->CanReserveFor(Shooter));

	ASuitInteractionTestActor* RemovedSuit = World->SpawnActorDeferred<ASuitInteractionTestActor>(
		ASuitInteractionTestActor::StaticClass(), FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Removable Interaction spawns"), RemovedSuit)) { return false; }
	RemovedSuit->Configure(NewObject<UStaticMesh>(GetTransientPackage()), Configuration.FirstPersonMesh, Configuration.ThirdPersonMesh);
	RemovedSuit->FinishSpawning(FTransform::Identity);
	if (!RemovedSuit->HasActorBegunPlay()) { RemovedSuit->DispatchBeginPlay(); }
	TestTrue(TEXT("Interaction EndPlay case reserves"), RemovedSuit->Interact(Shooter));
	RemovedSuit->Destroy();
	TestFalse(TEXT("Pre-commit Interaction EndPlay cancels"), Shooter->GetSuitTransitionId().IsValid());
	TestFalse(TEXT("Pre-commit EndPlay releases both blocks"), Shooter->IsSuitTransitionBlocked() || Partner->IsSuitTransitionBlocked());
	TestFalse(TEXT("Pre-commit EndPlay grants nothing"), ShooterPS->GetAcquiredSuit());

	UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent();
	ASuitInteractionTestRifle* OldRifle = World->SpawnActor<ASuitInteractionTestRifle>();
	ASuitInteractionTestRifle* Pistol = World->SpawnActor<ASuitInteractionTestRifle>();
	ASuitInteractionTestRifle* Melee = World->SpawnActor<ASuitInteractionTestRifle>();
	if (!OldRifle || !Pistol || !Melee) { AddError(TEXT("Existing loadout did not spawn")); return false; }
	Pistol->SetTestWeaponType(EWeaponType::Pistol);
	Melee->SetTestWeaponType(EWeaponType::Melee);
	for (AWeaponBase* Weapon : { OldRifle, Pistol, Melee })
	{
		Inventory->HandleEquipWeapon(Weapon);
		Shooter->CancelLocalProceduralWeaponSwitch();
		Shooter->EndActionLock(EShooterActionLock::Equip);
	}
	TestTrue(TEXT("Existing loadout occupies all slots"),
		Inventory->GetWeaponInSlot(EWeaponSlot::Primary) == OldRifle
		&& Inventory->GetWeaponInSlot(EWeaponSlot::Secondary) == Pistol
		&& Inventory->GetWeaponInSlot(EWeaponSlot::Melee) == Melee);

	// 최소 시간 경계는 위에서 검사했다. 이 구간은 명시적 완료 응답으로 commit 이후 수명을 검사한다.
	Shooter->SuitFadeOutDuration = 0.0f;
	TestTrue(TEXT("Commit case reserves"), Suit->Interact(Shooter));
	Id = Shooter->GetSuitTransitionId();
	ShooterController->NotifySuitFadeOutFinished(Id);
	PartnerController->NotifySuitFadeOutFinished(Id);
	TestTrue(TEXT("Commit acquires both PlayerStates"), ShooterPS->GetAcquiredSuit() && PartnerPS->GetAcquiredSuit());
	TestTrue(TEXT("Commit grants exact stored Rifle"), Shooter->GetCurrentWeapon() == StoredRifle);
	TestTrue(TEXT("Commit grants exact stored Partner weapon"), Partner->GetCurrentWeapon() == StoredPartnerWeapon);
	TestTrue(TEXT("Commit removes old Rifle instead of dropping it"), OldRifle->IsActorBeingDestroyed());
	TestTrue(TEXT("Commit preserves Pistol and Melee slots"),
		Inventory->GetWeaponInSlot(EWeaponSlot::Secondary) == Pistol
		&& Inventory->GetWeaponInSlot(EWeaponSlot::Melee) == Melee
		&& !Pistol->IsActorBeingDestroyed() && !Melee->IsActorBeingDestroyed());
	TestTrue(TEXT("Commit retains transition block while applying"), Shooter->IsSuitTransitionBlocked());
	Shooter->AcknowledgeSuitTransition(PartnerController, Id, ESuitTransitionPhase::FadingOut);
	TestTrue(TEXT("Old phase cannot regrant"), Shooter->GetCurrentWeapon() == StoredRifle);
	Suit->Destroy();
	TestTrue(TEXT("Consumed Interaction teardown does not own transition lifetime"), Shooter->GetSuitTransitionId() == Id);
	TestWorld.AdvanceTime(0.6f);
	TestFalse(TEXT("Post-commit timeout releases both blocks"), Shooter->IsSuitTransitionBlocked() || Partner->IsSuitTransitionBlocked());
	TestTrue(TEXT("Post-commit timeout keeps acquired state"), ShooterPS->GetAcquiredSuit() && PartnerPS->GetAcquiredSuit());
	TestTrue(TEXT("Post-commit timeout preserves grants"), Shooter->GetCurrentWeapon() == StoredRifle && Partner->GetCurrentWeapon() == StoredPartnerWeapon);
	TestTrue(TEXT("Post-commit timeout keeps Suit presentation"), Shooter->GetAppliedPresentation() == EShooterPresentation::Suit);
	TestTrue(TEXT("Post-commit timeout releases both local transitions without undoing presentation"),
		!ShooterController->LocalSuitTransitionId.IsValid() && !PartnerController->LocalSuitTransitionId.IsValid()
		&& ShooterController->LastCleanupId == Id && PartnerController->LastCleanupId == Id);
	ShooterController->NotifySuitPresentationReady(Id);
	Shooter->AcknowledgeSuitTransition(ShooterController, Id, ESuitTransitionPhase::Applying);
	TestFalse(TEXT("Late response cannot restart"), Shooter->GetSuitTransitionId().IsValid());

	// 종료된 서버 전환과 분리해 로컬 수명주기 경계만 검사한다. 이 요청은 완료 응답을 만들지 않는다.
	const FGuid LocalCleanupId = FGuid::NewGuid();
	ShooterController->ClientSetSuitTransitionPhase_Implementation(Shooter, LocalCleanupId, ESuitTransitionPhase::FadingOut, 0.2f);
	ShooterController->SetPawn(Shooter);
	TestTrue(TEXT("Setting the same Pawn preserves the active local transition"), ShooterController->LocalSuitTransitionId == LocalCleanupId);
	ShooterController->SetPawn(nullptr);
	TestTrue(TEXT("Pawn replacement clears the cached transition even without a server phase notification"),
		!ShooterController->LocalSuitTransitionId.IsValid() && ShooterController->LastCleanupId == LocalCleanupId);
	ShooterController->SetPawn(Shooter);
	ShooterController->ClientSetSuitTransitionPhase_Implementation(Shooter, LocalCleanupId, ESuitTransitionPhase::FadingOut, 0.2f);
	ShooterController->ClientPrepareForArenaExit_Implementation();
	TestTrue(TEXT("Arena exit clears the local transition before leaving play"),
		!ShooterController->LocalSuitTransitionId.IsValid() && ShooterController->LastCleanupId == LocalCleanupId);
	ShooterController->ClientSetSuitTransitionPhase_Implementation(Shooter, LocalCleanupId, ESuitTransitionPhase::FadingOut, 0.2f);
	const int32 CleanupRequestsBeforeEndPlay = ShooterController->CleanupRequests;
	ShooterController->EndPlay(EEndPlayReason::LevelTransition);
	ShooterController->NotifySuitFadeOutFinished(LocalCleanupId);
	TestTrue(TEXT("Controller EndPlay clears local callbacks exactly once"),
		!ShooterController->LocalSuitTransitionId.IsValid()
		&& ShooterController->CleanupRequests == CleanupRequestsBeforeEndPlay + 1);
	TestFalse(TEXT("EndPlay callback cannot restart the completed server transition"), Shooter->GetSuitTransitionId().IsValid());
	return true;
}

#endif
