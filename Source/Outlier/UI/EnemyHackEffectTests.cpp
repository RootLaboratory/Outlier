#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackType.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "UI/EnemyHudWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

namespace
{
struct FEnemyHackEffectTestWorld
{
	UWorld* World = nullptr;

	FEnemyHackEffectTestWorld()
	{
		if (!GEngine)
		{
			return;
		}
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("EnemyHackEffectTest")), GetTransientPackage());
		if (!World)
		{
			return;
		}
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		++GFrameCounter;
		World->GetTimerManager().Tick(0.0f);
	}

	~FEnemyHackEffectTestWorld()
	{
		if (!World)
		{
			return;
		}
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	AEnemyBase* SpawnEnemy() const
	{
		if (!World)
		{
			return nullptr;
		}
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

	void AdvanceTime(float Seconds) const
	{
		// AI/물리 없이 서버 게임 시계와 TimerManager만 같은 간격으로 진행한다.
		World->TimeSeconds += Seconds;
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHackEffectTimeTest,
	"Outlier.Enemy.HackEffect.MinutesAndSeconds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHackEffectTimeTest::RunTest(const FString& Parameters)
{
	struct FTimeCase { float Remaining; int32 Minutes; int32 Seconds; };
	const FTimeCase Cases[] = {
		{ 60.0f, 1, 0 }, { 59.2f, 1, 0 }, { 59.0f, 0, 59 },
		{ 1.0f, 0, 1 }, { 0.2f, 0, 1 }, { 0.0f, 0, 0 },
		{ -1.0f, 0, 0 }, { 120.0f, 1, 0 }
	};
	for (const FTimeCase& Case : Cases)
	{
		const FEnemyHackEffectTime Time = FEnemyHackEffectTime::FromRemainingTime(Case.Remaining);
		TestEqual(FString::Printf(TEXT("%.2f remaining: minutes"), Case.Remaining), Time.Minutes, Case.Minutes);
		TestEqual(FString::Printf(TEXT("%.2f remaining: seconds"), Case.Remaining), Time.Seconds, Case.Seconds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHackEffectLifetimeTest,
	"Outlier.Enemy.HackEffect.Lifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHackEffectLifetimeTest::RunTest(const FString& Parameters)
{
	FEnemyHackEffectTestWorld Fixture;
	AEnemyBase* Enemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Native Enemy spawns"), Enemy))
	{
		return false;
	}
	TestFalse(TEXT("No effect before an explicit gameplay start"), Enemy->IsHackEffectActive());
	TestEqual(TEXT("Inactive effect has no remaining time"), Enemy->GetHackEffectRemainingTime(), 0.0f);
	TestTrue(TEXT("Server starts the common lifetime"), Enemy->BeginHackEffect());
	const FEnemyHackEffectState StartedState = Enemy->HackEffectState;
	TestFalse(TEXT("Duplicate start is rejected"), Enemy->BeginHackEffect());
	TestEqual(TEXT("Duplicate start preserves expiry"), Enemy->HackEffectState.EndServerTime, StartedState.EndServerTime);
	Fixture.AdvanceTime(59.99f);
	TestTrue(TEXT("Effect remains active before 60 seconds"), Enemy->IsHackEffectActive());
	TestEqual(TEXT("Last fractional second displays one second"),
		Enemy->GetHackEffectRemainingMinutesAndSeconds().Seconds, 1);
	Fixture.AdvanceTime(0.02f);
	TestFalse(TEXT("Effect expires at the server deadline"), Enemy->IsHackEffectActive());
	TestTrue(TEXT("Natural expiry retains its reason"), Enemy->HackEffectState.EndReason == EEnemyHackEffectEndReason::Expired);
	TestFalse(TEXT("Duplicate terminal callback cannot end twice"), Enemy->EndHackEffect(EEnemyHackEffectEndReason::TargetDied));
	TestFalse(TEXT("Late success cannot restart an expired effect"), Enemy->BeginHackEffect());
	TestTrue(TEXT("Natural expiry invokes the existing GAS death path"), Enemy->IsDead());
	TestTrue(TEXT("Expiry performs common death cleanup"), Enemy->bDeathCleanupPerformed);

	// 사망한 Actor를 단순 상태 초기화로 재사용하지 않는다. 새 생존 대상에서 대여 경계를 검사한다.
	Enemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Lease test Enemy spawns"), Enemy))
	{
		return false;
	}
	Enemy->ResetHackEffectForPool();
	TestTrue(TEXT("A fresh lease may start its own lifetime"), Enemy->BeginHackEffect());
	const uint32 NewRevision = Enemy->HackEffectState.Revision;
	Enemy->HandleHackEffectExpired(StartedState.Revision, 0, 0);
	TestTrue(TEXT("An old expiry callback cannot end the new lease"), Enemy->IsHackEffectActive());
	TestEqual(TEXT("An old callback cannot change the revision"), Enemy->HackEffectState.Revision, NewRevision);
	TestTrue(TEXT("Manual release ends the common lifetime"), Enemy->EndHackEffect(EEnemyHackEffectEndReason::ManualRelease));
	TestFalse(TEXT("Explicit end clears the authority timer"), Fixture.World->GetTimerManager().TimerExists(Enemy->HackEffectExpiryTimer));

	Enemy->ResetHackEffectForPool();
	Enemy->BeginHackEffect();
	Enemy->PerformDeathCleanup();
	TestFalse(TEXT("Common death cleanup cancels the lifetime"), Enemy->IsHackEffectActive());
	TestTrue(TEXT("Common death cleanup records early death"), Enemy->HackEffectState.EndReason == EEnemyHackEffectEndReason::TargetDied);

	// 상태 복제가 종료 RPC보다 늦게 도착해도 종료된 상태를 되살리지 않는다.
	const FEnemyHackEffectState EndedState = Enemy->HackEffectState;
	Enemy->HackEffectState = StartedState;
	Enemy->OnRep_HackEffectState(EndedState);
	TestEqual(TEXT("Late replication preserves the latest revision"), Enemy->HackEffectState.Revision, EndedState.Revision);
	TestFalse(TEXT("Late replication cannot reactivate the effect"), Enemy->IsHackEffectActive());
	TestFalse(TEXT("Death cleanup prevents a new lifetime"), Enemy->BeginHackEffect());
	Enemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Authority test Enemy spawns"), Enemy))
	{
		return false;
	}
	Enemy->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("A client cannot start the authority lifetime"), Enemy->BeginHackEffect());
	TestFalse(TEXT("A client cannot end the authority lifetime"), Enemy->EndHackEffect(EEnemyHackEffectEndReason::Expired));
	Enemy->SetRole(ROLE_Authority);
	Enemy->BeginHackEffect();
	Enemy->InvalidatePoolLease();
	TestFalse(TEXT("Invalidating a lease cancels its effect"), Enemy->IsHackEffectActive());
	TestFalse(TEXT("Invalidating a lease clears its timer"), Fixture.World->GetTimerManager().TimerExists(Enemy->HackEffectExpiryTimer));
	Fixture.AdvanceTime(61.0f);
	TestFalse(TEXT("An invalidated lifetime cannot kill its target later"), Enemy->IsDead());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHackEffectGameplayTest,
	"Outlier.Enemy.HackEffect.Gameplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHackEffectGameplayTest::RunTest(const FString& Parameters)
{
	FEnemyHackEffectTestWorld Fixture;
	if (!TestNotNull(TEXT("Gameplay world exists"), Fixture.World))
	{
		return false;
	}
	APartnerPlayerController* Controller = Fixture.World->SpawnActor<APartnerPlayerController>();
	APartnerCharacter* Partner = Fixture.World->SpawnActor<APartnerCharacter>();
	if (!TestNotNull(TEXT("Partner controller spawns"), Controller)
		|| !TestNotNull(TEXT("Partner spawns"), Partner))
	{
		return false;
	}
	Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);
	Controller->Possess(Partner);

	// 전환 연출/네트워크 ACK 대신 서버 commit 직전 상태만 준비한다. 실제 commit/복귀 코드를 실행한다.
	const auto CommitEnemy = [this, &Fixture, Controller, Partner](bool bFinishPresentation = true) -> AEnemyBase*
	{
		AEnemyBase* Enemy = Fixture.SpawnEnemy();
		if (!Enemy)
		{
			return nullptr;
		}
		Enemy->ApplyPossessionPendingState();
		Controller->PendingEnemyPossessionTarget = Enemy;
		Controller->PendingEnemyPossessionSource = Partner;
		TestFalse(TEXT("Pending possession has no lifetime"), Enemy->IsHackEffectActive());
		Controller->CommitPendingEnemyPossession(Enemy);
		TestTrue(TEXT("Commit transfers control to Enemy"), Controller->GetPawn() == Enemy);
		TestFalse(TEXT("Commit alone does not start the lifetime"), Enemy->IsHackEffectActive());
		Fixture.AdvanceTime(2.0f);
		TestFalse(TEXT("Presentation time does not consume the lifetime"), Enemy->IsHackEffectActive());
		Controller->ServerNotifyHackPossessionRevealFinished_Implementation(nullptr);
		TestFalse(TEXT("An invalid completion cannot start the lifetime"), Enemy->IsHackEffectActive());
		if (bFinishPresentation)
		{
			Controller->ServerNotifyHackPossessionRevealFinished_Implementation(Enemy);
			TestTrue(TEXT("Presentation completion starts the lifetime"), Enemy->IsHackEffectActive());
			TestEqual(TEXT("Presentation completion grants the full minute"), Enemy->GetHackEffectRemainingTime(), 60.0f);
			const double Deadline = Enemy->HackEffectState.EndServerTime;
			Controller->ServerNotifyHackPossessionRevealFinished_Implementation(Enemy);
			TestEqual(TEXT("Duplicate completion cannot restart the minute"), Enemy->HackEffectState.EndServerTime, Deadline);
		}
		return Enemy;
	};

	AEnemyBase* Enemy = CommitEnemy();
	if (!TestNotNull(TEXT("Committed Enemy exists"), Enemy))
	{
		return false;
	}
	Controller->ReleaseEnemyPossession();
	TestTrue(TEXT("Manual release kills the target"), Enemy->IsDead());
	TestTrue(TEXT("Manual release restores Partner control"), Controller->GetPawn() == Partner);
	TestTrue(TEXT("Manual release preserves its terminal reason"), Enemy->HackEffectState.EndReason == EEnemyHackEffectEndReason::ManualRelease);
	TestFalse(TEXT("Return resumes suspended Partner cooldowns"), Partner->GetOutlierAbilitySystemComponent()->ArePartnerSkillCooldownsSuspended());
	const uint32 ManualRevision = Enemy->HackEffectState.Revision;
	Controller->ReleaseEnemyPossession();
	TestEqual(TEXT("Repeated release does not end again"), Enemy->HackEffectState.Revision, ManualRevision);

	Enemy = CommitEnemy();
	if (!Enemy)
	{
		return false;
	}
	Fixture.AdvanceTime(60.01f);
	TestTrue(TEXT("Expiry kills a possessed target"), Enemy->IsDead());
	TestTrue(TEXT("Expiry restores Partner control"), Controller->GetPawn() == Partner);
	TestTrue(TEXT("Death cleanup preserves expiry reason"), Enemy->HackEffectState.EndReason == EEnemyHackEffectEndReason::Expired);

	Enemy = CommitEnemy();
	if (!Enemy)
	{
		return false;
	}
	const uint32 CleanupRevision = Enemy->HackEffectState.Revision;
	Controller->ReleaseEnemyPossessionForCleanup();
	TestTrue(TEXT("Cleanup restores Partner control"), Controller->GetPawn() == Partner);
	TestFalse(TEXT("Cleanup does not kill"), Enemy->IsDead());
	Enemy->HandleHackEffectExpired(CleanupRevision, 0, 0);
	Fixture.AdvanceTime(60.01f);
	TestFalse(TEXT("Old expiry cannot kill after cleanup"), Enemy->IsDead());

	Enemy = CommitEnemy();
	if (!Enemy)
	{
		return false;
	}
	const double DisconnectDeadline = Enemy->HackEffectState.EndServerTime;
	Controller->ReleaseEnemyPossessionForDisconnect();
	TestFalse(TEXT("Temporary disconnect does not immediately kill"), Enemy->IsDead());
	TestEqual(TEXT("Temporary disconnect retains the deadline"), Enemy->HackEffectState.EndServerTime, DisconnectDeadline);
	TestTrue(TEXT("Temporary disconnect retains the active lifetime"), Enemy->IsHackEffectActive());
	Fixture.AdvanceTime(60.01f);
	TestTrue(TEXT("Disconnected target still expires"), Enemy->IsDead());

	Enemy = CommitEnemy();
	if (!Enemy)
	{
		return false;
	}
	Enemy->Die();
	const uint32 DeathRevision = Enemy->HackEffectState.Revision;
	TestTrue(TEXT("Early death restores Partner"), Controller->GetPawn() == Partner);
	TestTrue(TEXT("Early death records TargetDied"), Enemy->HackEffectState.EndReason == EEnemyHackEffectEndReason::TargetDied);
	Fixture.AdvanceTime(60.01f);
	TestEqual(TEXT("Expiry cannot repeat early death"), Enemy->HackEffectState.Revision, DeathRevision);

	Enemy = CommitEnemy(false);
	if (!Enemy)
	{
		return false;
	}
	Controller->ReleaseEnemyPossession();
	TestTrue(TEXT("Manual release during presentation still kills"), Enemy->IsDead());
	TestTrue(TEXT("Manual release during presentation restores Partner"), Controller->GetPawn() == Partner);
	Controller->ServerNotifyHackPossessionRevealFinished_Implementation(Enemy);
	TestFalse(TEXT("Late presentation completion cannot revive a released target"), Enemy->IsHackEffectActive());

	Enemy = CommitEnemy(false);
	if (!Enemy)
	{
		return false;
	}
	Controller->ReleaseEnemyPossessionForCleanup();
	Controller->ServerNotifyHackPossessionRevealFinished_Implementation(Enemy);
	TestFalse(TEXT("Completion after cleanup cannot start the old target"), Enemy->IsHackEffectActive());
	TestFalse(TEXT("Cleanup during presentation does not kill"), Enemy->IsDead());

	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Turret spawns"), Turret))
	{
		return false;
	}
	Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
	Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
	Turret->TurretLifecycleState = EAutoTurretLifecycleState::Active;
	FHackResultContext Context;
	Context.Result = EHackResult::Success;
	Turret->HandleHackEffect(HackGameplayTags::Effect::ChangeTeam(), Context);
	TestTrue(TEXT("Turret team conversion starts the lifetime"), Turret->IsHackedToPlayerTeam() && Turret->IsHackEffectActive());
	const double TurretDeadline = Turret->HackEffectState.EndServerTime;
	Fixture.AdvanceTime(1.0f);
	Turret->HandleHackEffect(HackGameplayTags::Effect::ChangeTeam(), Context);
	TestEqual(TEXT("Duplicate turret success cannot extend time"), Turret->HackEffectState.EndServerTime, TurretDeadline);
	Fixture.AdvanceTime(59.01f);
	TestTrue(TEXT("Turret expiry uses GAS death"), Turret->IsDead());
	TestTrue(TEXT("Turret remains on hacked team at death"), Turret->IsHackedToPlayerTeam());
	TestTrue(TEXT("Turret expiry enters its existing death lifecycle"),
		Turret->GetTurretLifecycleState() == EAutoTurretLifecycleState::DeadPresentation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHackEffectHUDTest,
	"Outlier.UI.Enemy.HackEffectTimer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHackEffectHUDTest::RunTest(const FString& Parameters)
{
	FEnemyHackEffectTestWorld Fixture;
	AEnemyBase* FirstEnemy = Fixture.SpawnEnemy();
	AEnemyBase* NextEnemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("First Enemy spawns"), FirstEnemy)
		|| !TestNotNull(TEXT("Next Enemy spawns"), NextEnemy))
	{
		return false;
	}

	// Abstract인 네이티브 HUD의 인스턴스는 메모리 내 BP 자식으로만 만든다. 콘텐츠 에셋은 저장하지 않는다.
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UEnemyHudWidget::StaticClass(),
		GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("HackTimerHUDTest")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Transient HUD Blueprint is created"), Blueprint))
	{
		return false;
	}
	Blueprint->bGenerateAbstractClass = false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestNotNull(TEXT("Transient HUD class compiles"), Blueprint->GeneratedClass.Get())
		|| !TestFalse(TEXT("HUD test class is concrete"), Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_Abstract)))
	{
		return false;
	}
	UEnemyHudWidget* Hud = NewObject<UEnemyHudWidget>(Fixture.World, Blueprint->GeneratedClass);
	Hud->HackEffectMinutesText = NewObject<UTextBlock>(Hud);
	Hud->HackEffectSecondsText = NewObject<UTextBlock>(Hud);
	Hud->OwningEnemy = FirstEnemy;
	Hud->BindHackTimer();
	TestTrue(TEXT("Inactive binding is hidden"), Hud->HackEffectMinutesText->GetVisibility() == ESlateVisibility::Collapsed);

	FirstEnemy->BeginHackEffect();
	TestEqual(TEXT("Minutes have two digits"), Hud->HackEffectMinutesText->GetText().ToString(), FString(TEXT("01")));
	TestEqual(TEXT("Seconds have two digits"), Hud->HackEffectSecondsText->GetText().ToString(), FString(TEXT("00")));
	Fixture.AdvanceTime(1.0f);
	Hud->RefreshHackTimer();
	TestEqual(TEXT("Minute rollover updates minutes"), Hud->HackEffectMinutesText->GetText().ToString(), FString(TEXT("00")));
	TestEqual(TEXT("Minute rollover updates seconds"), Hud->HackEffectSecondsText->GetText().ToString(), FString(TEXT("59")));

	Hud->ReleaseHackTimer();
	Fixture.AdvanceTime(10.0f);
	Hud->BindHackTimer();
	TestEqual(TEXT("Late binding resumes current server time"), Hud->HackEffectTime.Seconds, 49);
	APartnerPlayerController* Controller = Fixture.World->SpawnActor<APartnerPlayerController>();
	if (!TestNotNull(TEXT("HUD owner controller spawns"), Controller))
	{
		return false;
	}
	Controller->EnemyHudInstance = Hud;
	// 소유 Controller 통보 -> Partner 복귀 -> Enemy 종료 복제 순서도 마지막 숫자를 유지한다.
	Controller->ClientPrepareExpiredEnemyHud_Implementation(FirstEnemy);
	Controller->RequestPopEnemyHud();
	TestTrue(TEXT("Partner return defers only the expiring HUD removal"), Controller->bEnemyHudPopPending);
	TestTrue(TEXT("Expiring HUD remains until its final display"), Controller->EnemyHudInstance == Hud);
	Hud->RefreshHackTimer();
	Hud->HandleHackEffectStarted(FirstEnemy, 60.0f);
	TestEqual(TEXT("Late active state cannot overwrite terminal zero"), Hud->HackEffectTime.Seconds, 0);
	FirstEnemy->EndHackEffect(EEnemyHackEffectEndReason::Expired);
	TestTrue(TEXT("Duplicate expiry retains the hide callback"), Fixture.World->GetTimerManager().TimerExists(Hud->HackTimerHideHandle));
	TestEqual(TEXT("Natural expiry shows zero"), Hud->HackEffectTime.Seconds, 0);
	TestTrue(TEXT("Zero remains visible in the terminal frame"), Hud->HackEffectMinutesText->GetVisibility() == ESlateVisibility::HitTestInvisible);
	Fixture.World->GetTimerManager().ClearTimer(Hud->HackTimerHideHandle);
	Hud->HideExpiredHackTimer();
	TestTrue(TEXT("A same-frame callback cannot hide zero"), Hud->bHackTimerShowingExpiry);
	// 실제 렌더러 대신 Slate 구성과 Paint 완료 표식 사이의 제거 경계를 검사한다.
	const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
	Fixture.AdvanceTime(0.1f);
	TestTrue(TEXT("A mounted HUD waits for its terminal Paint"), Hud->bHackTimerShowingExpiry);
	Hud->bHackTimerExpiryPainted = true;
	Fixture.AdvanceTime(0.1f);
	TestTrue(TEXT("Zero is hidden after the terminal frame"), Hud->HackEffectMinutesText->GetVisibility() == ESlateVisibility::Collapsed);
	TestNull(TEXT("Final display removes the old Enemy HUD"), Controller->EnemyHudInstance.Get());
	TestFalse(TEXT("Final display clears the pending Pop state"), Controller->bEnemyHudPopPending);

	// 다음 대상에 연결한 뒤 이전 대상의 종료/숨김 콜백을 재현한다.
	NextEnemy->BeginHackEffect();
	Hud->InitializeForEnemy(NextEnemy);
	Hud->HandleHackEffectEnded(FirstEnemy, EEnemyHackEffectEndReason::Expired);
	Hud->HideExpiredHackTimer();
	TestEqual(TEXT("Old callbacks preserve the new target's minute"), Hud->HackEffectTime.Minutes, 1);
	TestTrue(TEXT("Old callbacks preserve the new timer's visibility"), Hud->HackEffectMinutesText->GetVisibility() == ESlateVisibility::HitTestInvisible);
	NextEnemy->EndHackEffect(EEnemyHackEffectEndReason::ManualRelease);
	TestTrue(TEXT("Manual release hides the timer immediately"), Hud->HackEffectMinutesText->GetVisibility() == ESlateVisibility::Collapsed);
	TestFalse(TEXT("Manual release stops UI refresh"), Fixture.World->GetTimerManager().TimerExists(Hud->HackTimerRefreshHandle));
	TestFalse(TEXT("Manual release unsubscribes the HUD"), Hud->bHackTimerBound);
	Controller->EnemyHudInstance = Hud;
	Controller->RequestPopEnemyHud();
	TestNull(TEXT("Manual release removes the HUD immediately"), Controller->EnemyHudInstance.Get());
	NextEnemy->ResetHackEffectForPool();
	NextEnemy->BeginHackEffect();
	Hud->BindHackTimer();
	Hud->ReleaseDamageFeedback();
	TestFalse(TEXT("The existing HUD Pop path also unsubscribes time"), Hud->bHackTimerBound);
	TestFalse(TEXT("The existing HUD Pop path clears refresh"), Fixture.World->GetTimerManager().TimerExists(Hud->HackTimerRefreshHandle));
	Hud->BindHackTimer();
	NextEnemy->Destroy();
	TestFalse(TEXT("Target loss stops refresh even outside BeginPlay"), Hud->GetOwningEnemy() && IsValid(Hud->GetOwningEnemy()));
	Hud->RefreshHackTimer();
	TestTrue(TEXT("Target loss hides stale numbers"), Hud->HackEffectMinutesText->GetVisibility() == ESlateVisibility::Collapsed);
	Hud->InitializeForEnemy(nullptr);
	TestNull(TEXT("Blueprint-style null binding releases the target"), Hud->GetOwningEnemy());
	TestFalse(TEXT("Null binding has no live timer subscription"), Hud->bHackTimerBound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyHackEffectAssetBindingTest,
	"Outlier.UI.Enemy.HackEffectAssetBinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyHackEffectAssetBindingTest::RunTest(const FString& Parameters)
{
	FEnemyHackEffectTestWorld Fixture;
	if (!TestNotNull(TEXT("Asset binding world exists"), Fixture.World))
	{
		return false;
	}
	const TCHAR* Paths[] = {
		TEXT("/Game/Blueprints/Widget/Enemy/WBP_EnemyHud_Rifle.WBP_EnemyHud_Rifle_C"),
		TEXT("/Game/Blueprints/Widget/Enemy/WBP_EnemyHud_Melee.WBP_EnemyHud_Melee_C")
	};
	for (const TCHAR* Path : Paths)
	{
		UClass* HudClass = LoadClass<UEnemyHudWidget>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("HUD class loads: %s"), Path), HudClass))
		{
			continue;
		}
		UEnemyHudWidget* Hud = CreateWidget<UEnemyHudWidget>(Fixture.World, HudClass);
		if (!TestNotNull(TEXT("HUD initializes its real WidgetTree"), Hud))
		{
			continue;
		}
		// 이름 문자열 존재만으로는 바인딩 성공이 아니다. 생성된 네이티브 TextBlock 참조를 검사한다.
		TestNotNull(FString::Printf(TEXT("Minutes TextBlock binds: %s"), Path), Hud->HackEffectMinutesText.Get());
		TestNotNull(FString::Printf(TEXT("Seconds TextBlock binds: %s"), Path), Hud->HackEffectSecondsText.Get());
		Hud->InitializeForEnemy(nullptr);
	}
	return true;
}

#endif
