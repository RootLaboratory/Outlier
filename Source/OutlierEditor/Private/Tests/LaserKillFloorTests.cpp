#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierEditor/Tests/LaserKillFloorTestPawn.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Environment/LaserKillFloor.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"

ALaserKillFloorTestPawn::ALaserKillFloorTestPawn()
{
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->SetSphereRadius(30.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
}

float ALaserKillFloorTestPawn::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	++DamageCount;
	LastDamageRequest = Request;
	return bRejectDamage ? 0.0f : Request.DamageAmount;
}

namespace
{
// TimerManager는 같은 프레임의 중복 Tick을 무시하고 첫 Tick에서 예약을 활성화한다.
void AdvanceContactTimers(UWorld* World)
{
	for (int32 Step = 0; Step < 3; ++Step)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(0.1f);
	}
}

struct FScopedLaserKillFloorTestWorld
{
	UWorld* World = nullptr;

	bool Initialize(FAutomationTestBase& Test, bool bBeginPlay = true)
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Laser floor test world is created"), World))
		{
			GEngine->DestroyWorldContext(World);
			return false;
		}

		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		FURL TestURL;
		TestURL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		if (!Test.TestTrue(TEXT("Test world has server authority"), World->SetGameMode(TestURL)))
		{
			return false;
		}
		World->InitializeActorsForPlay(TestURL);
		if (bBeginPlay) World->BeginPlay();
		return true;
	}

	~FScopedLaserKillFloorTestWorld()
	{
		if (!World)
		{
			return;
		}

		if (World->HasBegunPlay())
		{
			World->BeginTearingDown();
			World->EndPlay(EEndPlayReason::LevelTransition);
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierLaserKillFloorOverlapTest,
	"Outlier.Environment.LaserKillFloor.OverlapDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLaserKillFloorOverlapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedLaserKillFloorTestWorld TestWorld;
	if (!TestWorld.Initialize(*this))
	{
		return false;
	}

	UWorld* World = TestWorld.World;
	ALaserKillFloor* Floor = World->SpawnActor<ALaserKillFloor>();
	ALaserKillFloorTestPawn* Pawn = World->SpawnActor<ALaserKillFloorTestPawn>(
		ALaserKillFloorTestPawn::StaticClass(), FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Laser floor spawns"), Floor)
		|| !TestNotNull(TEXT("Damage receiver Pawn spawns"), Pawn))
	{
		return false;
	}
	if (!Floor->HasActorBegunPlay())
	{
		Floor->DispatchBeginPlay();
	}
	if (!Pawn->HasActorBegunPlay())
	{
		Pawn->DispatchBeginPlay();
	}

	UBoxComponent* KillVolume = Cast<UBoxComponent>(Floor->GetRootComponent());
	UStaticMeshComponent* LaserPlane = Cast<UStaticMeshComponent>(
		Floor->GetDefaultSubobjectByName(TEXT("LaserPlane")));
	if (!TestNotNull(TEXT("Kill volume exists"), KillVolume)
		|| !TestNotNull(TEXT("Visual plane exists"), LaserPlane))
	{
		return false;
	}

	TestTrue(TEXT("Floor is replicated"), Floor->GetIsReplicated());
	TestTrue(TEXT("Hazard starts enabled"), Floor->IsHazardEnabled());
	TestEqual(TEXT("Volume overlaps Pawns"), KillVolume->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	TestEqual(TEXT("Volume does not block WorldStatic"), KillVolume->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Ignore);
	TestEqual(TEXT("Visual plane has no collision"), LaserPlane->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Visual plane produces no overlaps"), LaserPlane->GetGenerateOverlapEvents());
	TestEqual(TEXT("Pawn starts outside the floor"), Pawn->DamageCount, 0);
	auto MovePawn = [Pawn, KillVolume](const FVector& Location)
	{
		Pawn->SetActorLocation(Location);
		CastChecked<USphereComponent>(Pawn->GetRootComponent())->UpdateOverlaps();
		KillVolume->UpdateOverlaps();
	};

	MovePawn(FVector::ZeroVector);
	TestTrue(TEXT("Moved Pawn overlaps the active kill volume"), KillVolume->IsOverlappingActor(Pawn));
	TestEqual(TEXT("Entering active floor applies damage once"), Pawn->DamageCount, 1);
	if (Pawn->DamageCount > 0)
	{
		TestEqual(TEXT("Damage uses the environment tag"), Pawn->LastDamageRequest.DamageTag,
			OutlierDamage::InstantKillTag());
		TestTrue(TEXT("Instant kill remains environment damage"), Pawn->LastDamageRequest.DamageTag.MatchesTag(
			OutlierGameplayTags::Damage::Environment()));
		TestEqual(TEXT("Damage uses the floor as causer"), Pawn->LastDamageRequest.DamageCauser,
			static_cast<AActor*>(Floor));
		TestTrue(TEXT("Damage is immediately lethal in ordinary vitality ranges"),
			Pawn->LastDamageRequest.DamageAmount >= 1000000.0f);
	}

	Floor->SetHazardEnabled(false);
	TestFalse(TEXT("Floor disables"), Floor->IsHazardEnabled());
	TestEqual(TEXT("Disabled volume has no collision"), KillVolume->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	MovePawn(FVector(1000.0f, 0.0f, 0.0f));
	MovePawn(FVector::ZeroVector);
	TestEqual(TEXT("Disabled floor does not damage on entry"), Pawn->DamageCount, 1);

	Floor->SetHazardEnabled(true);
	TestTrue(TEXT("Floor re-enables"), Floor->IsHazardEnabled());
	TestEqual(TEXT("Enabled volume restores overlap queries"), KillVolume->GetCollisionEnabled(),
		ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Re-enabling damages a Pawn already inside"), Pawn->DamageCount, 2);
	Floor->SetHazardEnabled(true);
	TestEqual(TEXT("Setting the same state does not damage again"), Pawn->DamageCount, 2);
	MovePawn(FVector(1000.0f, 0.0f, 0.0f));
	MovePawn(FVector::ZeroVector);
	TestTrue(TEXT("Re-entered Pawn overlaps the active kill volume"), KillVolume->IsOverlappingActor(Pawn));
	TestEqual(TEXT("Leaving and entering active floor damages again"), Pawn->DamageCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierLaserContactRetryTest,
	"Outlier.Environment.LaserKillFloor.ContactRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLaserContactRetryTest::RunTest(const FString& Parameters)
{
	FScopedLaserKillFloorTestWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UWorld* World = Fixture.World;
	ALaserKillFloorTestPawn* Pawn = World->SpawnActor<ALaserKillFloorTestPawn>();
	Pawn->bRejectDamage = true;
	// 먼저 안에 있는 Pawn도 Floor BeginPlay에서 검사한다.
	ALaserKillFloor* Floor = World->SpawnActor<ALaserKillFloor>();
	if (!Floor->HasActorBegunPlay()) Floor->DispatchBeginPlay();
	UBoxComponent* Volume = CastChecked<UBoxComponent>(Floor->GetRootComponent());
	Volume->UpdateOverlaps();
	TestTrue(TEXT("Initial occupant receives a request"), Pawn->DamageCount > 0);
	const int32 Rejected = Pawn->DamageCount;
	AdvanceContactTimers(World);
	TestTrue(TEXT("Rejected contact is retried without reentry"), Pawn->DamageCount > Rejected);
	Pawn->bRejectDamage = false;
	AdvanceContactTimers(World);
	const int32 Accepted = Pawn->DamageCount;
	AdvanceContactTimers(World);
	TestEqual(TEXT("Successful continuous contact is not retried"), Pawn->DamageCount, Accepted);
	Floor->SetHazardEnabled(false);
	Pawn->bRejectDamage = true;
	Floor->SetHazardEnabled(true);
	const int32 BeforeDisable = Pawn->DamageCount;
	Floor->SetHazardEnabled(false);
	AdvanceContactTimers(World);
	TestEqual(TEXT("Disable clears rejected contact timer"), Pawn->DamageCount, BeforeDisable);
	Floor->SetHazardEnabled(true);
	Pawn->SetActorLocation(FVector(1000, 0, 0));
	Volume->UpdateOverlaps();
	const int32 BeforeExit = Pawn->DamageCount;
	AdvanceContactTimers(World);
	TestEqual(TEXT("Leaving clears rejected contact"), Pawn->DamageCount, BeforeExit);
	Floor->SetRole(ROLE_SimulatedProxy);
	Pawn->SetActorLocation(FVector::ZeroVector);
	Volume->UpdateOverlaps();
	TestEqual(TEXT("Client contact cannot request damage"), Pawn->DamageCount, BeforeExit);
	Floor->SetRole(ROLE_Authority);
	Floor->SetHazardEnabled(false);
	Floor->SetHazardEnabled(true);
	const int32 BeforeDestroy = Pawn->DamageCount;
	Floor->Destroy();
	AdvanceContactTimers(World);
	TestEqual(TEXT("EndPlay clears retry timer"), Pawn->DamageCount, BeforeDestroy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierLaserPlayerLifecycleTest,
	"Outlier.Environment.LaserKillFloor.PlayerLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLaserPlayerLifecycleTest::RunTest(const FString& Parameters)
{
	// 실제 BP는 읽기/스폰만 한다. 에셋의 기본값이나 레벨을 저장하지 않는다.
	for (const TCHAR* FloorPath : {
		TEXT("/Game/Blueprints/Object/BP_LaserFloor.BP_LaserFloor_C"),
		TEXT("/Game/Blueprints/Tutorial/BP_TutoLaserKillFloor.BP_TutoLaserKillFloor_C") })
	{
		for (const bool bSuit : {false, true})
		{
			FScopedLaserKillFloorTestWorld Fixture;
			if (!Fixture.Initialize(*this, false)) return false;
			UWorld* World = Fixture.World;
			World->WorldType = EWorldType::PIE;
			UClass* FloorClass = LoadClass<ALaserKillFloor>(nullptr, FloorPath);
			UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
				TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
			if (!TestTrue(TEXT("Production laser and Shooter classes load"), FloorClass && ShooterClass)) return false;
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass,
				FTransform(FVector(2000, 0, 0)), Spawn);
			APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(),
				FTransform(FVector(-2000, 0, 0)), Spawn);
			AOutlierPlayerState* ShooterState = World->SpawnActor<AOutlierPlayerState>();
			AOutlierPlayerState* PartnerState = World->SpawnActor<AOutlierPlayerState>();
			if (!TestTrue(TEXT("Real pair spawns"), Shooter && Partner && ShooterState && PartnerState)) return false;
			ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
			PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
			ShooterState->SetPairId(31);
			PartnerState->SetPairId(31);
			Shooter->SetPlayerState(ShooterState);
			Partner->SetPlayerState(PartnerState);
			UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
			UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
			ShooterASC->InitializeForPawn(Shooter);
			PartnerASC->InitializeForPawn(Partner);
			ShooterState->SetShooterCharacter(Shooter);
			ShooterState->SetPartnerCharacter(Partner);
			PartnerState->SetShooterCharacter(Shooter);
			PartnerState->SetPartnerCharacter(Partner);
			ShooterState->SetAcquiredSuit(bSuit);
			PartnerState->SetAcquiredSuit(bSuit);
			Shooter->DispatchBeginPlay();
			ShooterASC->InitializeVitalityToSelf(10000000.0f);
			ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 10000000.0f);
			ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 10000000.0f);
			ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 10000000.0f);
			ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 10000000.0f);

			UDataTable* Vitality = NewObject<UDataTable>();
			Vitality->RowStruct = FOutlierVitalityDataRow::StaticStruct();
			FOutlierVitalityDataRow VitalityRow;
			VitalityRow.MaxHealth = 10000000.0f;
			Vitality->AddRow(TEXT("Test"), VitalityRow);
			UDataTable* Survival = NewObject<UDataTable>();
			Survival->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
			FPartnerSurvivalDataRow SurvivalRow;
			SurvivalRow.RebootTime = 30.0f;
			Survival->AddRow(TEXT("Test"), SurvivalRow);
			FDataTableRowHandle VitalityHandle;
			VitalityHandle.DataTable = Vitality;
			VitalityHandle.RowName = TEXT("Test");
			FDataTableRowHandle SurvivalHandle;
			SurvivalHandle.DataTable = Survival;
			SurvivalHandle.RowName = TEXT("Test");
			UPartnerVitalityComponent* PartnerVitality = Partner->GetPartnerVitalityComponent();
			if (!TestTrue(TEXT("Real Partner lifecycle initializes"), PartnerVitality->InitializeFromDataTables(
				VitalityHandle, SurvivalHandle))) return false;

			ALaserKillFloor* Floor = World->SpawnActor<ALaserKillFloor>(FloorClass);
			if (!TestNotNull(TEXT("Production laser BP spawns"), Floor)) return false;
			Floor->DispatchBeginPlay();
			// World가 플레이 상태여야 실제 Overlap delegate가 전달된다.
			// Partner는 맵/이동 데이터 초기화 대신 위의 실제 Vitality 컴포넌트만 준비한 상태를 유지한다.
			World->SetBegunPlay(true);
			UBoxComponent* Volume = CastChecked<UBoxComponent>(Floor->GetRootComponent());
			const FFloatProperty* DamageProperty = FindFProperty<FFloatProperty>(Floor->GetClass(), TEXT("InstantKillDamage"));
			AddInfo(FString::Printf(TEXT("Laser enabled=%d BegunPlay=%d Authority=%d Damage=%f Bound=%d"),
				Floor->IsHazardEnabled(), Floor->HasActorBegunPlay(), Floor->HasAuthority(),
				DamageProperty ? DamageProperty->GetPropertyValue_InContainer(Floor) : -1.0f,
				Volume->OnComponentBeginOverlap.IsBound()));
			AddInfo(FString::Printf(TEXT("%s: Volume=%s PawnResponse=%d Events=%d Extent=%s ShooterEvents=%d"),
				FloorPath, *Volume->GetName(), Volume->GetCollisionResponseToChannel(ECC_Pawn),
				Volume->GetGenerateOverlapEvents(), *Volume->GetScaledBoxExtent().ToString(),
				Shooter->GetCapsuleComponent()->GetGenerateOverlapEvents()));
			TestEqual(TEXT("BP volume overlaps Pawn"), Volume->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
			TestTrue(TEXT("BP volume generates contacts"), Volume->GetGenerateOverlapEvents());
			ShooterState->SetEditorPairInvulnerable(true);
			Partner->SetActorLocation(Floor->GetActorLocation());
			Partner->GetCapsuleComponent()->UpdateOverlaps();
			Volume->UpdateOverlaps();
			TestTrue(TEXT("Real Partner capsule enters volume"), Volume->IsOverlappingActor(Partner));
			TestFalse(TEXT("Tester blocks Partner reboot"), PartnerVitality->IsRebooting());
			TestFalse(TEXT("Tester blocks pre-suit GameOver"), Shooter->IsDead());
			const FActiveGameplayEffectHandle Immunity = PartnerASC->ApplyDamageImmuneStateToSelf();
			ShooterState->SetEditorPairInvulnerable(false);
			AdvanceContactTimers(World);
			TestEqual(TEXT("Other immunity survives Tester OFF"), Partner->GetVitalAttributeSet()->GetHealth(), 10000000.0f);
			PartnerASC->RemoveActiveEffectFromSelf(Immunity);
			AdvanceContactTimers(World);
			TestEqual(TEXT("Immunity OFF inside kills Partner vitality"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);
			TestFalse(TEXT("Partner instant kill bypasses Reboot regardless of suit"), PartnerVitality->IsRebooting());
			TestTrue(TEXT("Partner instant kill enters Shooter death path regardless of suit"), Shooter->IsDead());
			if (bSuit)
			{
				// Partner 접촉의 페어 사망을 지운 뒤 Shooter 자신의 레이저 피해를 별도로 검증한다.
				FGameplayTagContainer DeadTags;
				DeadTags.AddTag(OutlierGameplayTags::State::Dead());
				ShooterASC->RemoveActiveEffectsWithGrantedTags(DeadTags);
				TestFalse(TEXT("Shooter laser fixture clears preceding pair death"), Shooter->IsDead());
				AdvanceContactTimers(World);
				TestFalse(TEXT("Same successful contact cannot repeat pair death"), Shooter->IsDead());
				TestFalse(TEXT("Same successful contact cannot start Reboot"), PartnerVitality->IsRebooting());
				ShooterState->SetEditorPairInvulnerable(true);
				Shooter->SetActorLocation(Floor->GetActorLocation());
				Shooter->GetCapsuleComponent()->UpdateOverlaps();
				Volume->UpdateOverlaps();
				TestTrue(TEXT("Actual Shooter capsule enters BP laser"), Volume->IsOverlappingActor(Shooter));
				TestFalse(TEXT("Tester blocks Shooter instant kill"), Shooter->IsDead());
				ShooterState->SetEditorPairInvulnerable(false);
				AdvanceContactTimers(World);
				TestTrue(TEXT("High-health/shield Shooter dies when Tester turns OFF inside"), Shooter->IsDead());
				TestEqual(TEXT("All personal shield is consumed"), Shooter->GetCurShield(), 0.0f);
				TestEqual(TEXT("All partner shield is consumed"), Shooter->GetCurPartnerShield(), 0.0f);
			}
			PartnerVitality->BeginOwnerTeardown();
		}
	}
	return true;
}

#endif
