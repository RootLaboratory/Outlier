#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
struct FEnemyHitClueTestWorld
{
	UWorld* World = nullptr;

	FEnemyHitClueTestWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyHitClueTest")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->TimeSeconds = 10.0f;
	}

	~FEnemyHitClueTestWorld()
	{
		if (!World) return;
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	AEnemyBase* SpawnEnemy() const
	{
		if (!World) return nullptr;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform::Identity, Spawn);
		if (Enemy)
		{
			Enemy->GetOutlierAbilitySystemComponent()->InitializeForPawn(Enemy);
			Enemy->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
		}
		return Enemy;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitClueValidationTest,
	"Outlier.Enemy.HitClue.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitClueValidationTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	AEnemyBase* Other = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Other receiver"), Other)) return false;
	FEnemyHitClue Clue, Read;
	TestTrue(TEXT("World origin is a valid snapshot"), Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 2.0, Clue));
	TestFalse(TEXT("Creation alone does not publish a clue"), Enemy->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Another receiver cannot accept the event"), Other->RecordHitClue(Clue));
	TestTrue(TEXT("Receiver accepts its event"), Enemy->RecordHitClue(Clue));
	TestTrue(TEXT("Pending clue can be read"), Enemy->TryGetPendingHitClue(Read));
	TestEqual(TEXT("Lifetime is fixed at occurrence"), Read.ExpiresAt, 12.0);
	TestTrue(TEXT("Origin remains unchanged"), Read.Location.IsZero());

	const double InvalidLifetimes[] = { 0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() };
	for (double Lifetime : InvalidLifetimes)
	{
		TestFalse(TEXT("Invalid lifetime is rejected"), Enemy->CreateHitClue(FVector::ZeroVector, 10.0, Lifetime, Read));
		TestEqual(TEXT("Failed creation clears output"), Read.Serial, uint64(0));
	}
	TestFalse(TEXT("Future occurrence is rejected"), Enemy->CreateHitClue(FVector::ZeroVector, 11.0, 2.0, Read));
	TestFalse(TEXT("Negative occurrence is rejected"), Enemy->CreateHitClue(FVector::ZeroVector, -1.0, 20.0, Read));
	TestFalse(TEXT("Expired occurrence is rejected"), Enemy->CreateHitClue(FVector::ZeroVector, 8.0, 2.0, Read));
	FEnemyHitClue Invalid = Clue;
	Invalid.Location.X = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Invalid snapshot cannot be recorded"), Enemy->RecordHitClue(Invalid));
	TestFalse(TEXT("NaN location cannot be issued"), Enemy->CreateHitClue(Invalid.Location, 10.0, 2.0, Read));
	TestFalse(TEXT("NaN occurrence cannot be issued"), Enemy->CreateHitClue(FVector::ZeroVector,
		std::numeric_limits<double>::quiet_NaN(), 2.0, Read));

	Fixture.World->TimeSeconds = 12.0f;
	TestFalse(TEXT("Waiting clue expires at its original deadline"), Enemy->TryGetPendingHitClue(Read));
	TestEqual(TEXT("Failed lookup clears output"), Read.Serial, uint64(0));
	TestFalse(TEXT("Expired event cannot be revived"), Enemy->RecordHitClue(Clue));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitClueOrderingTest,
	"Outlier.Enemy.HitClue.OrderingAndConsumption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitClueOrderingTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Enemy"), Enemy)) return false;
	FEnemyHitClue First, Second, Older, Read;
	Enemy->CreateHitClue(FVector(100.0, 0.0, 0.0), 10.0, 5.0, First);
	Enemy->CreateHitClue(FVector(-100.0, 0.0, 0.0), 10.0, 5.0, Second);
	TestTrue(TEXT("Latest same-frame event is accepted"), Enemy->RecordHitClue(Second));
	TestFalse(TEXT("Late delivery cannot replace newer event"), Enemy->RecordHitClue(First));
	TestFalse(TEXT("Duplicate event cannot renew lifetime"), Enemy->RecordHitClue(Second));
	Enemy->CreateHitClue(FVector::ZeroVector, 9.0, 5.0, Older);
	TestFalse(TEXT("Higher serial does not make an older occurrence fresh"), Enemy->RecordHitClue(Older));
	TestTrue(TEXT("Latest pending clue remains available"), Enemy->TryGetPendingHitClue(Read));
	TestEqual(TEXT("Rejected events preserve selected event"), Read.Serial, Second.Serial);
	TestFalse(TEXT("Stale consume cannot remove latest event"), Enemy->ConsumeHitClue(First.Serial));
	TestTrue(TEXT("Ignored event is explicitly consumed"), Enemy->ConsumeHitClue(Second.Serial));
	TestFalse(TEXT("Consumption is once only"), Enemy->ConsumeHitClue(Second.Serial));
	TestFalse(TEXT("Consumed event cannot be republished"), Enemy->RecordHitClue(Second));
	TestFalse(TEXT("No pending clue after consumption"), Enemy->TryGetPendingHitClue(Read));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitClueInvestigationTest,
	"Outlier.Enemy.HitClue.InvestigationLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitClueInvestigationTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Enemy"), Enemy)) return false;
	FEnemyHitClue First, Second, Active, Read;
	Enemy->CreateHitClue(FVector(100.0, 0.0, 0.0), 10.0, 1.0, First);
	Enemy->RecordHitClue(First);
	TestTrue(TEXT("Valid clue starts an investigation"), Enemy->BeginHitInvestigation(First.Serial, Active));
	TestFalse(TEXT("Started clue leaves the waiting slot"), Enemy->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Same event cannot start twice"), Enemy->BeginHitInvestigation(First.Serial, Read));
	Enemy->CreateHitClue(FVector(-100.0, 0.0, 0.0), 10.0, 2.0, Second);
	TestTrue(TEXT("Another same-frame hit can wait"), Enemy->RecordHitClue(Second));
	TestTrue(TEXT("New hit does not end active investigation"), Enemy->IsHitInvestigationCurrent(First.Serial));
	TestTrue(TEXT("Active snapshot remains fixed"), Active.Location.Equals(First.Location));
	TestTrue(TEXT("Caller explicitly switches to pending hit"), Enemy->BeginHitInvestigation(Second.Serial, Active));
	TestFalse(TEXT("Old completion cannot end replacement"), Enemy->EndHitInvestigation(First.Serial));
	FEnemyHitClue Ignored;
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 2.0, Ignored);
	Enemy->RecordHitClue(Ignored);
	TestTrue(TEXT("A later event can be ignored independently"), Enemy->ConsumeHitClue(Ignored.Serial));
	TestTrue(TEXT("Ignoring pending event preserves active investigation"), Enemy->IsHitInvestigationCurrent(Second.Serial));

	Fixture.World->TimeSeconds = 13.0f;
	TestTrue(TEXT("Started investigation survives waiting TTL"), Enemy->IsHitInvestigationCurrent(Second.Serial));
	TestTrue(TEXT("Investigation ends through its own completion"), Enemy->EndHitInvestigation(Second.Serial));
	TestFalse(TEXT("Completion is once only"), Enemy->EndHitInvestigation(Second.Serial));
	TestFalse(TEXT("Completed event cannot restart"), Enemy->BeginHitInvestigation(Second.Serial, Read));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitClueLifecycleTest,
	"Outlier.Enemy.HitClue.LifecycleAndAuthority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitClueLifecycleTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Enemy"), Enemy)) return false;
	FEnemyHitClue Old, Current, Read;
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Old);
	Enemy->ClearHitClues();
	TestFalse(TEXT("Clear invalidates even unpublished callbacks"), Enemy->RecordHitClue(Old));
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	TestTrue(TEXT("Event numbers do not reset across clear"), Current.Serial > Old.Serial);
	Enemy->RecordHitClue(Current);

	// 이 테스트는 스턴 행동 대신 단서 저장 계층만 검사한다. 스턴 중 보관하되 시작은 허용하지 않는다.
	TestTrue(TEXT("Accepted hit updates minimum alert hold"), Enemy->IsAlertHoldActive());
	Enemy->CombatState = EEnemyCombatState::Stun;
	TestTrue(TEXT("Stun preserves pending clue"), Enemy->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Stun defers investigation start"), Enemy->BeginHitInvestigation(Current.Serial, Read));
	Enemy->CombatState = EEnemyCombatState::NonCombat;
	TestTrue(TEXT("Recovery can start unexpired event"), Enemy->BeginHitInvestigation(Current.Serial, Read));
	TestEqual(TEXT("Recovery does not extend deadline"), Read.ExpiresAt, Current.ExpiresAt);
	Enemy->InvalidatePoolLease();
	TestFalse(TEXT("Lease invalidation also discards alert clock"), Enemy->HasAlertClue());
	TestFalse(TEXT("Lease invalidation cancels active callback"), Enemy->IsHitInvestigationCurrent(Current.Serial));
	TestFalse(TEXT("Old lease cannot republish event"), Enemy->RecordHitClue(Current));
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	Enemy->RecordHitClue(Current);
	Enemy->SetEnemyPossessed(true);
	TestFalse(TEXT("Possession discards alert clock"), Enemy->HasAlertClue());
	TestFalse(TEXT("Possession discards AI clue"), Enemy->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Possessed pawn cannot issue AI clue"), Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Read));
	Enemy->SetEnemyPossessed(false);
	TestFalse(TEXT("Release cannot restore prior possession event"), Enemy->RecordHitClue(Current));

	// 실제 반환 진입점을 컨트롤러 없이 호출해 OnUnPossess 정리에 의존하지 않음을 확인한다.
	Enemy->PoolState = EEnemyPoolState::CombatActive;
	Enemy->PoolGameplayGeneration = 1;
	Enemy->PoolLeaseSerial = 1;
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	Enemy->RecordHitClue(Current);
	Enemy->BeginHitInvestigation(Current.Serial, Read);
	Enemy->FinishPoolReturn(1, 1);
	TestFalse(TEXT("Pool return discards alert clock"), Enemy->HasAlertClue());
	TestFalse(TEXT("Pool return cancels active callback without controller"), Enemy->IsHitInvestigationCurrent(Current.Serial));
	Enemy->ResetPoolRuntimeState();
	Enemy->PoolState = EEnemyPoolState::CombatActive;
	TestFalse(TEXT("Reuse cannot restore previous lease event"), Enemy->RecordHitClue(Current));
	Enemy->PoolState = EEnemyPoolState::Unmanaged;

	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Turret"), Turret)) return false;
	Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
	Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
	Turret->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	Turret->RecordHitClue(Current);
	Turret->ApplyHackedTeamState();
	TestFalse(TEXT("Turret team refresh discards alert clock"), Turret->HasAlertClue());
	TestFalse(TEXT("Turret team refresh clears clues without controller"), Turret->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Old turret team event cannot return"), Turret->RecordHitClue(Current));

	AEnemyAIController* AI = Fixture.World->SpawnActor<AEnemyAIController>();
	if (!TestNotNull(TEXT("AI controller"), AI)) return false;
	AI->Possess(Enemy);
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	Enemy->RecordHitClue(Current);
	AI->RefreshTeamAndPerceptionFromPawn();
	TestFalse(TEXT("Controller team refresh discards alert clock"), Enemy->HasAlertClue());
	TestFalse(TEXT("Team refresh discards prior clue"), Enemy->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Prior-team callback cannot restore clue"), Enemy->RecordHitClue(Current));
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	Enemy->RecordHitClue(Current);
	AI->UnPossess();
	TestFalse(TEXT("Controller detach discards alert clock"), Enemy->HasAlertClue());
	TestFalse(TEXT("Controller detach discards prior clue"), Enemy->TryGetPendingHitClue(Read));

	Enemy->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot issue hit events"), Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Read));
	TestFalse(TEXT("Client cannot record hit events"), Enemy->RecordHitClue(Current));
	Enemy->SetRole(ROLE_Authority);
	Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Current);
	Enemy->RecordHitClue(Current);
	Enemy->BeginDeathForPoolTesting();
	TestFalse(TEXT("Death cleanup discards alert clock"), Enemy->HasAlertClue());
	TestFalse(TEXT("Death cleanup discards pending clue"), Enemy->TryGetPendingHitClue(Read));
	TestFalse(TEXT("Death callback cannot restore clue"), Enemy->RecordHitClue(Current));
	TestFalse(TEXT("Death cleanup prevents issuing fresh events"), Enemy->CreateHitClue(FVector::ZeroVector, 10.0, 5.0, Read));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitControllerHandoffTest,
	"Outlier.Enemy.HitClue.ControllerHandoff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitControllerHandoffTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	AEnemyAIController* FirstAI = Fixture.World ? Fixture.World->SpawnActor<AEnemyAIController>() : nullptr;
	AEnemyAIController* NextAI = Fixture.World ? Fixture.World->SpawnActor<AEnemyAIController>() : nullptr;
	APlayerController* Player = Fixture.World ? Fixture.World->SpawnActor<APlayerController>() : nullptr;
	if (!TestNotNull(TEXT("Enemy for controller handoff"), Enemy)
		|| !TestNotNull(TEXT("Original AI controller"), FirstAI)
		|| !TestNotNull(TEXT("Replacement AI controller"), NextAI)
		|| !TestNotNull(TEXT("Player controller"), Player)) return false;
	FirstAI->Possess(Enemy);

	FEnemyHitClue Active, Pending, Unpublished, Read;
	const auto StartResponse = [this, Enemy, &Fixture, &Active, &Pending, &Unpublished]()
	{
		const double Now = Fixture.World->GetTimeSeconds();
		if (!Enemy->CreateHitClue(FVector(-500, 0, 0), Now, 5.0, Active) || !Enemy->RecordHitClue(Active)) return false;
		Enemy->TickHitResponse(0.0f);
		if (!TestTrue(TEXT("Real controller starts a direction response before handoff"), Enemy->IsHitDirectionConfirmationActive())) return false;
		return Enemy->CreateHitClue(FVector(0, 500, 0), Now, 5.0, Pending) && Enemy->RecordHitClue(Pending)
			&& Enemy->CreateHitClue(FVector(500, 0, 0), Now, 5.0, Unpublished);
	};
	const auto CheckCleared = [this, Enemy, &Active, &Pending, &Unpublished, &Read]()
	{
		return TestFalse(TEXT("Handoff releases direction ownership"), Enemy->HasHitResponse())
			&& TestFalse(TEXT("Handoff discards the waiting response"), Enemy->TryGetPendingHitClue(Read))
			&& TestFalse(TEXT("Handoff discards the previous alert deadline"), Enemy->HasAlertClue())
			&& TestFalse(TEXT("Old completion cannot finish a later action"), Enemy->EndHitInvestigation(Active.Serial))
			&& TestFalse(TEXT("Old pending callback cannot publish again"), Enemy->RecordHitClue(Pending))
			&& TestFalse(TEXT("Unpublished old callback cannot cross the handoff"), Enemy->RecordHitClue(Unpublished));
	};
	if (!StartResponse()) return false;
	// 실제 Possess를 호출해 이전 Controller 해제와 새 Controller 획득의 정리 경로를 함께 검사한다.
	NextAI->Possess(Enemy);
	if (!TestEqual(TEXT("Replacement AI owns the pawn"), Enemy->GetController(), static_cast<AController*>(NextAI))
		|| !CheckCleared() || !StartResponse()) return false;
	Player->Possess(Enemy);
	if (!TestTrue(TEXT("Player possession suppresses AI perception"), Enemy->IsEnemyPossessed()) || !CheckCleared()) return false;
	NextAI->Possess(Enemy);
	if (!TestFalse(TEXT("AI repossession restores AI control"), Enemy->IsEnemyPossessed())
		|| !CheckCleared() || !StartResponse()) return false;
	// 사망 중단은 BeginPlay와 실제 피해 처리를 사용하는 StateTree 통합 테스트에서 별도로 검사한다.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitDamageOriginTest,
	"Outlier.Enemy.HitResponse.DamageOrigins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitDamageOriginTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	for (int32 Kind = 0; Kind < 4; ++Kind)
	{
		AEnemyBase* Enemy = Fixture.SpawnEnemy();
		if (!TestNotNull(TEXT("Living damage receiver"), Enemy)) return false;
		// 기본 Enemy도 AI를 자동 생성한다. 이 검증은 행동 채택 전 피해→단서 전달만 분리해서 확인한다.
		if (AController* Controller = Enemy->GetController()) Controller->UnPossess();
		FOutlierDamageRequest Request;
		Request.DamageAmount = 1.0f;
		Request.DamageTag = Kind == 0 ? OutlierGameplayTags::Damage::Melee()
			: Kind == 2 ? OutlierGameplayTags::Damage::Explosion() : OutlierGameplayTags::Damage::Ranged();
		Request.bReflectedDamage = Kind == 3;
		// 피해 출처 여부는 내성 분류와 독립적이다. Ignore인 폭발도 유효한 출처를 전달할 수 있다.
		Request.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
		Request.bHasDamageOrigin = true;
		Request.DamageOrigin = Kind == 0 ? FVector::ZeroVector : FVector(-500.0, Kind * 100.0, 50.0);
		const float AppliedDamage = Enemy->ReceiveOutlierDamage(Request);
		AddInfo(FString::Printf(TEXT("[HitOrigin] Kind=%d Applied=%.3f Health=%.3f HasResponse=%d Controller=%s"),
			Kind, AppliedDamage, Enemy->GetCurrentHealth(), Enemy->HasHitResponse(), *GetNameSafe(Enemy->GetController())));
		TestTrue(TEXT("Source-bearing damage applies"), AppliedDamage > 0.0f);
		FEnemyHitClue Clue;
		if (!TestTrue(FString::Printf(TEXT("Supported attack %d records a clue"), Kind), Enemy->TryGetPendingHitClue(Clue))) return false;
		TestTrue(TEXT("Clue keeps the supplied origin including zero"), Clue.Location.Equals(Request.DamageOrigin));
		TestEqual(TEXT("Clue uses damage occurrence time"), Clue.OccurredAt, double(Fixture.World->GetTimeSeconds()));
		Request.DamageOrigin = FVector(9000.0);
		TestTrue(TEXT("Later request changes do not mutate the snapshot"), Enemy->TryGetPendingHitClue(Clue));
		TestFalse(TEXT("Snapshot does not follow later source changes"), Clue.Location.Equals(Request.DamageOrigin));
	}
	for (int32 Kind = 0; Kind < 4; ++Kind)
	{
		AEnemyBase* Enemy = Fixture.SpawnEnemy();
		if (!TestNotNull(TEXT("Excluded damage receiver"), Enemy)) return false;
		if (AController* Controller = Enemy->GetController()) Controller->UnPossess();
		FOutlierDamageRequest Request;
		Request.DamageAmount = Kind == 1 ? 0.0f : Kind == 3 ? 200.0f : 1.0f;
		Request.DamageTag = OutlierGameplayTags::Damage::Ranged();
		Request.DamageOrigin = FVector(500.0, 0.0, 0.0);
		Request.bHasDamageOrigin = Kind != 0;
		if (Kind == 2) Enemy->SetCanBeDamaged(false);
		Enemy->ReceiveOutlierDamage(Request);
		FEnemyHitClue Clue;
		TestFalse(TEXT("Unspecified, zero, rejected and lethal damage have no clue"),
			Enemy->TryGetPendingHitClue(Clue));
		TestFalse(TEXT("Excluded damage cannot start an ordinary response"), Enemy->HasHitResponse());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPistolClueLifetimeTest,
	"Outlier.Enemy.PistolHit.LifetimeAndRepeatedHits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPistolClueLifetimeTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	for (float Duration : {0.0f, 0.5f, 5.0f})
	{
		AEnemyBase* Enemy = Fixture.SpawnEnemy();
		if (!TestNotNull(TEXT("Pistol receiver"), Enemy)) return false;
		if (AController* Controller = Enemy->GetController()) Controller->UnPossess();
		const_cast<FEnemyStat&>(Enemy->GetRuntimeStat()).PistolHitClueGraceSeconds = 0.75f;
		FOutlierDamageRequest Request;
		Request.DamageAmount = 1.0f;
		Request.DamageTag = OutlierGameplayTags::Damage::Ranged();
		Request.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Pistol;
		Request.bHasDamageOrigin = true;
		Request.DamageOrigin = FVector::ZeroVector;
		Request.StunDurationSeconds = Duration;
		const double OccurredAt = Fixture.World->GetTimeSeconds();
		TestTrue(TEXT("Pistol damage is accepted"), Enemy->ReceiveOutlierDamage(Request) > 0.0f);
		FEnemyHitClue First, Second;
		if (!TestTrue(TEXT("Pistol retains a deferred origin"), Enemy->TryGetPendingHitClue(First))) return false;
		TestEqual(TEXT("Lifetime uses this hit's applied stun plus configured grace"), First.ExpiresAt, OccurredAt + Duration + 0.75);
		TestTrue(TEXT("World origin is a valid pistol source"), First.Location.IsZero());
		TestFalse(TEXT("Pistol alone never confirms combat"), Enemy->IsInCombat());
		Fixture.World->TimeSeconds += 0.25f;
		Request.StunDurationSeconds = 0.25f;
		Request.DamageOrigin = FVector(0, 700, 0);
		const double SecondAt = Fixture.World->GetTimeSeconds();
		Enemy->ReceiveOutlierDamage(Request);
		if (!TestTrue(TEXT("New pistol hit replaces the pending event"), Enemy->TryGetPendingHitClue(Second))) return false;
		TestTrue(TEXT("Repeated hit has a new serial"), Second.Serial > First.Serial);
		TestEqual(TEXT("New hit's deadline excludes an older longer stun"), Second.ExpiresAt, SecondAt + 1.0);
		TestFalse(TEXT("Older event cannot be republished"), Enemy->RecordHitClue(First));
		Fixture.World->TimeSeconds = Second.ExpiresAt + 0.1;
		Enemy->TickHitResponse(0.0f);
		TestFalse(TEXT("Longer unrelated stun cannot extend a stale pistol event"), Enemy->TryGetPendingHitClue(First));
		TestFalse(TEXT("Expired event does not start an action"), Enemy->HasHitResponse());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPistolRecoveryTest,
	"Outlier.Enemy.PistolHit.RecoveryAndExpiry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyPistolRecoveryTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	for (bool bExpired : {false, true})
	{
		AEnemyBase* Enemy = Fixture.SpawnEnemy();
		if (!TestNotNull(TEXT("Stunned pistol receiver"), Enemy)) return false;
		const_cast<FEnemyStat&>(Enemy->GetRuntimeStat()).PistolHitClueGraceSeconds = 1.0f;
		Enemy->EnterStun();
		FOutlierDamageRequest Request;
		Request.DamageAmount = 1.0f;
		Request.DamageTag = OutlierGameplayTags::Damage::Ranged();
		Request.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Pistol;
		Request.bHasDamageOrigin = true;
		Request.DamageOrigin = FVector(-700, 0, 0);
		Enemy->ReceiveOutlierDamage(Request);
		FEnemyHitClue Clue;
		if (!TestTrue(TEXT("Another active stun preserves the pending pistol clue"), Enemy->TryGetPendingHitClue(Clue))) return false;
		TestFalse(TEXT("No-stun pistol cannot bypass another stun"), Enemy->HasHitResponse());
		Fixture.World->TimeSeconds += bExpired ? 1.25f : 0.25f;
		Enemy->RestoreStateAfterStun();
		TestEqual(TEXT("Recovery starts only an unexpired investigation"), Enemy->HasHitResponse(), !bExpired);
		if (!bExpired)
		{
			TestEqual(TEXT("Recovery enters Alert rather than Combat"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
			TestTrue(TEXT("Recovery uses the original snapshot"), Enemy->GetPerceptionGoal().Location.Equals(Request.DamageOrigin));
			Enemy->ClearHitClues();
			Fixture.World->TimeSeconds = Clue.OccurredAt + 4.1;
			TestFalse(TEXT("Recovery did not restart the four-second alert clock"), Enemy->IsAlertHoldActive());
			Enemy->RestoreStateAfterStun();
			TestFalse(TEXT("Duplicate recovery cannot replay the consumed event"), Enemy->HasHitResponse());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitResponseQueueTest,
	"Outlier.Enemy.HitResponse.ConfirmationQueueAndLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitResponseQueueTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	AEnemyAIController* Controller = Fixture.World ? Fixture.World->SpawnActor<AEnemyAIController>() : nullptr;
	if (!TestNotNull(TEXT("Response enemy"), Enemy) || !TestNotNull(TEXT("Response controller"), Controller)) return false;
	Controller->Possess(Enemy);
	FEnemyStat& Stat = const_cast<FEnemyStat&>(Enemy->GetRuntimeStat());
	Stat.HitDirectionMinDuration = 0.4f;
	Stat.HitDirectionMaxDuration = 0.5f;
	FEnemyHitClue First, Second, Latest, Read;
	if (!TestTrue(TEXT("Initial event is created at the actual world time"), Enemy->CreateHitClue(
		FVector(500.0, 0.0, 0.0), Fixture.World->GetTimeSeconds(), 0.1, First))
		|| !TestTrue(TEXT("Initial event is recorded"), Enemy->RecordHitClue(First))) return false;
	Enemy->TickHitResponse(0.0f);
	if (!TestTrue(TEXT("Fresh clue starts confirmation"), Enemy->IsHitDirectionConfirmationActive())) return false;
	const uint64 FirstInvestigation = Enemy->GetLocationInvestigationSerial();
	TestEqual(TEXT("Hit alone enters Alert"), Enemy->GetCombatState(), EEnemyCombatState::Alert);
	TestTrue(TEXT("First event owns the active slot"), Enemy->IsHitInvestigationCurrent(First.Serial));
	Fixture.World->TimeSeconds = 10.2f;
	if (!TestTrue(TEXT("Second event is created at the actual world time"), Enemy->CreateHitClue(
		FVector(-500.0, 0.0, 0.0), Fixture.World->GetTimeSeconds(), 5.0, Second))
		|| !TestTrue(TEXT("Second event is recorded"), Enemy->RecordHitClue(Second))
		|| !TestTrue(TEXT("Latest event is created at the actual world time"), Enemy->CreateHitClue(
			FVector(0.0, 500.0, 0.0), Fixture.World->GetTimeSeconds(), 5.0, Latest))
		|| !TestTrue(TEXT("Latest event is recorded"), Enemy->RecordHitClue(Latest))) return false;
	Enemy->TickHitResponse(0.2f);
	TestTrue(TEXT("Minimum confirmation preserves the first direction despite its expired waiting TTL"),
		Enemy->IsHitInvestigationCurrent(First.Serial));
	TestTrue(TEXT("Only the newest alternate direction waits"), Enemy->TryGetPendingHitClue(Read) && Read.Serial == Latest.Serial);
	Fixture.World->TimeSeconds = 10.6f;
	Enemy->TickHitResponse(0.4f);
	TestTrue(TEXT("Latest alternate direction is selected after the original window"), Enemy->IsHitInvestigationCurrent(Latest.Serial));
	TestFalse(TEXT("Old callback cannot complete replacement investigation"), Enemy->IsLocationInvestigationCurrent(FirstInvestigation));
	TestFalse(TEXT("Old completion cannot remove the replacement slot"), Enemy->EndHitInvestigation(First.Serial));
	Fixture.World->TimeSeconds = 16.0f;
	Enemy->TickHitResponse(0.5f);
	TestTrue(TEXT("A started investigation survives its source deadline"), Enemy->HasHitResponse());
	Enemy->ClearHitClues();
	TestFalse(TEXT("Lifecycle clear releases runtime response"), Enemy->HasHitResponse());
	Enemy->CreateHitClue(FVector(-500.0, 0.0, 0.0), Fixture.World->GetTimeSeconds(), 0.1, Read);
	Enemy->RecordHitClue(Read);
	Fixture.World->TimeSeconds = 16.2f;
	Enemy->TickHitResponse(0.2f);
	TestFalse(TEXT("An expired waiting clue cannot start a response"), Enemy->HasHitResponse());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHitUpperStateTest,
	"Outlier.Enemy.HitResponse.UpperStateAndReturning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHitUpperStateTest::RunTest(const FString& Parameters)
{
	FEnemyHitClueTestWorld Fixture;
	AEnemyBase* Returning = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Returning enemy"), Returning)) return false;
	FEnumProperty* CombatState = FindFProperty<FEnumProperty>(Returning->GetClass(), TEXT("CombatState"));
	if (!TestNotNull(TEXT("Returning setup can select the existing state"), CombatState)) return false;
	// 방/경로 생성 대신 귀환 중 실행 입력만 고정한다. 피격 반응이 이동 속도와 입력을 정지시키는지 검사한다.
	CombatState->GetUnderlyingProperty()->SetIntPropertyValue(CombatState->ContainerPtrToValuePtr<void>(Returning),
		static_cast<int64>(EEnemyCombatState::Returning));
	FEnemyStat& ReturnStat = const_cast<FEnemyStat&>(Returning->GetRuntimeStat());
	ReturnStat.HitDirectionMinDuration = 0.1f;
	ReturnStat.HitDirectionMaxDuration = 0.2f;
	ReturnStat.HitReturnReactionInterval = 1.0f;
	UCharacterMovementComponent* Movement = Returning->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Flying);
	Movement->Velocity = FVector(150.0, 0.0, 0.0);
	Returning->AddMovementInput(FVector::ForwardVector, 1.0f, true);
	const FVector Velocity = Movement->Velocity;
	const FVector PendingInput = Returning->GetPendingMovementInputVector();
	FEnemyHitClue First, Next, Read;
	if (!TestTrue(TEXT("Returning hit is issued"), Returning->CreateHitClue(FVector(-500.0, 0.0, 0.0),
		Fixture.World->GetTimeSeconds(), 5.0, First)) || !Returning->RecordHitClue(First)) return false;
	Returning->TickHitResponse(0.0f);
	TestTrue(TEXT("Returning hit starts direction confirmation"), Returning->IsHitDirectionConfirmationActive());
	TestTrue(TEXT("Returning hit does not stop current velocity"), Movement->Velocity.Equals(Velocity));
	TestTrue(TEXT("Returning hit preserves movement input"), Returning->GetPendingMovementInputVector().Equals(PendingInput));
	TestEqual(TEXT("Hit preserves the Returning state"), Returning->GetCombatState(), EEnemyCombatState::Returning);
	Fixture.World->TimeSeconds = 10.3f;
	Returning->TickHitResponse(0.3f);
	TestFalse(TEXT("Returning response finishes without a location search"), Returning->HasHitResponse());
	TestFalse(TEXT("Returning confirmation cannot create a search episode"), Returning->GetSearchEpisode().bActive);
	TestTrue(TEXT("Finishing confirmation preserves return velocity"), Movement->Velocity.Equals(Velocity));
	Returning->CreateHitClue(FVector(0.0, 500.0, 0.0), Fixture.World->GetTimeSeconds(), 5.0, Next);
	Returning->RecordHitClue(Next);
	Returning->TickHitResponse(0.0f);
	TestFalse(TEXT("Repeated returning hits respect the reaction interval"), Returning->HasHitResponse());
	TestFalse(TEXT("Suppressed repeated hit is consumed rather than delayed indefinitely"), Returning->TryGetPendingHitClue(Read));

	AEnemyBase* Stunned = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Stun response enemy"), Stunned)) return false;
	Stunned->CreateHitClue(FVector(500.0, 0.0, 0.0), Fixture.World->GetTimeSeconds(), 5.0, First);
	Stunned->RecordHitClue(First);
	Stunned->TickHitResponse(0.0f);
	if (!TestTrue(TEXT("An action is active before stun"), Stunned->HasHitResponse())) return false;
	Stunned->EnterStun();
	Stunned->TickHitResponse(0.0f);
	TestFalse(TEXT("Stun consumes the interrupted active action"), Stunned->HasHitResponse());
	TestFalse(TEXT("Old interrupted event cannot complete later"), Stunned->IsHitInvestigationCurrent(First.Serial));
	Fixture.World->TimeSeconds = 10.4f;
	Stunned->CreateHitClue(FVector(-500.0, 0.0, 0.0), Fixture.World->GetTimeSeconds(), 5.0, Next);
	Stunned->RecordHitClue(Next);
	Stunned->TickHitResponse(0.1f);
	TestFalse(TEXT("A new hit cannot start an action during stun"), Stunned->HasHitResponse());
	TestTrue(TEXT("Stun preserves the fresh pending event"), Stunned->TryGetPendingHitClue(Read) && Read.Serial == Next.Serial);
	Stunned->RestoreStateAfterStun();
	Stunned->TickHitResponse(0.0f);
	TestTrue(TEXT("Recovery starts only the fresh unexpired event"), Stunned->IsHitInvestigationCurrent(Next.Serial));
	TestFalse(TEXT("Recovery cannot revive the interrupted old event"), Stunned->IsHitInvestigationCurrent(First.Serial));
	return true;
}

#endif
