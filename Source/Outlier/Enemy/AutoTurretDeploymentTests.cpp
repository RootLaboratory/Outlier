#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/LaserWeapon.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FTurretDeploymentTestWorld
	{
		UWorld* World = nullptr;

		FTurretDeploymentTestWorld()
		{
			if (!GEngine) return;
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TurretDeploymentTest")),
				GetTransientPackage());
			if (!World) return;
			World->AddToRoot();
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FTurretDeploymentTestWorld()
		{
			if (!World) return;
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};

	UAnimInstance* MakeTestAnimInstance(USkeletalMeshComponent* Mesh, USkeleton* Skeleton)
	{
		// 렌더링 에셋 없이 몽타주 인스턴스의 재생 상태만 검증한다. 실제 Slot/본 자세는 PIE 대상이다.
		UAnimInstance* Instance = NewObject<UAnimInstance>(Mesh);
		Mesh->AnimScriptInstance = Instance;
		Instance->InitializeAnimation();
		Instance->CurrentSkeleton = Skeleton;
		return Instance;
	}

	UAnimMontage* MakeTestMontage(UObject* Outer, USkeleton* Skeleton)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(Outer);
		Montage->SetSkeleton(Skeleton);
		Montage->SlotAnimTracks.Reset();
		Montage->SlotAnimTracks.AddDefaulted_GetRef().SlotName = TEXT("DefaultSlot");
		Montage->CompositeSections.AddDefaulted_GetRef().SectionName = TEXT("Default");
		Montage->SetCompositeLength(1.0f);
		return Montage;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoTurretDeploymentPresentationTest,
	"Outlier.Enemy.Turret.DeploymentPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutoTurretDeploymentPresentationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTurretDeploymentTestWorld Fixture;
	if (!TestNotNull(TEXT("Turret deployment world"), Fixture.World)) return false;
	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Native turret"), Turret)) return false;

	USkeleton* Skeleton = NewObject<USkeleton>(Turret);
	UAnimInstance* Body = MakeTestAnimInstance(Turret->GetMesh(), Skeleton);
	UAnimInstance* Head = MakeTestAnimInstance(Turret->TurretHeadMesh, Skeleton);
	Turret->DeployMontage = MakeTestMontage(Turret, Skeleton);
	Turret->FireMontage = MakeTestMontage(Turret, Skeleton);
	UAnimMontage* Deploy = Turret->DeployMontage;
	const float FinalPosition = Deploy->GetPlayLength() - KINDA_SMALL_NUMBER;
	Turret->PrepareForRoomWaveActivation();
	TestEqual(TEXT("Waiting holds the closed frame"), Body->Montage_GetPosition(Deploy), 0.0f);
	TestFalse(TEXT("Waiting body montage is paused"), Body->Montage_IsPlaying(Deploy));
	TestTrue(TEXT("Authority starts the first activation"), Turret->BeginRoomWaveDeployment(0));
	const int32 FirstSerial = Turret->GetRoomWaveActivationSerial();
	FAnimMontageInstance* FirstInstance = Body->GetActiveInstanceForMontage(Deploy);
	if (!TestNotNull(TEXT("Deployment really creates a montage instance"), FirstInstance)) return false;
	TestTrue(TEXT("Deploying body montage plays"), Body->Montage_IsPlaying(Deploy));
	TestFalse(TEXT("Only the deployment instance disables auto blend-out"), FirstInstance->bEnableAutoBlendOut);
	TestTrue(TEXT("Shared montage asset remains unchanged"), Deploy->bEnableAutoBlendOut);
	Body->Montage_SetPosition(Deploy, 0.4f);
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	TestEqual(TEXT("Duplicate start does not restart playback"), Body->Montage_GetPosition(Deploy), 0.4f);

	Head->Montage_Play(Turret->FireMontage);
	// 완료가 blend-in 도중 도착해도 가중치가 덜 오른 자세를 고정하거나 이전 인스턴스를 남기지 않는다.
	Turret->CompleteTurretDeployment();
	TestTrue(TEXT("Completion stops the replaced blend-in instance"), FirstInstance->IsStopped());
	TestEqual(TEXT("Active body holds the deployed frame"), Body->Montage_GetPosition(Deploy), FinalPosition);
	TestFalse(TEXT("Active body montage is paused"), Body->Montage_IsPlaying(Deploy));
	FAnimMontageInstance* HeldInstance = Body->GetActiveInstanceForMontage(Deploy);
	if (!TestNotNull(TEXT("Active pose has a live montage instance"), HeldInstance)) return false;
	TestEqual(TEXT("Held pose has full weight immediately"), HeldInstance->GetWeight(), 1.0f);
	int32 ActiveDeploymentCount = 0;
	for (const FAnimMontageInstance* Instance : Body->MontageInstances)
	{
		if (Instance && Instance->Montage == Deploy && !Instance->IsStopped())
		{
			++ActiveDeploymentCount;
		}
	}
	TestEqual(TEXT("Completion leaves exactly one active deployment instance"), ActiveDeploymentCount, 1);
	TestTrue(TEXT("Body completion does not pause head fire"), Head->Montage_IsPlaying(Turret->FireMontage));
	TestFalse(TEXT("Body skeletal animation remains enabled"), Turret->GetMesh()->bPauseAnims);
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	Turret->OnRep_TurretLifecycleState();
	TestTrue(TEXT("Duplicate Active and late start keep the same held instance"),
		Body->GetActiveInstanceForMontage(Deploy) == HeldInstance);

	// 관련성 복귀에서 전개 RPC를 못 받았거나 몽타주가 사라져도 Active 스냅샷만으로 복원한다.
	Body->Montage_Stop(0.0f, Deploy);
	Turret->PresentedDeploymentGeneration = INDEX_NONE;
	Turret->PresentedDeploymentSerial = 0;
	Turret->bDeploymentCompletionPresented = false;
	Turret->OnRep_TurretLifecycleState();
	TestEqual(TEXT("Active snapshot restores directly to the deployed frame"),
		Body->Montage_GetPosition(Deploy), FinalPosition);
	TestFalse(TEXT("Restored Active never starts deployment playback"), Body->Montage_IsPlaying(Deploy));

	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPresentation);
	TestEqual(TEXT("Death presentation keeps the deployed body"), Body->Montage_GetPosition(Deploy), FinalPosition);
	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPersistent);
	TestEqual(TEXT("Persistent death keeps the deployed body"), Body->Montage_GetPosition(Deploy), FinalPosition);
	Turret->PrepareForRoomWaveActivation();
	TestEqual(TEXT("Explicit Waiting closes the body"), Body->Montage_GetPosition(Deploy), 0.0f);
	TestFalse(TEXT("Waiting cancels head fire"), Head->Montage_IsPlaying(Turret->FireMontage));
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	TestFalse(TEXT("Old start cannot play while Waiting"), Body->Montage_IsPlaying(Deploy));
	TestTrue(TEXT("A reused turret starts a new activation"), Turret->BeginRoomWaveDeployment(0));
	TestNotEqual(TEXT("Reuse advances activation serial"), Turret->GetRoomWaveActivationSerial(), FirstSerial);
	Body->Montage_SetPosition(Deploy, 0.3f);
	Turret->MulticastBeginTurretDeployment_Implementation(0, FirstSerial);
	Turret->NotifyDeploySequenceFinished(0, FirstSerial);
	TestTrue(TEXT("Old completion cannot activate the reused turret"), Turret->IsDeploying());
	TestEqual(TEXT("Old start cannot rewind the reused deployment"), Body->Montage_GetPosition(Deploy), 0.3f);

	// 속성보다 먼저 온 RPC는 보류하고, 이후 Deploying 스냅샷으로 한 번만 재생한다.
	Turret->PrepareForRoomWaveActivation();
	const int32 NextSerial = Turret->GetRoomWaveActivationSerial() + 1;
	Turret->MulticastBeginTurretDeployment_Implementation(1, NextSerial);
	TestFalse(TEXT("RPC before its snapshot cannot start the old activation"), Body->Montage_IsPlaying(Deploy));
	Turret->ReplicatedLifecycle.State = EAutoTurretLifecycleState::Deploying;
	Turret->ReplicatedLifecycle.GameplayGeneration = 1;
	Turret->ReplicatedLifecycle.ActivationSerial = NextSerial;
	Turret->OnRep_TurretLifecycleState();
	TestTrue(TEXT("Deploying snapshot recovers an early or missed RPC"), Body->Montage_IsPlaying(Deploy));
	TestEqual(TEXT("Snapshot restores the activation generation"), Turret->GetRoomWaveGameplayGeneration(), 1);
	Body->Montage_SetPosition(Deploy, 0.6f);
	Turret->OnRep_TurretLifecycleState();
	TestEqual(TEXT("Duplicate Deploying snapshot does not rewind"), Body->Montage_GetPosition(Deploy), 0.6f);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FAutoTurretAimPolicyTest,
	"Outlier.Enemy.Turret.AimPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FAutoTurretAimPolicyTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Scenario : { TEXT("YawRangesAndBoundary"), TEXT("VerticalLimitsAndDegeneracy"), TEXT("PerceptionHeadDirection"),
		TEXT("GuardRecoveryAndHandoff"), TEXT("LaserDeferredClue"), TEXT("RealSightConeAndOcclusion"),
		TEXT("AttackReachabilityAndAlignment") })
	{
		Names.Add(Scenario);
		Commands.Add(Scenario);
	}
}

