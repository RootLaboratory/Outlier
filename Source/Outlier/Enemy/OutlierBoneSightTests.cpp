#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/OutlierBoneSight.h"
#include "Enemy/OutlierSightBoneCache.h"
#include "Enemy/VECDrone.h"
#include "Enemy/SelfDestructDrone.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "StateTree.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "ReferenceSkeleton.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Weapon/WeaponBase.h"

namespace OutlierBoneSightTests
{
	struct FWorld
	{
		UWorld* World;
		UGameInstance* Instance = nullptr;
		FWorld(bool bDedicated = false)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("BoneSightWorld")));
			World->AddToRoot();
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.RunAsDedicated = bDedicated;
			Context.SetCurrentWorld(World);
			if (bDedicated)
			{
				Instance = NewObject<UGameInstance>(GEngine);
				World->SetGameInstance(Instance);
				Instance->Init();
			}
			World->CreateAISystem();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
			if (bDedicated) World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			if (bDedicated)
			{
				URL.Port = 0;
				World->Listen(URL);
				World->BeginPlay();
			}
		}
		~FWorld()
		{
			GEngine->ShutdownWorldNetDriver(World);
			if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(true);
			if (Instance) Instance->Shutdown();
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};

	USkeletalMesh* LoadBody()
	{
		return LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/1P/Workwear/3_Meshes/M_Player1_3.M_Player1_3"));
	}

	USkeletalMeshComponent* MakeMesh(AActor& Owner, USkeletalMesh* Asset)
	{
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(&Owner);
		Owner.SetRootComponent(Mesh);
		Mesh->SetSkeletalMeshAsset(Asset);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierBoneSightOcclusionTest,
	"Outlier.Enemy.SightRuntime.OcclusionAndLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierBoneSightOcclusionTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	FWorld Fixture;
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	USkeletalMesh* Asset = LoadBody();
	if (!TestNotNull(TEXT("Actual body mesh"), Asset) || !Target) return false;
	USkeletalMeshComponent* Mesh = MakeMesh(*Target, Asset);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
	Mesh->bEnableUpdateRateOptimizations = true;
	FOutlierBoneSight Sight;
	FOutlierSightBoneCache Registration;
	FCanBeSeenFromContext Context;
	Context.ObserverLocation = FVector(1000, 1000, 300);
	FVector Seen;
	int32 Sync = 0, Async = 0;
	float Strength = 0;
	const auto Query = [&]
	{
		Sync = Async = 0;
		return Sight.Query(*Target, Mesh, Registration, Context, Seen, Sync, Async, Strength);
	};
	for (const int32 Count : { 5, 6 })
	{
		const TArray<FName> All = { TEXT("head"), TEXT("pelvis"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") };
		const TConstArrayView<FName> Names(All.GetData(), Count);
		if (!TestTrue(TEXT("Actual bone indices resolve"), Registration.Resolve(Asset, Names))) continue;
		TestTrue(TEXT("First query waits for a completed pose"), Query() == UAISense_Sight::EVisibilityResult::NotVisible);
		TestEqual(TEXT("Warmup does not trace"), Sync, 0);
		TestTrue(TEXT("A query requests full pose refresh"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
		Mesh->RefreshBoneTransforms();
		TestTrue(TEXT("Unobstructed real bones are visible"), Query() == UAISense_Sight::EVisibilityResult::Visible);
		TestEqual(TEXT("Every real trace is reported"), Sync, Count);
		TestEqual(TEXT("No async traces invented"), Async, 0);
		TestEqual(TEXT("Full exposure strength"), Strength, 1.0f);

		TArray<UBoxComponent*> Blockers;
		const int32 Required = Count / 2 + Count % 2;
		// 각 광선의 중점에 작은 실제 충돌체를 둔다. 한 점 성공이 전체 판정을 우회하는지 검사한다.
		for (int32 Index = 0; Index < Count - Required + 1; ++Index)
		{
			AActor* Wall = Fixture.World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
			Wall->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(0.02));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Box->SetWorldLocation((Context.ObserverLocation + Mesh->GetBoneTransform(Registration.GetBoneIndices()[Index]).GetLocation()) * 0.5);
			Box->RegisterComponent();
			Blockers.Add(Box);
		}
		TestTrue(TEXT("One below half fails despite other visible bones"), Query() == UAISense_Sight::EVisibilityResult::NotVisible);
		TestEqual(TEXT("Occlusion traces reported exactly"), Sync, Count);
		Blockers.Last()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 Bone : Registration.GetBoneIndices())
		{
			FHitResult Hit;
			Fixture.World->LineTraceSingleByChannel(Hit, Context.ObserverLocation, Mesh->GetBoneTransform(Bone).GetLocation(), ECC_Visibility);
			AddInfo(FString::Printf(TEXT("Count=%d Bone=%s Hit=%s Point=%s"), Count, *Mesh->GetBoneName(Bone).ToString(), *GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToString()));
		}
		TestTrue(TEXT("Exactly ceil half succeeds"), Query() == UAISense_Sight::EVisibilityResult::Visible);
		TestEqual(TEXT("Threshold strength"), Strength, static_cast<float>(Required) / Count);
		for (UBoxComponent* Box : Blockers) Box->GetOwner()->Destroy();
		for (int32 Observer = 0; Observer < 20; ++Observer) Query();
		TestTrue(TEXT("Observers share a single lease"), Sight.IsPoseRefreshRequested());
		for (int32 Step = 0; Step < 15; ++Step)
		{
			++GFrameCounter;
			Fixture.World->GetTimerManager().Tick(0.1f);
		}
		TestFalse(TEXT("Idle target releases pose request"), Sight.IsPoseRefreshRequested());
		TestTrue(TEXT("Original mesh policy restored"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered);
		TestTrue(TEXT("Original URO restored"), Mesh->bEnableUpdateRateOptimizations);
	}
	Sight.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierBoneSightDefaultsTest,
	"Outlier.Enemy.SightRuntime.ProjectDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierBoneSightDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	TestFalse(TEXT("Unrelated hacked turrets retain their existing default sight path"),
		AAutoTurret::StaticClass()->ImplementsInterface(UAISightTargetInterface::StaticClass()));
	const TCHAR* Classes[] = {
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"),
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C") };
	for (const TCHAR* Path : Classes)
	{
		UClass* Class = LoadClass<AActor>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		AActor* Default = Cast<AActor>(Class->GetDefaultObject());
		const FOutlierSightBoneCache* Cache = nullptr;
		USkeletalMeshComponent* Mesh = nullptr;
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(Default)) { Cache = &Shooter->ResolveSightBoneRegistration(); Mesh = Shooter->GetMesh(); }
		else if (APartnerCharacter* Partner = Cast<APartnerCharacter>(Default)) { Cache = &Partner->ResolveSightBoneRegistration(); Mesh = Partner->GetMesh(); }
		else if (AEnemyBase* Enemy = Cast<AEnemyBase>(Default)) { Cache = &Enemy->ResolveSightBoneRegistration(); Mesh = Enemy->GetMesh(); }
		if (!Cache || !Mesh || !TestTrue(TEXT("Actual BP inherits valid nonempty defaults"), Cache->IsValid())) continue;
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		TArray<FVector> Points;
		for (int32 Index : Cache->GetBoneIndices())
		{
			FTransform Pose = FTransform::Identity;
			for (int32 Bone = Index; Bone != INDEX_NONE; Bone = Skeleton.GetParentIndex(Bone)) Pose = Pose * Skeleton.GetRefBonePose()[Bone];
			Points.Add(Pose.GetLocation());
			AddInfo(FString::Printf(TEXT("Default=%s Bone=%s RefPosition=%s"), Path, *Skeleton.GetBoneName(Index).ToString(), *Pose.GetLocation().ToString()));
		}
		for (int32 A = 0; A < Points.Num(); ++A)
			for (int32 B = A + 1; B < Points.Num(); ++B)
				TestTrue(TEXT("Default samples occupy distinct positions"), !Points[A].Equals(Points[B], 1.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierBoneSightPerceptionTest,
	"Outlier.Enemy.SightRuntime.RealPerception",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierBoneSightPerceptionTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	FWorld Fixture(true);
	if (!TestTrue(TEXT("Perception integration runs on a dedicated server"), Fixture.World->GetNetMode() == NM_DedicatedServer)) return false;
	UClass* TargetClass = LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Actual target BP"), TargetClass)) return false;
	AVECDrone* Target = Fixture.World->SpawnActor<AVECDrone>(TargetClass);
	AAIController* Observer = Fixture.World->SpawnActor<AAIController>();
	if (!Target || !Observer) return false;
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Enemy/VECDrone/Meshes/SKM_Drone_Rifle.SKM_Drone_Rifle"));
	if (!TestNotNull(TEXT("Actual drone body"), Asset)) return false;
	Target->GetMesh()->SetSkeletalMeshAsset(Asset);
	Target->GetMesh()->SetAnimInstanceClass(nullptr);
	Target->SetActorLocation(FVector(500, 0, 0));
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!ShooterClass || !PartnerClass) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(500, 300, 0), FRotator::ZeroRotator, Spawn);
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(500, -300, 0), FRotator::ZeroRotator, Spawn);
	if (!Shooter || !Partner) return false;
	for (ACharacter* Character : { static_cast<ACharacter*>(Shooter), static_cast<ACharacter*>(Partner) })
	{
		Character->GetMesh()->SetAnimInstanceClass(nullptr);
		Character->GetCharacterMovement()->DisableMovement();
	}
	Target->SetEnemyPossessed(true);
	UAIPerceptionComponent* Listener = NewObject<UAIPerceptionComponent>(Observer);
	Observer->SetPerceptionComponent(*Listener);
	UAISenseConfig_Sight* Config = NewObject<UAISenseConfig_Sight>(Listener);
	Config->SightRadius = Config->LoseSightRadius = 2000;
	Config->PeripheralVisionAngleDegrees = 180;
	Config->DetectionByAffiliation.bDetectEnemies = true;
	Config->DetectionByAffiliation.bDetectFriendlies = true;
	Config->DetectionByAffiliation.bDetectNeutrals = true;
	Listener->ConfigureSense(*Config);
	Listener->RegisterComponent();
	Listener->RequestStimuliListenerUpdate();
	UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("Real perception system"), Perception)) return false;
	TestTrue(TEXT("Register actual target source"), UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Target));
	UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Shooter);
	UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Partner);
	const auto Advance = [&]
	{
		for (int32 Step = 0; Step < 12; ++Step)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_All, 0.05f);
			Perception->Tick(0.05f);
		}
	};
	Advance();
	TArray<AActor*> Seen;
	Listener->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
	TestTrue(TEXT("Actual Sight interface produces a perceived actor"), Seen.Contains(Target));
	TestTrue(TEXT("Shooter is perceived through its bone interface"), Seen.Contains(Shooter));
	TestTrue(TEXT("Partner is perceived through its bone interface"), Seen.Contains(Partner));
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(10, 1000, 1000));
	Box->SetWorldLocation(FVector(250, 0, 0));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Advance();
	Seen.Reset();
	Listener->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
	TestFalse(TEXT("Real wall removes perceived sight without injected stimuli"), Seen.Contains(Target));
	TestFalse(TEXT("Real wall hides Shooter"), Seen.Contains(Shooter));
	TestFalse(TEXT("Real wall hides Partner"), Seen.Contains(Partner));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAlertIntegrationTest,
	"Outlier.Enemy.SightRuntime.AlertAccumulationAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAlertIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	FWorld Fixture;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!Enemy || !Controller || !ShooterClass || !PartnerClass) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(500, 300, 0), FRotator::ZeroRotator, Spawn);
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(500, -300, 0), FRotator::ZeroRotator, Spawn);
	if (!Shooter || !Partner) return false;
	Controller->Possess(Enemy);
	const auto Stimulate = [&](AActor* Target, bool bVisible)
	{
		const FAIStimulus Stimulus(*GetDefault<UAISense_Sight>(), 1.0f,
			Target->GetActorLocation(), Enemy->GetActorLocation(),
			bVisible ? FAIStimulus::SensingSucceeded : FAIStimulus::SensingFailed);
		Controller->EnemyPerceptionComponent->RegisterStimulus(Target, Stimulus);
		Controller->EnemyPerceptionComponent->ProcessStimuli();
	};
	const auto Advance = [&](float Seconds)
	{
		// World의 큰 dt 제한에 걸리지 않도록 실제 서버 갱신 간격으로 시간을 진행한다.
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.025f));
		const float Step = Seconds / Steps;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, Step);
			Controller->ResolveInitialAwarenessAlert();
		}
	};
	const auto Progress = [&](AActor* Target)
	{
		const auto* State = Controller->AwarenessTargets.Find(Target);
		return State ? State->Progress.GetAwareness() : 0.0;
	};
	Stimulate(Shooter, true);
	Advance(0.6f);
	TestTrue(TEXT("Controller integrates 60% from world time"), FMath::IsNearlyEqual(Progress(Shooter), 0.6, 0.00001));
	for (int32 Index = 0; Index < 100; ++Index) Controller->ResolveInitialAwarenessAlert();
	TestTrue(TEXT("Repeated task evaluations cannot advance awareness"), FMath::IsNearlyEqual(Progress(Shooter), 0.6, 0.00001));
	Stimulate(Shooter, false);
	Advance(0.4f);
	TestTrue(TEXT("Concealment leaves 40% instead of resetting"), FMath::IsNearlyEqual(Progress(Shooter), 0.4, 0.00001));
	Stimulate(Partner, true);
	TestEqual(TEXT("Partner starts its own progress"), Progress(Partner), 0.0);
	Advance(0.2f);
	TestTrue(TEXT("Shooter decays while Partner accumulates"), FMath::IsNearlyEqual(Progress(Shooter), 0.3, 0.00001)
		&& FMath::IsNearlyEqual(Progress(Partner), 0.2, 0.00001));
	Controller->ForgetDetectionTarget(Partner);
	TestEqual(TEXT("Forgetting a target removes its progress"), Progress(Partner), 0.0);
	Advance(0.61f);
	TestTrue(TEXT("Zero awareness clears initial Alert before the old five-second wait"), Enemy->GetCombatState() == EEnemyCombatState::NonCombat);
	TestFalse(TEXT("Cleared lifetime cannot issue another transition"), Controller->ResolveInitialAwarenessAlert());

	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy->GetRuntimeStat());
	Stat.AwarenessDecayDuration = 4.0f;
	Stimulate(Shooter, true);
	Advance(0.4f);
	Stimulate(Shooter, false);
	Advance(0.4f);
	TestTrue(TEXT("Runtime stat changes the linear decay rate"), FMath::IsNearlyEqual(Progress(Shooter), 0.3, 0.00001));
	Controller->ForgetDetectionTarget(Shooter);
	Controller->ResolveInitialAwarenessAlert();
	TestTrue(TEXT("Removed last target cannot retain Alert"), Enemy->GetCombatState() == EEnemyCombatState::NonCombat);
	Stat.AwarenessDecayDuration = 2.0f;

	Stimulate(Shooter, true);
	Advance(0.2f);
	Enemy->EnterStun();
	TestEqual(TEXT("Stun immediately discards the previous exposure lifetime"), Controller->AwarenessTargets.Num(), 0);
	Stimulate(Shooter, false);
	Advance(0.4f);
	Enemy->RestoreStateAfterStun();
	Controller->ResolveInitialAwarenessAlert();
	TestTrue(TEXT("Hidden stun recovery cannot reuse old exposure or add a five-second hold"), Enemy->GetCombatState() == EEnemyCombatState::NonCombat);
	Stimulate(Partner, true);
	Advance(0.2f);
	Partner->Destroy();
	Controller->ResolveInitialAwarenessAlert();
	TestTrue(TEXT("Destroyed target cannot retain initial Alert"), Enemy->GetCombatState() == EEnemyCombatState::NonCombat);

	Stimulate(Shooter, true);
	Advance(0.6f);
	Stimulate(Shooter, false);
	Advance(0.4f);
	Stimulate(Shooter, true);
	Advance(0.61f);
	TestTrue(TEXT("Resumed awareness completes Combat through the existing commit boundary"), Enemy->IsInCombat());
	TestFalse(TEXT("Completion cannot be committed twice"), Controller->ResolveInitialAwarenessAlert());
	Controller->UnPossess();
	TestEqual(TEXT("Controller lifetime clears every target"), Controller->AwarenessTargets.Num(), 0);
	TestFalse(TEXT("Controller lifetime clears initial sight ownership"), Controller->bInitialAwarenessAlert);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHearingAwarenessIntegrationTest,
	"Outlier.Enemy.SightRuntime.HearingAccumulationAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHearingAwarenessIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	FWorld Fixture;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!Enemy || !Controller || !ShooterClass || !PartnerClass) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(500, 300, 0), FRotator::ZeroRotator, Spawn);
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(500, -300, 0), FRotator::ZeroRotator, Spawn);
	if (!Shooter || !Partner) return false;
	Controller->Possess(Enemy);
	const FVector HeardLocation(300, 100, 0);
	const auto Hear = [&](AActor* Target, float Age = 0.0f)
	{
		FAIStimulus Sound(*GetDefault<UAISense_Hearing>(), 1.0f, HeardLocation, Enemy->GetActorLocation());
		Sound.SetStimulusAge(Age);
		Controller->HandleHearingStimulus(Enemy, Target, Sound);
	};
	const auto See = [&](bool bVisible)
	{
		const FAIStimulus Sight(*GetDefault<UAISense_Sight>(), 1.0f, Shooter->GetActorLocation(), Enemy->GetActorLocation(),
			bVisible ? FAIStimulus::SensingSucceeded : FAIStimulus::SensingFailed);
		Controller->EnemyPerceptionComponent->RegisterStimulus(Shooter, Sight);
		Controller->EnemyPerceptionComponent->ProcessStimuli();
	};
	const auto Advance = [&](float Seconds)
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / 0.025f));
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_TimeOnly, Seconds / Steps);
			Controller->ResolveInitialAwarenessAlert();
		}
	};
	const auto Progress = [&](AActor* Target)
	{
		const auto* State = Controller->AwarenessTargets.Find(Target);
		return State ? State->Progress.GetAwareness() : 0.0;
	};
	// 조사 후보가 가까운 소리로 가득 차도 새로 들은 대상의 인지 시간은 시작한다.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector Nearby((Index & 1) ? 60 : -60, (Index & 2) ? 60 : -60, (Index & 4) ? 60 : -60);
		TestTrue(TEXT("Fill investigation candidates"), Enemy->RecordHeardNoise(Partner, Nearby, Fixture.World->GetTimeSeconds()));
	}
	Hear(Shooter);
	TestTrue(TEXT("Candidate admission cannot suppress hearing awareness"), Controller->AwarenessTargets.Contains(Shooter));
	Controller->ResetInitialAwareness();
	Enemy->ClearHeardNoise();
	See(true);
	Advance(0.4f);
	Hear(Shooter);
	Advance(0.1f);
	TestTrue(TEXT("Hearing does not add another 25% on top of sight"), FMath::IsNearlyEqual(Progress(Shooter), 0.5, 0.00001));
	See(false);
	Hear(Shooter);
	Advance(0.4f);
	TestTrue(TEXT("Hearing continues the same sight progress at quarter speed"), FMath::IsNearlyEqual(Progress(Shooter), 0.6, 0.00001));
	Hear(Partner);
	Advance(0.1f);
	TestTrue(TEXT("Partner hearing has its own progress"), FMath::IsNearlyEqual(Progress(Partner), 0.025, 0.00001));
	const double BeforeDuplicate = Progress(Shooter);
	for (int32 Index = 0; Index < 100; ++Index) { Hear(Shooter, 0.5f); Controller->ResolveInitialAwarenessAlert(); }
	TestEqual(TEXT("Old and duplicate inputs cannot multiply progress"), Progress(Shooter), BeforeDuplicate);
	Advance(0.2f);
	TestTrue(TEXT("Expiry splits hearing and decay inside the update"), FMath::IsNearlyEqual(Progress(Shooter), 0.525, 0.00001));
	Controller->UnPossess();
	TestEqual(TEXT("Controller reset clears both senses"), Controller->AwarenessTargets.Num(), 0);
	Controller->Possess(Enemy);
	Hear(Shooter, 0.6f);
	TestEqual(TEXT("A sound older than the hearing hold creates no awareness"), Controller->AwarenessTargets.Num(), 0);
	Shooter->SetActorLocation(FVector(10000, 0, 0));
	for (int32 Step = 0; Step < 16; ++Step)
	{
		Hear(Shooter);
		Advance(0.25f);
		if (Step < 15) TestFalse(TEXT("Continuous hearing cannot complete before four seconds"), Enemy->IsInCombat());
	}
	if (!Enemy->IsInCombat()) Advance(0.025f);
	TestTrue(TEXT("Hearing alone completes Combat"), Enemy->IsInCombat());
	TestTrue(TEXT("Completion remembers sound position instead of current actor position"), Enemy->GetLastKnownPlayerLocation().Equals(HeardLocation));
	TestFalse(TEXT("Hearing completion does not invent direct visibility"), Enemy->IsPlayerCurrentlyVisible());
	TestFalse(TEXT("Hearing completion does not grant hidden actor tracking"), Enemy->HasTrackableTarget());
	TestTrue(TEXT("Hearing completion enters existing investigation"), Enemy->IsNoiseInvestigationActive());
	TestTrue(TEXT("Investigation goal uses the heard coordinate"), Enemy->GetPerceptionGoal().Location.Equals(HeardLocation));
	TestFalse(TEXT("Completion cannot repeat"), Controller->ResolveInitialAwarenessAlert());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHearingStateTreeTest,
	"Outlier.Enemy.SightRuntime.DedicatedHearingStateTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHearingStateTreeTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	for (const TCHAR* Path : {
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"),
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C") })
	{
		FWorld Fixture(true);
		UClass* EnemyClass = LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		UClass* TargetClass = LoadClass<ACharacter>(nullptr, Path);
		if (!EnemyClass || !TargetClass) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVECDrone* Enemy = Fixture.World->SpawnActor<AVECDrone>(EnemyClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		ACharacter* Target = Fixture.World->SpawnActor<ACharacter>(TargetClass, FVector(500, 0, 0), FRotator::ZeroRotator, Spawn);
		if (!Enemy || !Target) return false;
		if (AEnemyBase* Possessed = Cast<AEnemyBase>(Target)) Possessed->SetEnemyPossessed(true);
		for (ACharacter* Actor : { static_cast<ACharacter*>(Enemy), Target })
		{
			Actor->GetCharacterMovement()->DisableMovement();
			Actor->GetMesh()->SetAnimInstanceClass(nullptr);
		}
		AActor* Wall = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(10, 10000, 10000));
		Box->SetWorldLocation(FVector(250, 0, 0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
		UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
		if (!Tree || !Perception) return false;
		// 기본 청각 반경 500cm 안에 두되 벽 뒤 대상의 실제 좌표와는 구분한다.
		const FVector HeardLocation(400, 200, 0);
		for (int32 Frame = 0; Frame < 200 && !Enemy->IsInCombat(); ++Frame)
		{
			// 실제 Hearing 이벤트를 사용한다. 대상 위치와 소리 좌표를 나눠 숨은 대상 추적이 섞이지 않는지 확인한다.
			if (Frame % 10 == 0) UAISense_Hearing::ReportNoiseEvent(Fixture.World, HeardLocation, 1.0f, Target);
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_All, 0.025f);
			Perception->Tick(0.025f);
			TestFalse(TEXT("Occlusion prevents actual sight throughout hearing acquisition"), Enemy->IsPlayerCurrentlyVisible());
			if (Frame == 80) TestTrue(TEXT("Two seconds of actual hearing remain in Alert"), Enemy->GetCombatState() == EEnemyCombatState::Alert);
		}
		if (!Enemy->IsInCombat())
		{
			FEnemyHeardNoise Noise;
			AddInfo(FString::Printf(TEXT("Hearing fixture: Range=%.1f State=%s Enemy=%s Target=%s Heard=%d Tree=%s"),
				Enemy->GetRuntimeStat().HearingRange, *UEnum::GetValueAsString(Enemy->GetCombatState()),
				*Enemy->GetActorLocation().ToString(), *Target->GetActorLocation().ToString(), Enemy->TryGetHeardNoise(Noise),
				*FString::JoinBy(Tree->GetActiveStateNames(), TEXT("/"), [](FName Name) { return Name.ToString(); })));
		}
		if (!TestTrue(FString::Printf(TEXT("Actual Hearing and production Alert task reach Combat for %s"), Path), Enemy->IsInCombat())) return false;
		TestFalse(TEXT("Sound-only Combat has no tracked actor"), Enemy->HasTrackableTarget());
		TestTrue(TEXT("Sound-only Combat keeps the sound investigation"), Enemy->IsNoiseInvestigationActive());
		TestTrue(TEXT("Actual goal uses sound event location"), Enemy->GetPerceptionGoal().Location.Equals(HeardLocation));
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_All, 0.025f);
		}
		TestFalse(TEXT("Deferred Combat event exits actual Alert branch"), Tree->GetActiveStateNames().Contains(TEXT("Alert")));
		TestFalse(TEXT("No direct sight or actor tracking is created by Combat transition"), Enemy->IsPlayerCurrentlyVisible() || Enemy->HasTrackableTarget());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightStateTreeTest,
	"Outlier.Enemy.SightRuntime.DedicatedStateTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightStateTreeTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	// 자극 주입 없이 실제 본 Trace→Perception 콜백→공용 Alert Task→상태 이벤트까지 검증한다.
	for (const TCHAR* TargetPath : {
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"),
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"),
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C") })
	{
		FWorld Fixture(true);
		UClass* EnemyClass = LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
		UClass* TargetClass = LoadClass<ACharacter>(nullptr, TargetPath);
		if (!EnemyClass || !TargetClass) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVECDrone* Enemy = Fixture.World->SpawnActor<AVECDrone>(EnemyClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		ACharacter* Target = Fixture.World->SpawnActor<ACharacter>(TargetClass, FVector(500, 0, 0), FRotator::ZeroRotator, Spawn);
		if (!Enemy || !Target) return false;
		if (AEnemyBase* Possessed = Cast<AEnemyBase>(Target)) Possessed->SetEnemyPossessed(true);
		for (ACharacter* Actor : { static_cast<ACharacter*>(Enemy), Target })
		{
			Actor->GetCharacterMovement()->DisableMovement();
			Actor->GetMesh()->SetAnimInstanceClass(nullptr);
		}
		AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
		UStateTreeComponent* Tree = Enemy->GetStateTreeComponent();
		if (!TestNotNull(TEXT("Actual enemy AI controller"), Controller) || !TestNotNull(TEXT("Actual common StateTree component"), Tree)) return false;
		TestTrue(TEXT("Actual configured StateTree is running"), Tree->GetStateTreeRunStatus() == EStateTreeRunStatus::Running);
		UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
		UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Target);
		const auto Step = [&]
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_All, 0.025f);
			Perception->Tick(0.025f);
		};
		for (int32 Index = 0; Index < 40 && Enemy->GetCombatState() != EEnemyCombatState::Alert; ++Index) Step();
		if (!TestTrue(FString::Printf(TEXT("Actual bone perception enters Alert for %s"), TargetPath), Enemy->GetCombatState() == EEnemyCombatState::Alert)) return false;
		for (int32 Index = 0; Index < 16; ++Index) Step();
		TestTrue(TEXT("Partial exposure has not committed Combat"), Enemy->GetCombatState() == EEnemyCombatState::Alert);
		TestTrue(TEXT("Production common tree selects Alert"), Tree->GetActiveStateNames().Contains(TEXT("Alert")));
		AActor* Wall = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(10, 1000, 1000));
		Box->SetWorldLocation(FVector(250, 0, 0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		for (int32 Index = 0; Index < 72; ++Index) Step();
		TestFalse(TEXT("Real obstruction removes direct visibility"), Enemy->IsPlayerCurrentlyVisible());
		TestTrue(TEXT("Awareness decay exits Alert without waiting five seconds"), Enemy->GetCombatState() == EEnemyCombatState::NonCombat);
		TestFalse(TEXT("Deferred clear event leaves production Alert state"), Tree->GetActiveStateNames().Contains(TEXT("Alert")));
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 Index = 0; Index < 80 && !Enemy->IsInCombat(); ++Index) Step();
		TestTrue(TEXT("Real rediscovery fills awareness and enters Combat"), Enemy->IsInCombat());
		Step(); Step();
		TestFalse(TEXT("Combat event exits production Alert state"), Tree->GetActiveStateNames().Contains(TEXT("Alert")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierBoneSightDedicatedPoseTest,
	"Outlier.Enemy.SightRuntime.DedicatedPoseAndPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierBoneSightDedicatedPoseTest::RunTest(const FString& Parameters)
{
	using namespace OutlierBoneSightTests;
	FWorld Fixture(true);
	if (!TestTrue(TEXT("Actual dedicated server world with a NetDriver"), Fixture.World->GetNetMode() == NM_DedicatedServer && Fixture.World->GetNetDriver())) return false;
	UClass* Class = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!Class) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(Class, FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	if (!Shooter || !Controller) return false;
	Controller->Possess(Shooter);
	Shooter->GetCharacterMovement()->DisableMovement();
	// 실제 원격 소유 Pawn처럼 포즈 진행은 ServerMove가 맡고, 메시 Tick은 본 평가를 맡긴다.
	// Sight가 이 엔진 스케줄을 바꾸거나 애니메이션 시간을 추가 진행해서는 안 된다.
	Controller->AcknowledgedPawn = Shooter;
	Shooter->GetMesh()->bOnlyAllowAutonomousTickPose = true;
	float MoveTime = 0.0f;
	UClass* RifleClass = LoadClass<AWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!TestNotNull(TEXT("Production rifle class"), RifleClass)) return false;
	AWeaponBase* Rifle = Fixture.World->SpawnActor<AWeaponBase>(RifleClass, Spawn);
	if (!Rifle) return false;
	Shooter->AFirstPersonCharacter::EquipWeapon(Rifle);
	Shooter->RefreshWeaponMode();
	FCanBeSeenFromContext Context;
	Context.ObserverLocation = FVector(1000, 0, 1200);
	FVector Seen;
	int32 Sync = 0, Async = 0;
	float Strength = 0;
	const auto Query = [&]
	{
		Sync = Async = 0;
		return Shooter->CanBeSeenFrom(Context, Seen, Sync, Async, Strength);
	};
	int32 Finalizations = 0;
	USkeletalMeshComponent* Mesh = Shooter->GetMesh();
	const FDelegateHandle Counter = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
		FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([&] { ++Finalizations; }));
	const auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Query();
			++GFrameCounter;
			MoveTime += 1.0f / 60.0f;
			FCharacterNetworkMoveData Move;
			Move.TimeStamp = MoveTime;
			Move.ControlRotation = FRotator::ZeroRotator;
			Move.Location = Shooter->GetActorLocation();
			Move.MovementMode = Shooter->GetCharacterMovement()->PackNetworkMovementMode();
			Move.CompressedMoveFlags = Shooter->GetCharacterMovement()->bWantsToCrouch
				? FSavedMove_Character::FLAG_WantsToCrouch : 0;
			Shooter->GetCharacterMovement()->ServerMove_PerformMovement(Move);
			Fixture.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		}
	};
	for (const bool bSuit : { true, false })
	{
		if (!TestTrue(TEXT("Production Suit presentation changes"), Shooter->SetSuitPresentation(bSuit))) continue;
		Advance(15);
		TestTrue(TEXT("Unrendered dedicated bones become visible through normal world ticks"), Query() == UAISense_Sight::EVisibilityResult::Visible);
		TestEqual(TEXT("All registered bones are traced"), Sync, Shooter->ResolveSightBoneRegistration().GetBoneIndices().Num());
		const int32 Before = Finalizations;
		const double Start = FPlatformTime::Seconds();
		for (int32 Observer = 0; Observer < 100; ++Observer) Query();
		AddInfo(FString::Printf(TEXT("Suit=%d 100ObserverQueriesMs=%.4f Traces=%d PoseFinalizations=%d"), bSuit, (FPlatformTime::Seconds() - Start) * 1000, Sync * 100, Finalizations - Before));
		TestEqual(TEXT("Repeated observers do not evaluate extra animation poses"), Finalizations, Before);
		const FVector NeutralHead = Mesh->GetBoneTransform(Shooter->ResolveSightBoneRegistration().GetBoneIndices()[0]).GetLocation();
		for (const float Lean : { -1.0f, 1.0f, 0.0f })
		{
			Shooter->SetLeanTarget(Lean, false);
			Advance(30);
			TestTrue(TEXT("Fixture actually reaches the requested resolved Lean"), FMath::IsNearlyEqual(Shooter->GetCurrentLeanAlpha(), Lean, 0.05f));
			TestTrue(TEXT("Lean visibility uses current animated TP bones"), Query() == UAISense_Sight::EVisibilityResult::Visible);
			const int32 Head = Shooter->ResolveSightBoneRegistration().GetBoneIndices()[0];
			TestTrue(TEXT("Reported sight location matches evaluated world bone"), Seen.Equals(Mesh->GetBoneTransform(Head).GetLocation(), 0.01));
			if (Lean != 0.0f) TestTrue(TEXT("Actual TP head moves with Lean"), !Seen.Equals(NeutralHead, 1.0));
		}
		Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Shooter->HandleCrouchPressed();
		Shooter->GetCharacterMovement()->Crouch(false);
		Advance(10);
		TestTrue(TEXT("Fixture actually enters crouch"), Shooter->bIsCrouched);
		TestTrue(TEXT("Crouched pose remains a valid bone sample"), Query() == UAISense_Sight::EVisibilityResult::Visible);
		Shooter->GetCharacterMovement()->UnCrouch(false);
		Shooter->HandleCrouchReleased();
		Shooter->GetCharacterMovement()->DisableMovement();
		UAnimInstance* Animation = Mesh->GetAnimInstance();
		UAnimMontage* Montage = Shooter->GetThirdPersonMeleeAttackMontage();
		if (TestNotNull(TEXT("Production TP action montage"), Montage) && TestNotNull(TEXT("Production TP AnimInstance"), Animation))
		{
			TestTrue(TEXT("Actual montage starts"), Animation->Montage_Play(Montage) > 0.0f);
			Advance(5);
			const float Position = Animation->Montage_GetPosition(Montage);
			TestTrue(TEXT("Normal world ticks advance montage time"), Position > 0.0f);
			for (int32 Observer = 0; Observer < 20; ++Observer) Query();
			TestEqual(TEXT("Sight calls do not advance montage time"), Animation->Montage_GetPosition(Montage), Position);
			TestTrue(TEXT("Montage pose is sampled by sight"), Query() == UAISense_Sight::EVisibilityResult::Visible);
			Animation->Montage_Stop(0.0f);
		}
	}
	Shooter->SetMeleeTracePoseRefreshEnabled(true);
	Shooter->BoneSight.Shutdown();
	TestTrue(TEXT("Sight release preserves active melee refresh"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	Query();
	Shooter->SetMeleeTracePoseRefreshEnabled(false);
	TestTrue(TEXT("Melee release preserves active sight refresh"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	Shooter->BoneSight.Shutdown();
	TestTrue(TEXT("Both released restore dedicated montage-only policy"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered);
	Mesh->UnregisterOnBoneTransformsFinalizedDelegate(Counter);
	return true;
}

#endif
