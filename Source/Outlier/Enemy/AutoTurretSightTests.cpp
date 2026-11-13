#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Misc/AutomationTest.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Team/OutlierTeamIds.h"
#include "Room/RoomTagComponent.h"
#include "UObject/UnrealType.h"
#include "Weapon/LaserWeapon.h"
#include <limits>

namespace
{
struct FTurretSightWorld
{
	UWorld* World = nullptr;
	FTurretSightWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TurretSightWorld")));
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
	}
	~FTurretSightWorld()
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	UBoxComponent* Block(const FVector& Location, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetWorldLocation(Location);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		return Box;
	}
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FAutoTurretSightTest,
	"Outlier.Enemy.Turret.Sight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FAutoTurretSightTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : { TEXT("HeadOnlyAndLastObservation"), TEXT("RealPerceptionTeamChanges"),
		TEXT("RifleAimAndDamage"), TEXT("LaserAimAndDamage"), TEXT("RifleMuzzleOcclusion"), TEXT("LaserCommitOcclusion") })
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FAutoTurretSightTest::RunTest(const FString& Parameters)
{
	FTurretSightWorld Fixture;
	const bool bLaser = Parameters.StartsWith(TEXT("Laser"));
	const bool bWeaponTest = bLaser || Parameters.StartsWith(TEXT("Rifle"));
	UClass* ObserverClass = AAutoTurret::StaticClass();
	if (bWeaponTest)
	{
		ObserverClass = LoadClass<AAutoTurret>(nullptr, bLaser
			? TEXT("/Game/Blueprints/Enemy/Turret/BP_LaserTurret.BP_LaserTurret_C")
			: TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C"));
		if (!TestNotNull(TEXT("Actual observer turret BP"), ObserverClass)) return false;
	}
	AAutoTurret* Observer = Fixture.World->SpawnActor<AAutoTurret>(ObserverClass);
	AAutoTurret* Target = Fixture.World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Observer turret"), Observer) || !TestNotNull(TEXT("Target turret"), Target)) return false;
	USkeletalMesh* Head = LoadObject<USkeletalMesh>(nullptr, bLaser
		? TEXT("/Game/Characters/Enemy/Turret/Meshes/Laser/SKM_LaserTurretHead.SKM_LaserTurretHead")
		: TEXT("/Game/Characters/Enemy/Turret/Meshes/Rifle/SKM_RifleTurretHead.SKM_RifleTurretHead"));
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Enemy/Turret/Meshes/Rifle/SKM_RifleTurretBase.SKM_RifleTurretBase"));
	if (!TestNotNull(TEXT("Actual turret Head asset"), Head) || !TestNotNull(TEXT("Actual turret Body asset"), Body)) return false;
	for (AAutoTurret* Turret : { Observer, Target })
	{
		Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
		Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
		FEnemyStat& Stat = const_cast<FEnemyStat&>(Turret->GetRuntimeStat());
		Stat.SightRadius = Stat.LoseSightRadius = 2000.0f;
		Stat.PeripheralVisionAngle = Stat.BattlePeripheralVisionAngle = 90.0f;
		Turret->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
		Turret->SetActorHiddenInGame(false);
	}
	Observer->bHackedToPlayerTeam = true;
	Observer->ApplyHackedTeamState();
	Observer->TurretHeadPivot->SetWorldLocation(FVector(0, 0, 150));
	const float TargetDistance = bWeaponTest ? 1200.0f : 600.0f;
	Target->SetActorLocation(FVector(TargetDistance, 0, -150));
	Target->TurretHeadMesh->SetSkeletalMeshAsset(Head);
	Target->GetMesh()->SetSkeletalMeshAsset(Body);
	for (USkeletalMeshComponent* Mesh : { Target->TurretHeadMesh.Get(), Target->GetMesh() })
	{
		Mesh->SetAnimInstanceClass(nullptr);
		Mesh->RefreshBoneTransforms();
		Mesh->UpdateBounds();
	}
	const auto MoveBoundsCenter = [](USkeletalMeshComponent* Mesh, const FVector& Center)
	{
		Mesh->SetWorldLocation(Mesh->GetComponentLocation() + Center - Mesh->Bounds.Origin);
		Mesh->RefreshBoneTransforms();
		Mesh->UpdateBounds();
	};
	MoveBoundsCenter(Target->GetMesh(), FVector(TargetDistance, 0, -150));
	MoveBoundsCenter(Target->TurretHeadMesh, FVector(TargetDistance, 0, 150));
	Target->ApplyTurretCollisionState();
	Fixture.Block(FVector(TargetDistance, 0, -110), FVector(200, 250, 110));
	FCanBeSeenFromContext Context;
	Context.IgnoreActor = Observer;
	Context.ObserverLocation = Observer->GetPawnViewLocation();
	FVector Seen;
	int32 Sync = 0, Async = 0;
	float Strength = 0;
	auto Query = [&]() { return Target->CanBeSeenFrom(Context, Seen, Sync, Async, Strength); };
	if (!TestTrue(TEXT("Exposed real Head is visible above a buried origin"), Query() == UAISense_Sight::EVisibilityResult::Visible)) return false;
	TestTrue(TEXT("At most two finite sample traces"), Sync > 0 && Sync <= 2 && Async == 0);
	TestTrue(TEXT("Sight endpoint is inside the exposed Head bounds, not buried Body or capsule"),
		Seen.Z > 0.0 && Target->TurretHeadMesh->Bounds.GetBox().IsInsideOrOn(Seen));
	TestTrue(TEXT("Common aim consumes the exact observer endpoint"), Observer->GetCombatAimPoint(Target).Equals(Seen, 0.1));

	if (Parameters == TEXT("HeadOnlyAndLastObservation"))
	{
		const FVector LastSeen = Seen;
		UBoxComponent* Wall = Fixture.Block(FVector(300, 0, 150), FVector(15, 500, 500));
		TestTrue(TEXT("A complete wall blocks every sample"), Query() == UAISense_Sight::EVisibilityResult::NotVisible);
		Target->TurretHeadMesh->AddWorldOffset(FVector(0, 100, 0));
		TestTrue(TEXT("Hidden Head movement does not change the last observed aim"), Observer->GetCombatAimPoint(Target).Equals(LastSeen, 0.1));
		FVector CurrentPoint;
		TestFalse(TEXT("Lost sight cannot authorize normal turret fire"), Target->TryGetObservedSightPoint(Observer, CurrentPoint, true));
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Removing the wall reacquires the moved Head"), Query() == UAISense_Sight::EVisibilityResult::Visible && !Seen.Equals(LastSeen));
		Observer->ResetTargetAttackContinuity();
		TestFalse(TEXT("Observer lifecycle reset invalidates old snapshots"), Target->TryGetObservedSightPoint(Observer, CurrentPoint));
		Query();
		Target->SetTurretLifecycleState(EAutoTurretLifecycleState::WaitingForWave);
		TestTrue(TEXT("Inactive target is not visible"), Query() == UAISense_Sight::EVisibilityResult::NotVisible);
		TestFalse(TEXT("Target lifecycle reset clears old snapshots"), Target->TryGetObservedSightPoint(Observer, CurrentPoint));
		Target->SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
		Target->TurretHeadMesh->SetSkeletalMeshAsset(nullptr);
		Target->AutoAimBodyAnchor->UnregisterComponent();
		TestTrue(TEXT("Missing Head uses only blocked Body fallback"), Query() == UAISense_Sight::EVisibilityResult::NotVisible);
		Context.ObserverLocation.X = std::numeric_limits<double>::quiet_NaN();
		TestTrue(TEXT("Invalid observer position is rejected without traces"), Query() == UAISense_Sight::EVisibilityResult::NotVisible && Sync == 0);
		return true;
	}

	AEnemyAIController* ObserverAI = Fixture.World->SpawnActor<AEnemyAIController>();
	AEnemyAIController* TargetAI = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("Observer controller"), ObserverAI) || !TestNotNull(TEXT("Target controller"), TargetAI)) return false;
	ObserverAI->Possess(Observer);
	TargetAI->Possess(Target);
	// 시야는 논리 조준값을 읽는다. 피벗 Transform만 바꾸면 실제 listener는 계속 정면을 본다.
	Target->UpdateTurretAimAtLocation(Observer->GetPawnViewLocation(), 10.0f, false);
	UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(Fixture.World);
	if (!TestNotNull(TEXT("Real perception system"), Perception)) return false;
	UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Observer);
	UAIPerceptionSystem::RegisterPerceptionStimuliSource(Fixture.World, UAISense_Sight::StaticClass(), Target);
	const auto Advance = [&]()
	{
		for (int32 Frame = 0; Frame < 16; ++Frame)
		{
			++GFrameCounter;
			Fixture.World->Tick(LEVELTICK_All, 0.025f);
			Perception->Tick(0.025f);
		}
	};
	const auto Perceives = [](AEnemyAIController* AI, AActor* Actor)
	{
		TArray<AActor*> SeenActors;
		AI->GetPerceptionComponent()->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), SeenActors);
		return SeenActors.Contains(Actor);
	};
	if (Parameters == TEXT("RealPerceptionTeamChanges"))
	{
		// 실제 해킹 경로는 Room 등록 AI의 이전 감지를 지운다. 이 수명주기도 fixture에 포함한다.
		UEnemyRoomSubsystem* Rooms = Fixture.World->GetSubsystem<UEnemyRoomSubsystem>();
		if (!TestNotNull(TEXT("Production room perception registry"), Rooms)) return false;
		for (AAutoTurret* Turret : { Observer, Target })
		{
			Turret->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1")));
			Rooms->RegisterEnemy(Turret);
		}
		// 최초에는 같은 Enemy로 등록하고 해킹/해제에서 listener와 source 양쪽 캐시를 검증한다.
		Observer->bHackedToPlayerTeam = false;
		Observer->ApplyHackedTeamState();
		Advance();
		TestFalse(TEXT("Same-team sources are not perceived as hostile"), Perceives(ObserverAI, Target));
		Observer->bHackedToPlayerTeam = true;
		Observer->ApplyHackedTeamState();
		Advance();
		TestTrue(TEXT("Real Sight acquires the hostile turret after hacking"), Perceives(ObserverAI, Target));
		TestTrue(TEXT("Other turret also recognizes the changed source team"), Perceives(TargetAI, Observer));
		UBoxComponent* Wall = Fixture.Block(FVector(300, 0, 150), FVector(15, 500, 500));
		Advance();
		TestFalse(TEXT("Real wall removes current perception without injected stimuli"), Perceives(ObserverAI, Target));
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Advance();
		TestTrue(TEXT("Wall removal reacquires actual perception"), Perceives(ObserverAI, Target));
		Observer->bHackedToPlayerTeam = false;
		Observer->ApplyHackedTeamState();
		Advance();
		TestFalse(TEXT("Hack expiry excludes the former hostile target"), Perceives(ObserverAI, Target));
		TestFalse(TEXT("Reverse listener forgets the now-friendly source"), Perceives(TargetAI, Observer));
		Observer->bHackedToPlayerTeam = true;
		Observer->ApplyHackedTeamState();
		Advance();
		TestTrue(TEXT("Re-hack reacquires both directions"), Perceives(ObserverAI, Target) && Perceives(TargetAI, Observer));
		return true;
	}

	UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr, bLaser
		? TEXT("/Game/Blueprints/Enemy/Turret/Weapon/BP_TurretLaser.BP_TurretLaser_C")
		: TEXT("/Game/Blueprints/Enemy/Turret/Weapon/BP_TurretRifle.BP_TurretRifle_C"));
	if (!TestNotNull(TEXT("Actual turret weapon BP"), WeaponClass)) return false;
	ARangedWeaponBase* Weapon = Fixture.World->SpawnActor<ARangedWeaponBase>(WeaponClass);
	FObjectPropertyBase* CurrentWeaponProperty = FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"));
	if (!TestNotNull(TEXT("Real weapon instance"), Weapon) || !TestNotNull(TEXT("Current weapon property"), CurrentWeaponProperty)) return false;
	Observer->TurretHeadMesh->SetSkeletalMeshAsset(Head);
	Observer->TurretHeadMesh->RefreshBoneTransforms();
	Observer->CacheAimOriginMuzzleSockets();
	Weapon->OnEquipped(Observer);
	// 부위 조준/실제 적중 검증에서는 산포 난수를 분리한다. BP의 발사/총구/피해 경로는 그대로 실행한다.
	FindFProperty<FObjectPropertyBase>(WeaponClass, TEXT("WeaponBloomTable"))->SetObjectPropertyValue_InContainer(Weapon, nullptr);
	for (const TCHAR* Property : { TEXT("BloomCurrent"), TEXT("BloomMin"), TEXT("BloomMax"), TEXT("BloomPerShot") })
		FindFProperty<FFloatProperty>(WeaponClass, Property)->SetPropertyValue_InContainer(Weapon, 0.0f);
	if (!bLaser)
	{
		FindFProperty<FBoolProperty>(WeaponClass, TEXT("bInfiniteAmmo"))->SetPropertyValue_InContainer(Weapon, false);
		FindFProperty<FIntProperty>(WeaponClass, TEXT("CurrentAmmo"))->SetPropertyValue_InContainer(Weapon, 10);
	}
	const auto HasLaserShot = [&]()
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(WeaponClass, TEXT("LaserPresentation"));
		const FLaserWeaponPresentation* Presentation = Property ? Property->ContainerPtrToValuePtr<FLaserWeaponPresentation>(Weapon) : nullptr;
		return Presentation && Presentation->Phase == ELaserWeaponPresentationPhase::Firing && Presentation->bHasImpact;
	};
	CurrentWeaponProperty->SetObjectPropertyValue_InContainer(Observer, Weapon);
	Advance();
	Context.ObserverLocation = Observer->GetPawnViewLocation();
	if (!TestTrue(TEXT("Weapon case has real current perception"), Perceives(ObserverAI, Target))) return false;
	Query();
	Observer->EnterCombatFromRoom(Seen, false, true);
	Observer->SetPlayerCurrentlyVisible(true);
	Observer->ObserveTrackedTarget(Target);
	Observer->BeginTargetAttackPreparation(0.0f);
	for (int32 Step = 0; Step < 30; ++Step) Observer->UpdateTurretAimAtLocation(Seen, 0.1f, true);
	Query();
	int32 Checks = 8;
	TestTrue(TEXT("Actual muzzle path reaches the observed Head"), Weapon->HasClearTargetAttackPath(Target, Seen, Checks));
	const float BeforeHealth = Target->GetCurrentHealth();
	const int32 BeforeAmmo = Weapon->GetCurrentAmmo();
	if (Parameters == TEXT("RifleMuzzleOcclusion"))
	{
		// 총구 그룹의 각 사선에 작은 엄폐물을 놓고 평균 시점에서 Head로 가는 선은 비워 둔다.
		TArray<FName> Sockets;
		Observer->GetWeaponMuzzleSocketNames(false, Sockets);
		if (!TestTrue(TEXT("Production rifle has real muzzle sockets"), !Sockets.IsEmpty())) return false;
		for (FName Socket : Sockets)
		{
			const FVector Muzzle = Observer->TurretHeadMesh->GetSocketLocation(Socket);
			const FVector BlockPoint = FMath::Lerp(Muzzle, Seen, 0.1);
			const float EyeDistance = FMath::PointDistToSegment(BlockPoint, Context.ObserverLocation, Seen);
			if (!TestTrue(TEXT("Real muzzle ray is distinct from the average eye ray"), EyeDistance > 2.0f)) return false;
			Fixture.Block(BlockPoint, FVector(FMath::Min(1.0f, EyeDistance * 0.25f)));
		}
		TestTrue(TEXT("Eyes still see the Head past muzzle-only cover"), Query() == UAISense_Sight::EVisibilityResult::Visible);
		Checks = 16;
		TestFalse(TEXT("Actual muzzle cover blocks normal attack eligibility"), Weapon->HasClearTargetAttackPath(Target, Seen, Checks));
		TestFalse(TEXT("Normal turret does not fire through muzzle cover"), Observer->StartAttackTarget(Target));
		TestEqual(TEXT("Muzzle cover prevents authoritative damage"), Target->GetCurrentHealth(), BeforeHealth);
		return true;
	}
	if (Parameters == TEXT("LaserCommitOcclusion"))
	{
		const FVector LockedPoint = Seen;
		CastChecked<ALaserWeapon>(Weapon)->BeginCharge(0.0f, 0.1f);
		Fixture.Block(FVector(300, 0, 150), FVector(15, 500, 500));
		TestTrue(TEXT("Charging laser loses real Head sight behind a wall"), Query() == UAISense_Sight::EVisibilityResult::NotVisible);
		Target->TurretHeadMesh->AddWorldOffset(FVector(0, 100, 0));
		TestTrue(TEXT("Laser override retains the last observed point after hidden Head movement"), Observer->GetCombatAimPoint(Target).Equals(LockedPoint, 0.1));
		TestTrue(TEXT("Committed laser still fires at its stored point without a new LOS wait"), Observer->StartAttackLocation(LockedPoint));
		TestTrue(TEXT("Committed laser produces an actual impact snapshot"), HasLaserShot());
		TestEqual(TEXT("The real wall absorbs the laser with no target damage"), Target->GetCurrentHealth(), BeforeHealth);
		Observer->StopCurrentAttack();
		return true;
	}
	TArray<FName> ShotSockets;
	Observer->GetWeaponMuzzleSocketNames(false, ShotSockets);
	for (FName Socket : ShotSockets)
	{
		const FVector Muzzle = Observer->TurretHeadMesh->GetSocketLocation(Socket);
		const FVector Direction = ObserverAI->GetControlRotation().Vector();
		FHitResult PredictedHit;
		FCollisionQueryParams ShotParams;
		ShotParams.AddIgnoredActor(Observer);
		ShotParams.AddIgnoredActor(Weapon);
		Fixture.World->LineTraceSingleByChannel(PredictedHit, Muzzle, Muzzle + Direction * Weapon->GetEffectiveRange(), ECC_PhysicsBody, ShotParams);
		AddInfo(FString::Printf(TEXT("Shot boundary Socket=%s Muzzle=%s Aim=%s Forward=%s ErrorDegrees=%.3f Tolerance=%.3f Spread=%.3f Hit=%s Component=%s Bone=%s HeadBounds=%s"),
			*Socket.ToString(), *Muzzle.ToCompactString(), *Seen.ToCompactString(), *Direction.ToCompactString(),
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((Seen - Muzzle).GetSafeNormal(), Direction), -1.0, 1.0))),
			Observer->GetTurretBehavior().AimToleranceDegrees, Weapon->GetCurrentSpread(),
			*GetNameSafe(PredictedHit.GetActor()), *GetNameSafe(PredictedHit.GetComponent()), *PredictedHit.BoneName.ToString(),
			*Target->TurretHeadMesh->Bounds.GetBox().ToString()));
	}
	const bool bStarted = bLaser ? Observer->StartAttackLocation(Seen) : Observer->StartAttackTarget(Target);
	TestTrue(TEXT("Prepared aligned actual weapon accepts the shot"), bStarted);
	TestTrue(TEXT("Accepted request produces an actual shot"), bLaser ? HasLaserShot() : Weapon->GetCurrentAmmo() < BeforeAmmo);
	TestTrue(TEXT("Real hitscan applies authoritative damage to the exposed Head"), Target->GetCurrentHealth() < BeforeHealth);
	Observer->StopCurrentAttack();
	return true;
}

#endif