bool FAutoTurretAimPolicyTest::RunTest(const FString& Parameters)
{
	FTurretDeploymentTestWorld Fixture;
	if (!TestNotNull(TEXT("Turret aim world"), Fixture.World)) return false;
	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Turret aim receiver"), Turret)) return false;
	Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
	Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
	Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
	Turret->SetActorRotation(FRotator(0, 37, 0));
	const FQuat BodyRotation = Turret->GetActorQuat();
	FAutoTurretBehaviorRow& Behavior = const_cast<FAutoTurretBehaviorRow&>(Turret->GetTurretBehavior());
	Behavior.DefaultRotationSpeedDegrees = Behavior.AttackRotationSpeedDegrees = 90.0f;
	// 기존 DT에 좁은 제한이 직렬화되어 있어도 새 전방위 정책을 가로막지 않아야 한다.
	Behavior.MaxYawDegrees = Behavior.AttackMaxYawDegrees = 10.0f;
	Behavior.MaxPitchDegrees = 23.0f;
	auto LocationAt = [&](float Pitch, float Yaw)
	{
		return Turret->GetPawnViewLocation() + BodyRotation.RotateVector(FRotator(Pitch, Yaw, 0).Vector()) * 1000.0;
	};
	if (Parameters == TEXT("YawRangesAndBoundary"))
	{
		for (bool bAttack : { false, true })
		{
			Turret->CurrentAimOffset = FRotator::ZeroRotator;
			Turret->ApplyHeadRotation();
			TestTrue(TEXT("Both general and attack aim can reach directly behind"),
				Turret->UpdateTurretAimAtLocation(LocationAt(0, 180), 10.0f, bAttack));
			TestTrue(TEXT("Rear aim has no leftover yaw clamp"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Turret->CurrentAimOffset.Yaw, 180.0f)) < 0.1f);
			Turret->CurrentAimOffset = FRotator(0, 179, 0);
			Turret->ApplyHeadRotation();
			Turret->UpdateTurretAimAtLocation(LocationAt(0, -179), 0.01f, bAttack);
			const float FirstStep = FMath::FindDeltaAngleDegrees(179.0f, Turret->CurrentAimOffset.Yaw);
			TestTrue(TEXT("Boundary crossing advances by the short positive step"), FirstStep > 0.0f && FirstStep <= 0.91f);
			TestTrue(TEXT("Boundary target finishes without a full reverse turn"),
				Turret->UpdateTurretAimAtLocation(LocationAt(0, -179), 0.1f, bAttack));
		}
		TestTrue(TEXT("Neutral-pitch search can also face directly behind"),
			Turret->UpdateTurretAimAtLocation(LocationAt(80, 180), 10.0f, false, true));
		TestTrue(TEXT("Search keeps its configured neutral pitch"),
			FMath::IsNearlyEqual(Turret->CurrentAimOffset.Pitch, Behavior.SearchNeutralPitchDegrees, 0.1f));
	}
	else if (Parameters == TEXT("VerticalLimitsAndDegeneracy"))
	{
		for (bool bAttack : { false, true })
		{
			Turret->CurrentAimOffset = FRotator(0, 72, 0);
			Turret->ApplyHeadRotation();
			const FVector Up = Turret->GetPawnViewLocation() + BodyRotation.RotateVector(FVector(0, 0, 1000));
			TestTrue(TEXT("Directly above is reachable without indefinite alignment"),
				Turret->UpdateTurretAimAtLocation(Up, 10.0f, bAttack));
			TestTrue(TEXT("Upward limit reaches positive ninety degrees"), FMath::IsNearlyEqual(Turret->CurrentAimOffset.Pitch, 90.0f, 0.1f));
			TestTrue(TEXT("Vertical source preserves the current yaw"), FMath::IsNearlyEqual(Turret->CurrentAimOffset.Yaw, 72.0f, 0.1f));
			Turret->UpdateTurretAimAtLocation(Up + BodyRotation.RotateVector(FVector(0.0000001, 0, 0)), 0.1f, bAttack);
			TestTrue(TEXT("Tiny horizontal noise cannot flip vertical yaw"), FMath::IsNearlyEqual(Turret->CurrentAimOffset.Yaw, 72.0f, 0.1f));
			TestTrue(TEXT("Downward source completes at the reachable limit"),
				Turret->UpdateTurretAimAtLocation(Turret->GetPawnViewLocation() - FVector(0, 0, 1000), 10.0f, bAttack));
			TestTrue(TEXT("Existing downward limit remains species configured"), FMath::IsNearlyEqual(Turret->CurrentAimOffset.Pitch, -23.0f, 0.1f));
			const FRotator Before = Turret->CurrentAimOffset;
			TestTrue(TEXT("Coincident source completes rather than waiting forever"),
				Turret->UpdateTurretAimAtLocation(Turret->GetPawnViewLocation(), 0.1f, bAttack));
			TestTrue(TEXT("Coincident source keeps the current orientation"), Turret->CurrentAimOffset.Equals(Before, 0.01f));
		}
	}
	else if (Parameters == TEXT("AttackReachabilityAndAlignment"))
	{
		TestTrue(TEXT("Attack candidates can be behind the turret"), Turret->IsTurretAimLocationReachable(LocationAt(0, 180)));
		TestTrue(TEXT("Attack candidates can be directly above"), Turret->IsTurretAimLocationReachable(LocationAt(90, 0)));
		TestTrue(TEXT("The configured downward boundary is reachable"), Turret->IsTurretAimLocationReachable(LocationAt(-23, 0)));
		TestFalse(TEXT("Clamping a deeper target does not make it attackable"), Turret->IsTurretAimLocationReachable(LocationAt(-30, 0)));
		TestFalse(TEXT("A coincident point has no valid firing direction"), Turret->IsTurretAimLocationReachable(Turret->GetPawnViewLocation()));
		Fixture.World->CreateAISystem();
		AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
		APartnerCharacter* Target = Fixture.World->SpawnActor<APartnerCharacter>();
		if (!TestNotNull(TEXT("Alignment controller"), AI) || !TestNotNull(TEXT("Alignment target"), Target))
		{
			return false;
		}
		AI->Possess(Turret);
		Target->SetActorLocation(LocationAt(0, 120));
		AI->SetVisibleTargetsForTesting({Target});
		Turret->SetPlayerCurrentlyVisible(true);
		Turret->ObserveTrackedTarget(Target);
		if (!TestEqual(TEXT("Alignment uses the actual tracked target"), Turret->GetTrackedTarget(), static_cast<AActor*>(Target)))
		{
			return false;
		}
		Turret->CurrentAimOffset = FRotator::ZeroRotator;
		Turret->ApplyHeadRotation();
		TestFalse(TEXT("Selecting a target does not immediately grant alignment"), Turret->IsTurretAttackAimReady(Target));
		Turret->UpdateTurretAimAtLocation(Turret->GetCombatAimPoint(Target), 0.01f, true);
		TestFalse(TEXT("A bounded rotation step still holds fire"), Turret->IsTurretAttackAimReady(Target));
		Turret->UpdateTurretAimAtLocation(Turret->GetCombatAimPoint(Target), 10.0f, true);
		TestTrue(TEXT("The actual head direction permits an aligned shot"), Turret->IsTurretAttackAimReady(Target));
		Target->SetActorLocation(LocationAt(-70, 0));
		Turret->UpdateTurretAimAtLocation(Turret->GetCombatAimPoint(Target), 10.0f, true);
		TestFalse(TEXT("Reaching the clamped tracking limit cannot approve an unreachable shot"), Turret->IsTurretAttackAimReady(Target));
	}
	else if (Parameters == TEXT("RealSightConeAndOcclusion"))
	{
		Fixture.World->CreateAISystem();
		// 일반 AIController는 감지만 갱신한다. 전투 AI가 타깃을 향해 자동 회전하면 정면 검증을 가리게 된다.
		AAIController* Observer = Fixture.World->SpawnActor<AAIController>();
		if (!TestNotNull(TEXT("Sight-only observer controller"), Observer)) return false;
		Observer->Possess(Turret);
		UAIPerceptionComponent* Listener = NewObject<UAIPerceptionComponent>(Observer);
		Observer->SetPerceptionComponent(*Listener);
		UAISenseConfig_Sight* Sight = NewObject<UAISenseConfig_Sight>(Listener);
		Sight->SightRadius = Sight->LoseSightRadius = 1000.0f;
		Sight->PeripheralVisionAngleDegrees = 45.0f;
		Sight->DetectionByAffiliation.bDetectEnemies = true;
		Sight->DetectionByAffiliation.bDetectFriendlies = true;
		Sight->DetectionByAffiliation.bDetectNeutrals = true;
		Listener->ConfigureSense(*Sight);
		Listener->RegisterComponent();
		Listener->RequestStimuliListenerUpdate();
		UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
		if (!TestNotNull(TEXT("Actual perception system"), Perception)) return false;
		AActor* Target = Fixture.World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Generic sight source"), Target)) return false;
		UBoxComponent* TargetBox = NewObject<UBoxComponent>(Target);
		Target->SetRootComponent(TargetBox);
		TargetBox->SetBoxExtent(FVector(20));
		TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TargetBox->SetCollisionResponseToAllChannels(ECR_Ignore);
		TargetBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		TargetBox->RegisterComponent();
		if (!TestTrue(TEXT("Target is registered with real Sight"),
			UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Target))) return false;
		auto AdvanceSight = [&]()
		{
			for (int32 Frame = 0; Frame < 12; ++Frame)
			{
				++GFrameCounter;
				Fixture.World->Tick(LEVELTICK_All, 0.05f);
				Perception->Tick(0.05f);
			}
		};
		auto IsSeen = [&]()
		{
			TArray<AActor*> Seen;
			Listener->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
			return Seen.Contains(Target);
		};
		Turret->CurrentAimOffset = FRotator::ZeroRotator;
		Turret->ApplyHeadRotation();
		Target->SetActorLocation(Turret->GetPawnViewLocation() + BodyRotation.RotateVector(FVector(500, 0, 0)));
		AdvanceSight();
		TestTrue(TEXT("Actual Sight detects a source in front of the head"), IsSeen());
		Target->SetActorLocation(Turret->GetPawnViewLocation() + BodyRotation.RotateVector(FVector(-500, 0, 0)));
		AdvanceSight();
		TestFalse(TEXT("Yaw freedom does not grant simultaneous rear sight"), IsSeen());
		Turret->UpdateTurretAimAtLocation(Target->GetActorLocation(), 10.0f, false);
		AdvanceSight();
		TestTrue(TEXT("Rotating only the head makes the rear source detectable"), IsSeen());
		const FVector HeadForward = Turret->GetViewRotation().Vector();
		Target->SetActorLocation(Turret->GetPawnViewLocation() + HeadForward * 1500.0f);
		AdvanceSight();
		TestFalse(TEXT("Head rotation preserves the configured sight distance"), IsSeen());
		Target->SetActorLocation(Turret->GetPawnViewLocation() + HeadForward * 500.0f);
		AdvanceSight();
		TestTrue(TEXT("Returning inside range reacquires actual Sight"), IsSeen());
		AActor* Wall = Fixture.World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Physical visibility blocker"), Wall)) return false;
		UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(WallBox);
		WallBox->SetBoxExtent(FVector(10, 500, 500));
		WallBox->SetWorldLocation(Turret->GetPawnViewLocation() + HeadForward * 250.0f);
		WallBox->SetWorldRotation(Turret->GetViewRotation());
		WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		WallBox->SetCollisionResponseToAllChannels(ECR_Block);
		WallBox->RegisterComponent();
		AdvanceSight();
		TestFalse(TEXT("A real wall blocks the head-facing sight ray"), IsSeen());
		WallBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AdvanceSight();
		TestTrue(TEXT("Removing the physical wall restores actual Sight"), IsSeen());
	}
	else if (Parameters == TEXT("GuardRecoveryAndHandoff") || Parameters == TEXT("LaserDeferredClue"))
	{
		AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
		if (!TestNotNull(TEXT("Hit response controller"), AI)) return false;
		AI->Possess(Turret);
		FEnemyStat& Stat = const_cast<FEnemyStat&>(Turret->GetRuntimeStat());
		Stat.HitDirectionMinDuration = 0.05f;
		Stat.HitDirectionMaxDuration = 3.0f;
		Stat.MinAlertHoldAfterClue = 4.0f;
		Fixture.World->TimeSeconds = 10.0f;
		auto Step = [&](float DeltaTime)
		{
			Fixture.World->TimeSeconds += DeltaTime;
			Turret->TickHitResponse(DeltaTime);
		};
		auto IssueClue = [&](const FVector& Location, double Lifetime)
		{
			FEnemyHitClue Clue;
			return Turret->CreateHitClue(Location, Fixture.World->GetTimeSeconds(), Lifetime, Clue)
				&& Turret->RecordHitClue(Clue);
		};
		auto FinishConfirmation = [&]()
		{
			for (int32 Frame = 0; Frame < 240 && Turret->HasHitResponse(); ++Frame) Step(1.0f / 60.0f);
			return !Turret->HasHitResponse();
		};
		if (Parameters == TEXT("GuardRecoveryAndHandoff"))
		{
			const FVector HitLocation = LocationAt(0, 160);
			if (!TestTrue(TEXT("Unseen hit becomes a location clue"), IssueClue(HitLocation, 6.0))) return false;
			Step(0.0f);
			if (!TestTrue(TEXT("Turret confirms the hit direction"), Turret->HasHitResponse())
				|| !TestTrue(TEXT("Turret confirmation finishes in finite time"), FinishConfirmation())) return false;
			TestEqual(TEXT("Hit confirmation does not manufacture combat"), Turret->GetCombatState(), EEnemyCombatState::Alert);
			TestTrue(TEXT("Completed confirmation hands over to guard scanning"), Turret->IsTurretGuardRotationActive());
			TestTrue(TEXT("Guard center is the accepted hit direction"), FMath::IsNearlyEqual(Turret->HitGuardCenterYaw, 160.0f, 0.01f));
			Step(0.2f);
			TestTrue(TEXT("Guard scan stays around the hit direction rather than returning early"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Turret->CurrentAimOffset.Yaw, 160.0f)) <= Behavior.SearchYawDegrees + 1.0f);
			TestTrue(TEXT("Original four-second hold is still active before its deadline"), Turret->IsAlertHoldActive());
			// 경계 해제는 실제 인지 정책으로 실행한다. 회전 완료 시각을 새 단서 시각으로 쓰지 않는다.
			Fixture.World->TimeSeconds = 14.1f;
			TestFalse(TEXT("Scanning cannot extend the hit occurrence deadline"), Turret->IsAlertHoldActive());
			AI->ResolveInitialAwarenessAlert();
			if (!TestEqual(TEXT("Existing alert rules release the guard"), Turret->GetCombatState(), EEnemyCombatState::NonCombat)) return false;
			const float BeforeReturn = Turret->CurrentAimOffset.Yaw;
			Step(0.01f);
			TestTrue(TEXT("Guard return moves continuously from the actual current angle"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeReturn, Turret->CurrentAimOffset.Yaw)) <= 0.91f);
			TestTrue(TEXT("Return remains active while it has not reached neutral"), Turret->IsTurretGuardRotationActive());
			if (!IssueClue(LocationAt(0, -100), 6.0)) return false;
			Step(0.0f);
			TestTrue(TEXT("A new hit immediately interrupts the old guard return"), Turret->HasHitResponse());
			TestFalse(TEXT("New confirmation owns rotation instead of old guard"), Turret->IsTurretGuardRotationActive());
			if (!FinishConfirmation()) return false;
			TestTrue(TEXT("The replacement confirmation owns the new guard center"),
				Turret->IsTurretGuardRotationActive() && FMath::IsNearlyEqual(Turret->HitGuardCenterYaw, -100.0f, 0.01f));
			Fixture.World->TimeSeconds += 4.1f;
			AI->ResolveInitialAwarenessAlert();
			for (int32 Frame = 0; Frame < 240 && Turret->IsTurretGuardRotationActive(); ++Frame) Step(1.0f / 60.0f);
			TestFalse(TEXT("Default-direction return finishes in finite time"), Turret->IsTurretGuardRotationActive());
			TestTrue(TEXT("Guard returns to the original local forward"), FMath::IsNearlyZero(Turret->CurrentAimOffset.Yaw, 1.1f));
			if (!IssueClue(Turret->GetPawnViewLocation() + FVector(0, 0, 1000), 6.0)) return false;
			Step(0.0f);
			if (!FinishConfirmation()) return false;
			const float VerticalCenterYaw = Turret->HitGuardCenterYaw;
			float FirstCycleYaw = 0.0f;
			for (int32 Frame = 0; Frame < 360; ++Frame)
			{
				Step(1.0f / 60.0f);
				if (Frame == 179) FirstCycleYaw = Turret->CurrentAimOffset.Yaw;
				if (!TestTrue(TEXT("Vertical-hit scan stays within its fixed center's yaw range"),
					FMath::Abs(FMath::FindDeltaAngleDegrees(VerticalCenterYaw, Turret->CurrentAimOffset.Yaw)) <= Behavior.SearchYawDegrees + 1.0f)) return false;
			}
			// 감속 추적에는 위상 지연이 있으므로 매 주기 끝의 중립 도달 대신 같은 위상의 누적 이동을 비교한다.
			TestTrue(TEXT("Vertical-hit scanning cannot accumulate yaw drift between cycles"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(FirstCycleYaw, Turret->CurrentAimOffset.Yaw)) < 3.0f);
			Turret->EnterStun();
			Step(0.01f);
			TestFalse(TEXT("Stun cancels a completed hit guard"), Turret->IsTurretGuardRotationActive());
			Turret->RestoreStateAfterStun();
			TestFalse(TEXT("Stun recovery cannot revive the old guard"), Turret->IsTurretGuardRotationActive());
			if (!IssueClue(LocationAt(0, 100), 6.0)) return false;
			Step(0.0f);
			if (!FinishConfirmation()) return false;
			Turret->ApplyHackedTeamState();
			TestFalse(TEXT("Team change discards the old team's guard rotation"), Turret->IsTurretGuardRotationActive());
		}
		else
		{
			ALaserWeapon* Laser = Fixture.World->SpawnActor<ALaserWeapon>();
			FObjectPropertyBase* WeaponProperty = FindFProperty<FObjectPropertyBase>(Turret->GetClass(), TEXT("CurrentWeapon"));
			if (!TestNotNull(TEXT("Laser commitment weapon"), Laser)
				|| !TestNotNull(TEXT("Existing replicated weapon slot"), WeaponProperty)) return false;
			Laser->SetOwner(Turret);
			WeaponProperty->SetObjectPropertyValue_InContainer(Turret, Laser);
			Turret->CurrentAimOffset = FRotator(10, 20, 0);
			Turret->ApplyHeadRotation();
			Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
			if (!IssueClue(LocationAt(0, -100), 6.0)) return false;
			const FRotator CommittedAim = Turret->CurrentAimOffset;
			Step(0.3f);
			FEnemyHitClue Pending;
			TestFalse(TEXT("Laser charge defers hit confirmation"), Turret->HasHitResponse());
			TestTrue(TEXT("Laser charge retains the original pending clue"), Turret->TryGetPendingHitClue(Pending));
			const double ExpiresAt = Pending.ExpiresAt;
			TestTrue(TEXT("Deferred hit cannot overwrite committed laser aim"), Turret->CurrentAimOffset.Equals(CommittedAim, 0.01));
			Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
			Step(0.0f);
			TestTrue(TEXT("Attack completion evaluates a still-valid clue once"), Turret->HasHitResponse());
			TestFalse(TEXT("Accepted deferred clue leaves the pending slot"), Turret->TryGetPendingHitClue(Pending));
			TestTrue(TEXT("Completion cannot manufacture a new occurrence deadline"), ExpiresAt == 16.0);
			if (!FinishConfirmation()) return false;
			Turret->ClearHitClues();
			Step(0.01f);
			TestFalse(TEXT("Consumed laser clue cannot replay after completion"), Turret->HasHitResponse());
			Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
			if (!IssueClue(LocationAt(0, 150), 0.1)) return false;
			Step(0.2f);
			Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
			Step(0.0f);
			TestFalse(TEXT("An expired deferred clue does not run at laser completion"), Turret->HasHitResponse());
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
				TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
			if (!TestNotNull(TEXT("Concrete shooter BP"), ShooterClass)) return false;
			AShooterCharacter* Visible = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass,
				FVector(2000, 0, 0), FRotator::ZeroRotator, Spawn);
			if (!TestNotNull(TEXT("Actually selected hostile type"), Visible)) return false;
			Visible->GetOutlierAbilitySystemComponent()->InitializeForPawn(Visible);
			Visible->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
			AI->SetCachedVisibleTargetForTesting(Visible);
			Turret->SetPlayerCurrentlyVisible(true);
			if (!TestTrue(TEXT("Visible target is valid under current team rules"), AI->IsValidTrackingTarget(Visible))) return false;
			Turret->SetAttackPhase(EEnemyAttackPhase::Telegraph);
			if (!IssueClue(LocationAt(0, -150), 6.0)) return false;
			Step(0.0f);
			TestFalse(TEXT("Direct sight consumes rather than queues an ignored hit"), Turret->TryGetPendingHitClue(Pending));
			Turret->SetAttackPhase(EEnemyAttackPhase::Idle);
			AI->SetCachedVisibleTargetForTesting(nullptr);
			Turret->SetPlayerCurrentlyVisible(false);
			Turret->ClearTargetTracking();
			Step(0.1f);
			TestFalse(TEXT("Later sight loss cannot replay the ignored laser hit"), Turret->HasHitResponse());
		}
	}
	else
	{
		AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
		if (!TestNotNull(TEXT("Turret perception controller"), AI)) return false;
		AI->Possess(Turret);
		UAIPerceptionComponent* Perception = AI->GetPerceptionComponent();
		if (!TestNotNull(TEXT("Real AI perception component"), Perception)) return false;
		for (const FRotator Aim : { FRotator(25, 150, 0), FRotator(89, -179, 0), FRotator(90, 72, 0), FRotator(-23, -90, 0) })
		{
			Turret->CurrentAimOffset = Aim;
			Turret->ApplyHeadRotation();
			FVector EyesLocation, EyesDirection;
			Perception->GetLocationAndDirection(EyesLocation, EyesDirection);
			const FVector HeadDirection = Turret->TurretHeadPitchPivot->GetForwardVector();
			TestTrue(TEXT("Actual head and logical view share the same forward"),
				HeadDirection.Equals(Turret->GetViewRotation().Vector(), 0.001));
			TestTrue(TEXT("Perception reads the actual head forward including vertical aim"), EyesDirection.Equals(HeadDirection, 0.001));
			TestTrue(TEXT("Perception origin follows the turret head viewpoint"), EyesLocation.Equals(Turret->GetPawnViewLocation(), 0.001));
			TestTrue(TEXT("Attack control rotation follows the actual head direction"), AI->GetControlRotation().Vector().Equals(HeadDirection, 0.001));
		}
	}
	TestTrue(TEXT("Head aiming never turns the turret body"), Turret->GetActorQuat().Equals(BodyRotation, 0.0001));
	return true;
}

#endif
