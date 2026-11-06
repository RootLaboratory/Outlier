#if WITH_DEV_AUTOMATION_TESTS

#include "Weapon/WallShotProbe.h"
#include "Weapon/RangedWeaponBase.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

WallShotProbe::FObserved& WallShotProbe::Observed()
{
	static FObserved Delegate;
	return Delegate;
}

bool FWallShotProbeAccess::Resolve(ARangedWeaponBase* Weapon, FTransform& Out)
{
	return Weapon->ResolveShotTransform(NAME_None, Out);
}

bool FWallShotProbeAccess::ResolveWall(ARangedWeaponBase* Weapon, ACharacter* Owner,
	const FVector& View, const FRotator& Rotation, FVector& Start, FVector& Direction)
{
	return Weapon->TryResolveWallOffsetMuzzleShot(Owner, NAME_None, View, Rotation, Start, Direction);
}

void FWallShotProbeAccess::ConfigureControlledShot(ARangedWeaponBase* Weapon)
{
	// 통제 조건만 바꾼다. 생산 코드의 충돌/총구 계산과 실제 탄약 소비는 유지한다.
	Weapon->WeaponBloomTable = nullptr;
	Weapon->BloomMin = Weapon->BloomMax = Weapon->BloomCurrent = Weapon->BloomPerShot = 0;
	Weapon->RecoilMultiplier = 0;
	// 무한 탄약 에셋도 비용 검증에서는 유한 탄창을 사용한다.
	Weapon->bInfiniteAmmo = false;
}

void FWallShotProbeAccess::AttackOnce(ARangedWeaponBase* Weapon)
{
	Weapon->StartAttack();
	Weapon->StopAttack();
}

FWeaponShotSnapshot FWallShotProbeAccess::GetLastShot(const ARangedWeaponBase* Weapon)
{
	return Weapon->LastResolvedShot;
}

void FWallShotProbeAccess::Present(ARangedWeaponBase* Weapon, const FWeaponShotSnapshot& Shot)
{
	Weapon->MulticastPlayFireFX_Implementation(Shot);
}

bool FWallShotProbeAccess::Occlusion(ARangedWeaponBase* Weapon, const FVector& View, const FVector& Start, FHitResult& Hit)
{
	FCollisionQueryParams Params;
	Weapon->AddWeaponTraceIgnoredActors(Params);
	return Weapon->ResolveShooterOcclusion(View, Start, Params, Hit);
}

bool FWallShotProbeAccess::Trace(ARangedWeaponBase* Weapon, const FVector& Start, const FVector& End, FHitResult& Hit)
{
	FCollisionQueryParams Params;
	Weapon->AddWeaponTraceIgnoredActors(Params);
	return Weapon->TraceShooterShot(Start, End, Params, Hit);
}

void FWallShotProbeAccess::ConfigureShotCosts(ARangedWeaponBase* Weapon)
{
	FWeaponRecoilRow Profile;
	Profile.ControlPitchPerShot = 0.25f;
	Profile.MaxAccumulatedPitchDegrees = 5.0f;
	Profile.RecoilResetDelay = 100.0f;
	Weapon->CachedHipRecoilProfile = Weapon->CachedADSRecoilProfile = Weapon->CachedAnyRecoilProfile = Profile;
	Weapon->bHasCachedHipRecoilProfile = Weapon->bHasCachedADSRecoilProfile = Weapon->bHasCachedAnyRecoilProfile = true;
	Weapon->ActiveRecoilProfile = Profile;
	Weapon->bHasActiveRecoilProfile = true;
	Weapon->RecoilMultiplier = 1.0f;
	Weapon->ReuseCooldown = 0.2f;
	Weapon->CurrentAmmo = 30;
}

uint32 FWallShotProbeAccess::GetRecoilSequence(const ARangedWeaponBase* Weapon)
{
	return Weapon->RecoilShotSequence;
}

void FWallShotProbeAccess::SetAttackGate(ARangedWeaponBase* Weapon, int32 Ammo, bool bReloading)
{
	Weapon->CurrentAmmo = Ammo;
	Weapon->bIsReloading = bReloading;
}

namespace
{
constexpr float WallShotTickInterval = 1.0f / 60.0f;

struct FWallShotWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	AShooterCharacter* Shooter = nullptr;
	ARangedWeaponBase* Weapon = nullptr;
	APlayerController* Controller = nullptr;
	UBoxComponent* Wall = nullptr;
	bool bDedicated = false;

	bool Initialize(FAutomationTestBase& Test, const TCHAR* WeaponName, bool bServer)
	{
		bDedicated = bServer;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("WallShotBaseline")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Baseline world"), World)) return false;
		World->AddToRoot();
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.RunAsDedicated = bServer;
		Context.SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		URL.Port = 0;
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		if (bServer) World->Listen(URL);
		World->BeginPlay();
		if (!Test.TestTrue(TEXT("Requested world mode"), World->GetNetMode() == (bServer ? NM_DedicatedServer : NM_Standalone))) return false;

		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		const FString Path = FString::Printf(TEXT("/Game/Blueprints/Weapon/BP_%s.BP_%s_C"), WeaponName, WeaponName);
		UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr, *Path);
		if (!Test.TestNotNull(TEXT("Production Shooter BP"), ShooterClass)
			|| !Test.TestNotNull(TEXT("Production weapon BP"), WeaponClass)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
		Weapon = World->SpawnActor<ARangedWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Shooter instance"), Shooter) || !Test.TestNotNull(TEXT("Weapon instance"), Weapon)
			|| !Test.TestNotNull(TEXT("Controller instance"), Controller)) return false;
		Controller->Possess(Shooter);
		Controller->SetViewTarget(Shooter);
		if (!bServer)
		{
			Controller->Player = NewObject<ULocalPlayer>(GEngine);
			Controller->SetAsLocalPlayerController();
		}
		Controller->PlayerCameraManager->bUseClientSideCameraUpdates = bServer;
		Shooter->GetCharacterMovement()->DisableMovement();
		if (!Test.TestTrue(TEXT("Suit presentation applies"), Shooter->SetSuitPresentation(true))) return false;
		Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
		Shooter->RefreshWeaponMode();
		FWallShotProbeAccess::ConfigureControlledShot(Weapon);
		// NullRHI는 렌더하지 않는다. 로컬 팔만 pose를 평가하고 서버의 기본 갱신 정책은 보존한다.
		if (!bServer)
		{
			Shooter->GetFirstPersonMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Weapon->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		AActor* WallActor = World->SpawnActor<AActor>();
		Wall = NewObject<UBoxComponent>(WallActor);
		WallActor->SetRootComponent(Wall);
		Wall->SetBoxExtent(FVector(2, 500, 500));
		Wall->SetCollisionResponseToAllChannels(ECR_Block);
		Wall->RegisterComponent();
		Wall->SetWorldLocation(FVector(500, 0, 1100));
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tick(90);
		return Test.TestTrue(TEXT("Requested owner locality"), Shooter->IsLocallyControlled() == !bServer)
			&& Test.TestTrue(TEXT("Actual muzzle socket exists"), Weapon->GetFirstPersonWeaponMesh()->DoesSocketExist(Weapon->GetMuzzleSocketName()));
	}

	void Tick(int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			Controller->SetControlRotation(FRotator::ZeroRotator);
			if (bDedicated) Controller->ServerUpdateCamera_Implementation(Shooter->GetPawnViewLocation(), 0);
			else Controller->UpdateCameraManager(WallShotTickInterval);
			World->Tick(LEVELTICK_All, WallShotTickInterval);
		}
	}

	~FWallShotWorld()
	{
		if (!World) return;
		if (Controller) Controller->Player = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (Instance) Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierWallShotBaselineTest, "Outlier.Weapon.WallShot.Baseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWallShotBaselineTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	TArray<FString> Rows;
	Rows.Add(TEXT("weapon,dedicated,wall_x,uses_muzzle,start_x,start_y,start_z,direction_x,direction_y,direction_z"));
	for (const TCHAR* Name : {TEXT("Pistol"), TEXT("Rifle")})
	{
		for (bool bServer : {false, true})
		{
			FWallShotWorld Fixture;
			if (!Fixture.Initialize(*this, Name, bServer)) return false;
			FVector View; FRotator Rotation;
			// 판정 기준은 서버가 해결한 시점이다. 원격 Controller의 보고된 카메라는 표시/수신 시각이 다르다.
			const FTransform ResolvedView = Fixture.Shooter->GetWallShotViewTransform();
			View = ResolvedView.GetLocation();
			Rotation = ResolvedView.Rotator();
			FTransform OpenShot;
			TestTrue(TEXT("Open shot resolves"), FWallShotProbeAccess::Resolve(Fixture.Weapon, OpenShot));
			TestTrue(TEXT("W02 open shot starts at viewpoint"), OpenShot.GetLocation().Equals(View, 0.1));
			TestTrue(TEXT("W02 open shot follows camera"), OpenShot.GetUnitAxis(EAxis::X).Equals(Rotation.Vector(), 0.001));

			// 기존 raw 분기 실패 기록은 Saved에 보존한다. 새 구현에서는 실제 시간 경과에 따른
			// 공통 상태의 진입을 기록하며, 0.05 경계 자체는 Slice3의 통제 입력으로 검증한다.
			Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			bool bSawCamera = false, bSawMuzzle = false;
			for (int32 Distance = 200; Distance >= 20; --Distance)
			{
				Fixture.Wall->SetWorldLocation(FVector(View.X + Distance, View.Y, View.Z));
				Fixture.Tick(1);
				FVector Start = View, Direction = Rotation.Vector();
				const bool bMuzzle = FWallShotProbeAccess::ResolveWall(Fixture.Weapon, Fixture.Shooter, View, Rotation, Start, Direction);
				bSawCamera |= !bMuzzle;
				bSawMuzzle |= bMuzzle;
				FTransform Common;
				TestTrue(TEXT("Baseline common shot resolves"), Fixture.Shooter->TryGetWallShotTransform(Common));
				TestTrue(TEXT("Baseline shot uses common state during approach"), Common.GetLocation().Equals(Start, 0.1)
					&& Common.GetUnitAxis(EAxis::X).Equals(Direction, 0.001));
				Rows.Add(FString::Printf(TEXT("%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"), Name, bServer,
					Distance, bMuzzle, Start.X, Start.Y, Start.Z, Direction.X, Direction.Y, Direction.Z));
			}
			TestTrue(TEXT("Approach records both open and avoiding states"), bSawCamera && bSawMuzzle);

			Fixture.Wall->SetWorldLocation(View + FVector(35, 0, 0));
			Fixture.Tick(90);
			const int32 Before = Fixture.Weapon->GetCurrentAmmo();
			int32 ObservedShots = 0;
			const FDelegateHandle Handle = WallShotProbe::Observed().AddLambda([&](const FWallShotObservation& Shot)
			{
				if (Shot.Weapon != Fixture.Weapon || Shot.bPresentation) return;
				++ObservedShots;
				const bool bStoppedAtWall = Shot.bHit && Shot.End.X <= Fixture.Wall->GetComponentLocation().X + 2.1;
				TestTrue(TEXT("W05 blocked shot terminates at front wall"), bStoppedAtWall);
			});
			FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
			WallShotProbe::Observed().Remove(Handle);
			TestEqual(TEXT("One production attack produces one observed shot"), ObservedShots, 1);
			TestEqual(TEXT("Blocked attack consumes one round"), Fixture.Weapon->GetCurrentAmmo(), Before - 1);
		}
	}
	const FString Output = FPaths::ProjectSavedDir() / TEXT("WallShotValidation/Baseline.csv");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	TestTrue(TEXT("Baseline CSV saved"), FFileHelper::SaveStringArrayToFile(Rows, *Output));
	AddInfo(TEXT("Baseline inspects actual BP weapons. Network ownership/rendering and W01/W04/W06-W11 are separate checks; this is not a visual verdict."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierWallShotSlice3Test, "Outlier.Weapon.WallShot.Slice3",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWallShotSlice3Test::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	for (const TCHAR* Name : {TEXT("Pistol"), TEXT("Rifle")})
	{
		for (bool bServer : {false, true})
		{
			FWallShotWorld Fixture;
			if (!Fixture.Initialize(*this, Name, bServer)) return false;
			const FVector View = Fixture.Shooter->GetWallShotViewTransform().GetLocation();
			Fixture.Shooter->GetFirstPersonMesh()->SetComponentTickEnabled(false);
			Fixture.Weapon->GetFirstPersonWeaponMesh()->SetComponentTickEnabled(false);
			const auto CompareQuery = [&]()
			{
				FTransform Expected, Actual;
				TestTrue(TEXT("Common query resolves without mesh ticks"), Fixture.Shooter->TryGetWallShotTransform(Expected));
				TestTrue(TEXT("Weapon query resolves without mesh ticks"), FWallShotProbeAccess::Resolve(Fixture.Weapon, Actual));
				TestTrue(TEXT("Production weapon consumes common shot transform"), Expected.Equals(Actual, 1.e-6));
				return Actual;
			};
			const FTransform Open = CompareQuery();
			TestTrue(TEXT("Open shot returns exact resolved viewpoint"), Open.Equals(Fixture.Shooter->GetWallShotViewTransform(), 1.e-6));

			// 이전 raw 0.05 경계의 양쪽을 같은 캐시/포즈로 조회한다. 조회가 상태를 진행시키면 실패한다.
			FWeaponWallAvoidanceState& State = const_cast<FWeaponWallAvoidanceState&>(Fixture.Shooter->GetWeaponWallAvoidanceState());
			const FWeaponWallAvoidanceState SavedState = State;
			State.Reset();
			State.WallOffsetAlpha = 0.0499f;
			State.ViewModelWallOffsetLoc = FVector(0, -4, -0.5);
			State.ViewModelWallOffsetRot = FRotator(12, -8, 17);
			const FTransform Below = CompareQuery();
			State.WallOffsetAlpha = 0.0501f;
			const FTransform Above = CompareQuery();
			TestTrue(TEXT("Production query has no raw threshold position jump"), FVector::Distance(Below.GetLocation(), Above.GetLocation()) < 0.1);
			TestTrue(TEXT("Production query has no raw threshold angle jump"), FMath::RadiansToDegrees(Below.GetRotation().AngularDistance(Above.GetRotation())) < 0.1);
			State = SavedState;
			FWallShotProbeAccess::ConfigureShotCosts(Fixture.Weapon);
			FActorSpawnParameters TargetSpawn;
			TargetSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// 실제 Enemy 설정은 BeginPlay에서 유효한 초기 체력을 제공한다. 기본 native 클래스는 Health=0이다.
			UClass* TargetClass = LoadClass<AEnemyBase>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
			if (!TestNotNull(TEXT("Production damage receiver class"), TargetClass)) return false;
			AEnemyBase* Target = Fixture.World->SpawnActor<AEnemyBase>(TargetClass, View + FVector(300, 0, 0), FRotator::ZeroRotator, TargetSpawn);
			if (!TestNotNull(TEXT("Real damage receiver behind wall"), Target)) return false;
			Target->RemoveEnemyTraitForTesting(OutlierGameplayTags::Enemy::Adaptation::Target());
			// 통제 조건에서 피해 허용을 명시한다.
			Target->SetCanBeDamaged(true);
			Target->SetActorTickEnabled(false);
			Target->GetCharacterMovement()->DisableMovement();
			if (AController* TargetController = Target->GetController()) TargetController->UnPossess();
			Target->GetOutlierAbilitySystemComponent()->bSuppressGameplayCues = true;
			TestTrue(TEXT("Damage target vitality initializes"), Target->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(10000.0f));
			UBoxComponent* TargetBody = NewObject<UBoxComponent>(Target);
			Target->AddInstanceComponent(TargetBody);
			TargetBody->SetupAttachment(Target->GetRootComponent());
			TargetBody->SetBoxExtent(FVector(10, 100, 100));
			TargetBody->SetCollisionResponseToAllChannels(ECR_Ignore);
			TargetBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
			TargetBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			TargetBody->RegisterComponent();
			// 통제용 피해 바디를 월드에 확정한 뒤 실제 Trace로 fixture를 먼저 검증한다.
			TargetBody->SetWorldLocation(View + FVector(300, 0, 0));
			Fixture.Tick(1);
			FHitResult PositiveControlHit;
			TestTrue(TEXT("Positive-control collision is registered in world"), FWallShotProbeAccess::Trace(Fixture.Weapon, View, View + FVector(1000, 0, 0), PositiveControlHit));
			TestTrue(TEXT("Positive-control trace reaches real receiver"), PositiveControlHit.GetActor() == Target);
			AddInfo(FString::Printf(TEXT("ReceiverFixture %s dedicated=%d View=%s Actor=%s Body=%s Hit=%s Point=%s"), Name, bServer,
				*View.ToString(), *Target->GetActorLocation().ToString(), *TargetBody->GetComponentLocation().ToString(),
				*GetNameSafe(PositiveControlHit.GetActor()), *PositiveControlHit.ImpactPoint.ToString()));
			TestTrue(TEXT("Positive-control receiver accepts damage"), Target->CanBeDamaged() && !Target->IsDead());
			const float TargetInitialHealth = Target->GetCurrentHealth();
			const uint32 OpenRecoil = FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon);
			FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
			TestTrue(TEXT("Unblocked positive control damages real receiver"), Target->GetCurrentHealth() < TargetInitialHealth);
			AddInfo(FString::Printf(TEXT("ReceiverResult Damage=%.2f HealthBefore=%.2f HealthAfter=%.2f Dead=%d CanDamage=%d Hit=%s"), Fixture.Weapon->GetDamageAtDistance(300), TargetInitialHealth, Target->GetCurrentHealth(), Target->IsDead(), Target->CanBeDamaged(), *GetNameSafe(FWallShotProbeAccess::GetLastShot(Fixture.Weapon).HitActor)));
			TestEqual(TEXT("Open attack applies recoil once"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), OpenRecoil + 1);
			Target->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(10000.0f);

			for (int32 Distance : {150, 70, 35, 70, 150})
			{
				Fixture.Wall->SetWorldLocation(View + FVector(Distance, 0, 0));
				Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Fixture.Tick(90);
				const FTransform Expected = CompareQuery();
				const int32 AmmoBefore = Fixture.Weapon->GetCurrentAmmo();
				const uint32 BeforeId = FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId;
				const uint32 BeforeRecoil = FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon);
				FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
				const FWeaponShotSnapshot Shot = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
				TestEqual(TEXT("One production attack consumes one round"), Fixture.Weapon->GetCurrentAmmo(), AmmoBefore - 1);
				TestEqual(TEXT("Each production attack advances shot identity once"), Shot.ShotId, BeforeId + 1);
				TestEqual(TEXT("Blocked attack applies recoil once"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), BeforeRecoil + 1);
				TestEqual(TEXT("Blocked attack does not damage receiver behind wall"), Target->GetCurrentHealth(), TargetInitialHealth);
				TestTrue(TEXT("Blocked attack commits normal reuse cooldown"), Fixture.Weapon->IsOnReuseCooldown() && Fixture.Weapon->GetReuseCooldownRemaining() > 0.0f);
				FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
				TestEqual(TEXT("Immediate attack cannot bypass cooldown"), FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId, Shot.ShotId);
				TestEqual(TEXT("Cooldown rejection consumes no extra ammunition"), Fixture.Weapon->GetCurrentAmmo(), AmmoBefore - 1);
				TestEqual(TEXT("Cooldown rejection adds no recoil"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), BeforeRecoil + 1);
				TestTrue(TEXT("Spread-free base direction follows common pose"), FVector(Shot.BaseDirection).Equals(Expected.GetUnitAxis(EAxis::X), 1.e-6));
				TestTrue(TEXT("Spread-free final direction is not changed again"), FVector(Shot.Direction).Equals(Shot.BaseDirection, 1.e-6));
				TestTrue(TEXT("Production shot terminates on front side of wall"), Shot.bHasImpact && Shot.End.X <= View.X + Distance + 2.1);
				if (Shot.bOccluded)
					TestTrue(TEXT("Occluded shot starts at safe viewpoint"), FVector(Shot.Start).Equals(View, 0.1));
				else
					TestTrue(TEXT("Unoccluded shot starts at common transform"), FVector(Shot.Start).Equals(Expected.GetLocation(), 0.1));
			}
			Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Fixture.Tick(360);
			TestEqual(TEXT("Avoidance releases completely without mesh ticks"), State.GetShotBlendAlpha(), 0.0f);
			TestTrue(TEXT("Released production query returns exact viewpoint"), CompareQuery().Equals(Open, 1.e-6));

			// 벽 위치/채널만 바꿔 실제 차폐 유틸리티를 검사한다. 별도 재구현한 판정과 비교하지 않는다.
			Fixture.Wall->SetBoxExtent(FVector(0.25, 500, 500));
			Fixture.Wall->SetWorldLocation(View + FVector(30, 0, 0));
			Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			for (ECollisionChannel Channel : {ECC_Visibility, ECC_PhysicsBody})
			{
				Fixture.Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
				Fixture.Wall->SetCollisionResponseToChannel(Channel, ECR_Block);
				for (double StartDistance : {30.0, 60.0})
				{
					FHitResult Hit;
					TestTrue(TEXT("Thin wall blocks muzzle inside or beyond it on either channel"), FWallShotProbeAccess::Occlusion(Fixture.Weapon, View, View + FVector(StartDistance, 0, 0), Hit));
					TestTrue(TEXT("Occlusion keeps first front surface"), Hit.ImpactPoint.X <= View.X + 30.1);
				}
				FHitResult Trace;
				TestTrue(TEXT("Final shot retains environmental block on either channel"), FWallShotProbeAccess::Trace(Fixture.Weapon, View, View + FVector(100, 0, 0), Trace));
				TestTrue(TEXT("Final trace chooses front wall surface"), Trace.GetComponent() == Fixture.Wall && Trace.ImpactPoint.X <= View.X + 30.1);
			}
			Fixture.Wall->SetCollisionResponseToAllChannels(ECR_Block);
			Fixture.Wall->SetWorldLocation(View);
			FHitResult Overlap;
			TestTrue(TEXT("Initial overlap is explicitly detected"), FWallShotProbeAccess::Occlusion(Fixture.Weapon, View, View, Overlap));
			TestTrue(TEXT("Initial overlap restores actual box face"), Overlap.bStartPenetrating && FMath::IsNearlyEqual(FMath::Abs(Overlap.ImpactPoint.X - View.X), 0.25, 0.1));
			const int32 BeforeOverlap = Fixture.Weapon->GetCurrentAmmo();
			FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
			const FWeaponShotSnapshot OverlapShot = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
			TestEqual(TEXT("Newly activated overlap still consumes one round"), Fixture.Weapon->GetCurrentAmmo(), BeforeOverlap - 1);
			TestTrue(TEXT("Newly activated wall blocks on actual box face before next wall-state update"), OverlapShot.bOccluded && OverlapShot.bStartPenetrating && FMath::IsNearlyEqual(FMath::Abs(OverlapShot.End.X - View.X), 0.25, 0.1));
			TestEqual(TEXT("Initial-overlap attack does not damage receiver behind wall"), Target->GetCurrentHealth(), TargetInitialHealth);
			Fixture.Tick(90);
			const uint32 BeforeGateId = FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId;
			const uint32 BeforeGateRecoil = FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon);
			FWallShotProbeAccess::SetAttackGate(Fixture.Weapon, 0, false);
			FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
			TestEqual(TEXT("Empty magazine cannot fire through wall policy"), FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId, BeforeGateId);
			TestEqual(TEXT("Empty magazine does not add recoil"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), BeforeGateRecoil);
			FWallShotProbeAccess::SetAttackGate(Fixture.Weapon, 30, true);
			FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
			TestEqual(TEXT("Reload blocks fire through wall policy"), FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId, BeforeGateId);
			TestEqual(TEXT("Reload rejection preserves ammo"), Fixture.Weapon->GetCurrentAmmo(), 30);
			TestEqual(TEXT("Reload rejection does not add recoil"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), BeforeGateRecoil);
			FWallShotProbeAccess::SetAttackGate(Fixture.Weapon, BeforeOverlap - 1, false);

			if (Fixture.Weapon->GetWeaponType() == EWeaponType::Rifle)
			{
				// 시야가 열린 별도 통제 조건에서 자동조준을 실제로 활성화한다. 벽 때문에 후보가
				// 사라진 결과를 "총열 방향 우선"으로 오인하지 않도록 회피 상태만 주입한다.
				Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Fixture.Tick(360);
				const FVector AssistedBodyPoint = View + FVector(300, 60, 0);
				// 이 대조군은 몸통 자동조준을 검증한다. 실제 코어 직접 조준은 생산 정책상 자동조준을 생략한다.
				TInlineComponentArray<UPrimitiveComponent*> TargetPrimitives(Target);
				for (UPrimitiveComponent* Primitive : TargetPrimitives)
					if (Primitive != TargetBody) Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				USceneComponent* BodyAnchor = Cast<USceneComponent>(Target->GetDefaultSubobjectByName(TEXT("AutoAimBodyAnchor")));
				if (!TestNotNull(TEXT("Production enemy autoaim anchor"), BodyAnchor)) return false;
				// 애니메이션/메시 갱신과 분리해 조준점을 월드 좌표로 고정한다.
				BodyAnchor->SetAbsolute(true, true, true);
				BodyAnchor->AddWorldOffset(AssistedBodyPoint - Target->GetAutoAimBodyLocation());
				TargetBody->SetWorldLocation(AssistedBodyPoint);
				Fixture.Tick(1);
				TestTrue(TEXT("Autoaim fixture remains off crosshair"), Target->GetAutoAimBodyLocation().Equals(AssistedBodyPoint, 0.1));
				// 후보 레지스트리는 유효한 방 태그가 있는 전투 대상을 보관한다.
				Target->GetRoomTagComp()->AssignDefaultRoomTag(FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1")));
				Fixture.World->GetSubsystem<UEnemyRoomSubsystem>()->RegisterEnemy(Target);
				TArray<AEnemyBase*> AutoAimCandidates;
				Fixture.World->GetSubsystem<UEnemyRoomSubsystem>()->GetAutoAimCandidates(AutoAimCandidates);
				TestTrue(TEXT("Body autoaim positive control is a registered candidate"), AutoAimCandidates.Contains(Target));
				UOutlierAbilitySystemComponent* ShooterASC = Fixture.Shooter->GetOutlierAbilitySystemComponent();
				ShooterASC->AddLooseGameplayTag(OutlierGameplayTags::State::WeaponOvercharged());
				ShooterASC->AddLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::WeaponOvercharge::AutoAim());
				TestTrue(TEXT("Production rifle overcharge autoaim is enabled"), Fixture.Shooter->IsAutoAimEnabled());
				const auto RefreshProjection = [&]()
				{
					struct FProjectionParameters { float FOV = 90.0f; float Aspect = 16.0f / 9.0f; } Projection;
					UFunction* Function = Fixture.Shooter->FindFunction(TEXT("ServerUpdateAutoAimView"));
					if (TestNotNull(TEXT("Production autoaim projection RPC"), Function))
						Fixture.Shooter->ProcessEvent(Function, &Projection);
				};
				RefreshProjection();
				State.Reset();
				State.WallOffsetAlpha = 1.0f;
				State.ViewModelWallOffsetLoc = FVector(0, -4, -0.5);
				State.ViewModelWallOffsetRot = FRotator(12, -8, 17);
				const FTransform Avoided = CompareQuery();
				FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
				const FWeaponShotSnapshot AvoidedShot = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
				TestTrue(TEXT("Enabled autoaim cannot redirect avoided barrel"), FVector(AvoidedShot.BaseDirection).Equals(Avoided.GetUnitAxis(EAxis::X), 1.e-6));
				State.Reset();
				Fixture.Tick(90);
				RefreshProjection();
				const FTransform Clear = CompareQuery();
				const FVector BodyDirection = (Target->GetAutoAimBodyLocation() - Clear.GetLocation()).GetSafeNormal();
				FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
				const FWeaponShotSnapshot Assisted = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
				TestTrue(TEXT("Clear rifle preserves body autoaim"), FVector(Assisted.BaseDirection).Equals(BodyDirection, 1.e-6));
				TestFalse(TEXT("Autoaim positive control redirects away from camera forward"), FVector(Assisted.BaseDirection).Equals(Clear.GetUnitAxis(EAxis::X), 0.01));
				ShooterASC->RemoveLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::WeaponOvercharge::AutoAim());
				ShooterASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::WeaponOvercharged());
			}

			int32 Presentations = 0;
			const FDelegateHandle Handle = WallShotProbe::Observed().AddLambda([&](const FWallShotObservation& Observation)
			{
				if (Observation.Weapon != Fixture.Weapon || !Observation.bPresentation) return;
				++Presentations;
				TestTrue(TEXT("Presentation consumes snapshotted world segment"), Observation.Start.Equals(OverlapShot.Start, 1.e-6) && Observation.End.Equals(OverlapShot.End, 1.e-6));
				TestTrue(TEXT("World-effect observation is from consumption path"), Observation.bWorldEffects);
			});
			// 실제 multicast가 Standalone에서는 이미 소비됐다. 다음 ID를 사용해 중복 전달을 직접 대조한다.
			FWeaponShotSnapshot Replay = OverlapShot;
			Replay.ShotId = FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId + 1;
			Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(nullptr);
			Fixture.Shooter->RefreshWeaponMode();
			FWallShotProbeAccess::Present(Fixture.Weapon, Replay);
			FWallShotProbeAccess::Present(Fixture.Weapon, Replay);
			WallShotProbe::Observed().Remove(Handle);
			TestEqual(TEXT("Duplicate snapshots consume once; Dedicated consumes no VFX"), Presentations, bServer ? 0 : 1);
			TestEqual(TEXT("Presentation replay never consumes more ammunition"), Fixture.Weapon->GetCurrentAmmo(), BeforeOverlap - 1);
		}
	}
	AddInfo(TEXT("Slice 3 checks production shot integration and snapshot consumption; network delivery and rendered W11 remain separate checks."));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierWallShotSlice4ContinuousTest, "Outlier.Weapon.WallShot.Slice4Continuous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWallShotSlice4ContinuousTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	for (const TCHAR* Name : {TEXT("Pistol"), TEXT("Rifle")})
	{
		for (bool bServer : {false, true})
		{
			FWallShotWorld Fixture;
			if (!Fixture.Initialize(*this, Name, bServer)) return false;
			// 프로브는 전방으로 진행한다. 큰 수직 면의 가장자리를 옮겨 좌우/상하만 노출한다.
			const UProceduralAnimValues* Values = Fixture.Weapon->GetFirstPersonProceduralValues();
			if (!TestNotNull(TEXT("Continuous fixture production probe settings"), Values)) return false;
			const FWeaponValues& Probe = Values->WeaponValues;
			const float Side = FMath::Max(Probe.WallSideProbeOffset, Probe.WallTraceRadius * 1.5f);
			const float Vertical = FMath::Max(Probe.WallVerticalProbeOffset, 0.0f);
			const FVector ProbeCenter(0, Probe.WallProbeRightOffset, Probe.WallProbeUpOffset);
			// 각 면의 경계를 실제 에셋의 해당 프로브 중심에 둔다. 카메라 중심 기준 고정
			// 20cm 경계는 오른쪽으로 이동한 왼쪽 프로브를 놓칠 수 있다.
			const FVector Edges[] = {ProbeCenter, ProbeCenter + FVector(0, -500 - Side, 0),
				ProbeCenter + FVector(0, 500 + Side, 0), ProbeCenter + FVector(0, 0, 500 + Vertical),
				ProbeCenter + FVector(0, 0, -500 - Vertical)};
			for (const FVector& Edge : Edges)
			{
				Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Fixture.Tick(360);
				FWallShotProbeAccess::ConfigureShotCosts(Fixture.Weapon);
				const FVector View = Fixture.Shooter->GetWallShotViewTransform().GetLocation();
				bool bSawAvoidance = false;
				int32 AcceptedShots = 0;
				const auto FireAndCompare = [&]()
				{
					FTransform Expected, Repeated;
					TestTrue(TEXT("W03 pre-fire common transform resolves"), Fixture.Shooter->TryGetWallShotTransform(Expected));
					TestTrue(TEXT("W03 repeated common transform resolves"), Fixture.Shooter->TryGetWallShotTransform(Repeated));
					TestTrue(TEXT("W03 pre-fire reads do not advance avoidance"), Expected.Equals(Repeated, 1.e-6));
					bSawAvoidance |= Fixture.Shooter->GetWeaponWallAvoidanceState().GetShotBlendAlpha() > 0.0f;
					const int32 Ammo = Fixture.Weapon->GetCurrentAmmo();
					const uint32 Recoil = FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon);
					const uint32 Id = FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId;
					FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
					const FWeaponShotSnapshot Shot = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
					++AcceptedShots;
					TestEqual(TEXT("W03 repeated production fire advances once"), Shot.ShotId, Id + 1);
					TestEqual(TEXT("W03 repeated production fire consumes one round"), Fixture.Weapon->GetCurrentAmmo(), Ammo - 1);
					TestEqual(TEXT("W03 repeated production fire applies one recoil"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), Recoil + 1);
					TestTrue(TEXT("W03 fire commits reuse cooldown"), Fixture.Weapon->IsOnReuseCooldown());
					TestTrue(TEXT("W03 shot follows immediately preceding common barrel"), FVector(Shot.BaseDirection).Equals(Expected.GetUnitAxis(EAxis::X), 1.e-6));
					TestTrue(TEXT("W03 zero bloom preserves base direction"), FVector(Shot.Direction).Equals(Shot.BaseDirection, 1.e-6));
					TestTrue(TEXT("W03 shot uses common start or explicit safe-view occlusion"), FVector(Shot.Start).Equals(Shot.bOccluded ? View : Expected.GetLocation(), 0.1));
					FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
					TestEqual(TEXT("W03 immediate retry preserves shot identity"), FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId, Shot.ShotId);
					TestEqual(TEXT("W03 immediate retry preserves ammo"), Fixture.Weapon->GetCurrentAmmo(), Ammo - 1);
					TestEqual(TEXT("W03 immediate retry preserves recoil"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), Recoil + 1);
				};
				// 발사 사이에도 매 프레임 벽을 움직여 진입/복귀 보간을 계속 진행한다.
				for (int32 Frame = 0; Frame < 300; ++Frame)
				{
					const float Distance = Frame < 120 ? FMath::Lerp(250.0f, 35.0f, Frame / 119.0f)
						: Frame < 180 ? 35.0f : FMath::Lerp(35.0f, 250.0f, (Frame - 180) / 119.0f);
					Fixture.Wall->SetWorldLocation(View + FVector(Distance, 0, 0) + Edge);
					Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
					Fixture.Tick(1);
					if (Frame % 15 == 0) FireAndCompare();
				}
				TestTrue(FString::Printf(TEXT("W03 %s dedicated=%d edge=%s approach exercises production avoidance"),
					Name, bServer, *Edge.ToString()), bSawAvoidance);
				Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Fixture.Tick(360);
				FTransform Released;
				TestTrue(TEXT("W03 released query resolves"), Fixture.Shooter->TryGetWallShotTransform(Released));
				TestEqual(TEXT("W03 release reaches zero blend"), Fixture.Shooter->GetWeaponWallAvoidanceState().GetShotBlendAlpha(), 0.0f);
				TestTrue(TEXT("W02/W03 released shot returns exact current viewpoint"), Released.Equals(Fixture.Shooter->GetWallShotViewTransform(), 1.e-6));
				FireAndCompare();
				TestEqual(TEXT("W03 approach hold exit and released shot all fired"), AcceptedShots, 21);
				AddInfo(FString::Printf(TEXT("Continuous %s dedicated=%d edge=%s shots=%d"), Name, bServer, *Edge.ToString(), AcceptedShots));
			}
			TArray<float> ClearTimes;
			const FFloatProperty* ReuseProperty = FindFProperty<FFloatProperty>(Fixture.Weapon->GetClass(), TEXT("ReuseCooldown"));
			if (!TestNotNull(TEXT("W02 production reuse cooldown property"), ReuseProperty)) return false;
			const float ProductionReuse = ReuseProperty->GetPropertyValue_InContainer(Fixture.Weapon->GetClass()->GetDefaultObject());
			for (bool bDistantWall : {false, true})
			{
				Fixture.Wall->SetCollisionEnabled(bDistantWall ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
				Fixture.Wall->SetWorldLocation(Fixture.Shooter->GetWallShotViewTransform().GetLocation() + FVector(1000, 0, 0));
				Fixture.Tick(360);
				FWallShotProbeAccess::ConfigureShotCosts(Fixture.Weapon);
				// 연사 중에는 실제 무기 에셋의 재사용 정책을 보존한다. 단발 비용 검증용
				// 0.2초 주입은 자동 발사의 기존 쿨다운 계약과 다를 수 있다.
				ReuseProperty->SetPropertyValue_InContainer(Fixture.Weapon, ProductionReuse);
				const int32 Ammo = Fixture.Weapon->GetCurrentAmmo();
				const uint32 Recoil = FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon);
				const float StartedAt = Fixture.World->GetTimeSeconds();
				TArray<float> Times;
				const FDelegateHandle Handle = WallShotProbe::Observed().AddLambda([&](const FWallShotObservation& Observation)
				{
					if (Observation.Weapon != Fixture.Weapon || Observation.bPresentation) return;
					Times.Add(Fixture.World->GetTimeSeconds() - StartedAt);
					TestEqual(TEXT("W02 held trigger has no avoidance for clear or distant wall"), Fixture.Shooter->GetWeaponWallAvoidanceState().GetShotBlendAlpha(), 0.0f);
					const FTransform View = Fixture.Shooter->GetWallShotViewTransform();
					TestTrue(TEXT("W02 held production shot preserves resolved view origin"), Observation.Start.Equals(View.GetLocation(), 0.1));
					TestTrue(TEXT("W02 held production shot preserves resolved view direction"), Observation.BaseDirection.Equals(View.GetUnitAxis(EAxis::X), 0.001));
				});
				Fixture.Weapon->StartAttack();
				Fixture.Tick(90);
				Fixture.Weapon->StopAttack();
				const int32 CountAtRelease = Times.Num();
				Fixture.Tick(90);
				WallShotProbe::Observed().Remove(Handle);
				TestEqual(TEXT("W02 StopAttack ends held-trigger fire"), Times.Num(), CountAtRelease);
				TestTrue(TEXT("W02 held production trigger emits at least one shot"), CountAtRelease > 0);
				if (Fixture.Weapon->GetWeaponType() == EWeaponType::Rifle)
					TestTrue(TEXT("W02 rifle held trigger actually repeats"), CountAtRelease > 1);
				TestEqual(TEXT("W02 held trigger consumes one round per single-muzzle shot"), Fixture.Weapon->GetCurrentAmmo(), Ammo - CountAtRelease);
				TestEqual(TEXT("W02 held trigger applies recoil once per single-muzzle shot"), FWallShotProbeAccess::GetRecoilSequence(Fixture.Weapon), Recoil + uint32(CountAtRelease));
				if (!bDistantWall) ClearTimes = Times;
				else
				{
					TestEqual(TEXT("W02 distant wall preserves clear held-trigger shot count"), Times.Num(), ClearTimes.Num());
					for (int32 Index = 0; Index < FMath::Min(Times.Num(), ClearTimes.Num()); ++Index)
						TestTrue(TEXT("W02 distant wall preserves clear held-trigger cadence"), FMath::IsNearlyEqual(Times[Index], ClearTimes[Index], WallShotTickInterval + 0.001f));
				}
				AddInfo(FString::Printf(TEXT("HeldTrigger %s dedicated=%d distant=%d shots=%d productionReuse=%.6f"), Name, bServer, bDistantWall, CountAtRelease, ProductionReuse));
			}
		}
	}
	AddInfo(TEXT("Controlled repeated and held production triggers checked; ADS, rendered pose agreement and real network transport require separate validation."));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierWallShotSlice4MultiMuzzleTest, "Outlier.Weapon.WallShot.Slice4MultiMuzzle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWallShotSlice4MultiMuzzleTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FWallShotWorld Fixture;
	if (!Fixture.Initialize(*this, TEXT("Rifle"), true)) return false;
	UClass* TurretClass = LoadClass<AAutoTurret>(nullptr,
		TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C"));
	if (!TestNotNull(TEXT("Production multi-muzzle turret class"), TurretClass)) return false;
	const FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("Room.Level01.1"));
	URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(Fixture.World);
	FRoomCombatRoomDefinition& Room = Definition->RoomDefinitions.AddDefaulted_GetRef();
	Room.RoomTag = RoomTag;
	FRoomCombatPhaseDefinition& Phase = Room.CombatPhases.AddDefaulted_GetRef();
	Phase.StartPolicy = ERoomCombatPhaseStartPolicy::ExternalTrigger;
	FRoomCombatWaveDefinition& Wave = Phase.Waves.AddDefaulted_GetRef();
	Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
	Wave.ExpectedWaveTurretCount = 1;
	URoomCombatSubsystem* Combat = Fixture.World->GetSubsystem<URoomCombatSubsystem>();
	if (!TestNotNull(TEXT("Turret fixture combat registry"), Combat)) return false;
	Combat->SetCombatDefinitionForTesting(Definition);
	const FTransform TurretTransform(FRotator::ZeroRotator, FVector(2000, 0, 1000));
	AAutoTurret* Turret = Fixture.World->SpawnActorDeferred<AAutoTurret>(TurretClass, TurretTransform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Production multi-muzzle turret"), Turret)) return false;
	Turret->ConfigureWaveRegistrationForTesting(RoomTag, 0, 0, TEXT("WallShot.Slice4.MultiMuzzleTurret"));
	Turret->FinishSpawning(TurretTransform);
	Turret->SetActorTickEnabled(false);
	Turret->GetCharacterMovement()->DisableMovement();
	if (AController* AI = Turret->GetController()) AI->UnPossess();
	// 플레이어 빙의는 기존 반동의 그룹당 1회 규칙을 관측하기 위한 통제 조건이다.
	APlayerController* TurretController = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Controlled turret owner controller"), TurretController)) return false;
	TurretController->Possess(Turret);
	TurretController->SetViewTarget(Turret);
	TurretController->UpdateCameraManager(WallShotTickInterval);
	ARangedWeaponBase* Weapon = Turret->GetCurrentWeapon();
	if (!TestNotNull(TEXT("Production turret equipped weapon"), Weapon)) return false;
	FWallShotProbeAccess::ConfigureControlledShot(Weapon);
	FWallShotProbeAccess::ConfigureShotCosts(Weapon);
	Turret->ResetWeaponMuzzleSequence();
	TArray<FName> ExpectedSockets;
	Turret->GetWeaponMuzzleSocketNames(false, ExpectedSockets);
	if (!TestTrue(TEXT("Production turret enables independent muzzle shots"), Turret->UsesIndependentMuzzleShots())
		|| !TestTrue(TEXT("Production turret active group has multiple muzzles"), ExpectedSockets.Num() > 1)) return false;
	for (const FName Socket : ExpectedSockets)
		TestTrue(TEXT("Production group socket exists on provider mesh"), Turret->GetWeaponMuzzleComponent(false)->DoesSocketExist(Socket));
	FVector View; FRotator Rotation;
	TurretController->GetPlayerViewPoint(View, Rotation);
	TArray<FName> ActualSockets;
	const uint32 BeforeId = FWallShotProbeAccess::GetLastShot(Weapon).ShotId;
	const uint32 BeforeRecoil = FWallShotProbeAccess::GetRecoilSequence(Weapon);
	const int32 BeforeAmmo = Weapon->GetCurrentAmmo();
	const FDelegateHandle Handle = WallShotProbe::Observed().AddLambda([&](const FWallShotObservation& Observation)
	{
		if (Observation.Weapon != Weapon || Observation.bPresentation) return;
		ActualSockets.Add(Observation.Socket);
		const FWeaponShotSnapshot Shot = FWallShotProbeAccess::GetLastShot(Weapon);
		TestFalse(TEXT("Non-Shooter muzzle remains outside Shooter wall-shot policy"), Shot.bShooterWallShot);
		TestEqual(TEXT("Non-Shooter shot has no Shooter equipment revision"), Shot.EquipmentRevision, uint32(0));
		TestTrue(TEXT("Non-Shooter shot preserves controller-view origin"), Observation.Start.Equals(View, 0.1));
		TestTrue(TEXT("Non-Shooter shot preserves controller-view direction"), Observation.BaseDirection.Equals(Rotation.Vector(), 0.001));
	});
	FWallShotProbeAccess::AttackOnce(Weapon);
	WallShotProbe::Observed().Remove(Handle);
	TestTrue(TEXT("Production attack retains every ordered provider socket"), ActualSockets == ExpectedSockets);
	TestEqual(TEXT("Multi-muzzle group consumes one round per muzzle"), Weapon->GetCurrentAmmo(), BeforeAmmo - ExpectedSockets.Num());
	TestEqual(TEXT("Multi-muzzle group advances identity per muzzle"), FWallShotProbeAccess::GetLastShot(Weapon).ShotId, BeforeId + uint32(ExpectedSockets.Num()));
	TestEqual(TEXT("Controlled player turret applies recoil once per group"), FWallShotProbeAccess::GetRecoilSequence(Weapon), BeforeRecoil + 1);
	AddInfo(TEXT("Real rifle turret provider/group/fire path checked. Partner production-fire regression: Outlier.Partner.RebootInputRecovery. Laser socket/cancellation regressions: Outlier.Weapon.Laser.SingleShotConfiguration and Outlier.Weapon.Laser.ShotAndCancellation."));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierWallShotSlice4MissingAssetTest, "Outlier.Weapon.WallShot.Slice4MissingAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWallShotSlice4MissingAssetTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	FWallShotWorld Fixture;
	if (!Fixture.Initialize(*this, TEXT("Pistol"), true)) return false;
	Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(nullptr);
	Fixture.Shooter->RefreshWeaponMode();
	// 컴포넌트 인스턴스만 변경한다. 로드된 SkeletalMesh/소켓/에셋은 수정하지 않는다.
	USkeletalMesh* OriginalMesh = Fixture.Weapon->GetFirstPersonWeaponMesh()->GetSkeletalMeshAsset();
	Fixture.Weapon->GetFirstPersonWeaponMesh()->SetSkeletalMeshAsset(nullptr);
	AddExpectedError(TEXT("Missing ranged weapon or skeletal asset"), EAutomationExpectedErrorFlags::Contains, 1, false);
	Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(Fixture.Weapon);
	Fixture.Shooter->RefreshWeaponMode();
	Fixture.Wall->SetWorldLocation(Fixture.Shooter->GetWallShotViewTransform().GetLocation() + FVector(70, 0, 0));
	Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Fixture.Tick(90);
	const FTransform View = Fixture.Shooter->GetWallShotViewTransform();
	FTransform MissingPose;
	TestFalse(TEXT("W07 missing instance mesh cannot produce a valid wall pose"), Fixture.Shooter->TryGetWallShotTransform(MissingPose));
	AddExpectedError(TEXT("has no valid shot pose; using guarded viewpoint fallback"), EAutomationExpectedErrorFlags::Contains, 1, false);
	FTransform Fallback;
	TestTrue(TEXT("W07 missing mesh resolves finite guarded fallback"), FWallShotProbeAccess::Resolve(Fixture.Weapon, Fallback));
	TestTrue(TEXT("W07 fallback uses exact resolved viewpoint"), Fallback.Equals(View, 1.e-6) && !Fallback.ContainsNaN());
	Fixture.Wall->SetWorldLocation(View.GetLocation() + FVector(35, 0, 0));
	Fixture.Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FWallShotProbeAccess::ConfigureShotCosts(Fixture.Weapon);
	const int32 BeforeAmmo = Fixture.Weapon->GetCurrentAmmo();
	const uint32 BeforeId = FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId;
	FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
	const FWeaponShotSnapshot Shot = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
	TestEqual(TEXT("W07 missing mesh still permits one normal-cost attack"), Fixture.Weapon->GetCurrentAmmo(), BeforeAmmo - 1);
	TestEqual(TEXT("W07 fallback advances one shot identity"), Shot.ShotId, BeforeId + 1);
	TestTrue(TEXT("W07 fallback remains finite and starts at view"), !FVector(Shot.Start).ContainsNaN() && FVector(Shot.Start).Equals(View.GetLocation(), 0.1));
	TestTrue(TEXT("W07 fallback cannot trace beyond blocking wall"), Shot.bHasImpact && Shot.End.X <= View.GetLocation().X + 37.1);
	Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(nullptr);
	Fixture.Shooter->RefreshWeaponMode();
	Fixture.Weapon->GetFirstPersonWeaponMesh()->SetSkeletalMeshAsset(OriginalMesh);
	for (const FName PropertyName : {FName(TEXT("FirstPersonProceduralValues")), FName(TEXT("PreSuitProceduralValues")), FName(TEXT("SuitProceduralValues"))})
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Fixture.Weapon->GetClass(), PropertyName);
		if (!TestNotNull(TEXT("W07 instance procedural setting property exists"), Property)) return false;
		Property->SetObjectPropertyValue_InContainer(Fixture.Weapon, nullptr);
	}
	Fixture.Weapon->RefreshProceduralPresentation();
	TestNull(TEXT("W07 all instance procedural settings are absent"), Fixture.Weapon->GetFirstPersonProceduralValues());
	AddExpectedError(TEXT("missing wall configuration"), EAutomationExpectedErrorFlags::Contains, 1, false);
	Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(Fixture.Weapon);
	Fixture.Shooter->RefreshWeaponMode();
	Fixture.Tick(90);
	FTransform WithoutSettings;
	TestTrue(TEXT("W07 missing settings preserve finite view fallback"), FWallShotProbeAccess::Resolve(Fixture.Weapon, WithoutSettings));
	TestTrue(TEXT("W07 missing settings use exact current viewpoint"), WithoutSettings.Equals(Fixture.Shooter->GetWallShotViewTransform(), 1.e-6) && !WithoutSettings.ContainsNaN());
	const int32 ConfigAmmo = Fixture.Weapon->GetCurrentAmmo();
	const uint32 ConfigId = FWallShotProbeAccess::GetLastShot(Fixture.Weapon).ShotId;
	FWallShotProbeAccess::AttackOnce(Fixture.Weapon);
	const FWeaponShotSnapshot ConfigShot = FWallShotProbeAccess::GetLastShot(Fixture.Weapon);
	TestEqual(TEXT("W07 missing settings consume normal ammunition"), Fixture.Weapon->GetCurrentAmmo(), ConfigAmmo - 1);
	TestEqual(TEXT("W07 missing settings emit exactly one shot"), ConfigShot.ShotId, ConfigId + 1);
	TestTrue(TEXT("W07 missing settings retain wall blocking"), ConfigShot.bHasImpact && ConfigShot.End.X <= View.GetLocation().X + 37.1);
	return !HasAnyErrors();
}
#endif
