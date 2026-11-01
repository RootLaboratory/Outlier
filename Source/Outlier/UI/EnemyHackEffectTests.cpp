#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackType.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "UI/EnemyHudWidget.h"
#include "UI/HackEffectTimerWidget.h"
#include "UI/BreathingWidget.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include <limits>
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
	UHackEffectTimerWidget* Timer = NewObject<UHackEffectTimerWidget>(Fixture.World);
	Hud->HackEffectTimerWidget = Timer;
	TestTrue(TEXT("Seconds keep their existing white default"), Timer->HackTimerNormalColor.Equals(FLinearColor::White));
	TestTrue(TEXT("Frame glow defaults decode cyan from sRGB"),
		Timer->HackTimerPulseNormalColor.Equals(FLinearColor(FColor(105, 241, 223))));
	TestTrue(TEXT("Frame glow warning defaults decode red from sRGB"),
		Timer->HackTimerPulseWarningColor.Equals(FLinearColor(FColor(255, 100, 108))));
	Timer->HackEffectMinutesText = NewObject<UTextBlock>(Hud);
	Timer->HackEffectSecondsText = NewObject<UTextBlock>(Hud);
	Timer->Colon = NewObject<UTextBlock>(Hud);
	Timer->TimerFrame = NewObject<UImage>(Hud);
	for (const auto& Case : {TPair<float, float>(60.0f, 0.0f), {30.0f, 0.0f}, {19.5f, 0.5f}, {9.0f, 1.0f}, {0.0f, 1.0f}})
	{
		TestTrue(TEXT("Warning blend follows remaining time boundaries"),
			FMath::IsNearlyEqual(Timer->GetHackTimerBlendAlpha(Case.Key, 60.0f), Case.Value, KINDA_SMALL_NUMBER));
	}
	for (float InvalidDuration : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		TestEqual(TEXT("Invalid duration keeps the normal color"), Timer->GetHackTimerBlendAlpha(0.0f, InvalidDuration), 0.0f);
	}
	for (const auto& Invalid : {TPair<float, float>(0.2f, 0.5f), {0.5f, 0.5f}, {1.1f, 0.1f}, {0.5f, -0.1f},
		{std::numeric_limits<float>::quiet_NaN(), 0.1f}, {0.5f, std::numeric_limits<float>::infinity()}})
	{
		Timer->HackTimerColorBlendStartRatio = Invalid.Key;
		Timer->HackTimerColorBlendEndRatio = Invalid.Value;
		TestTrue(TEXT("Invalid ratios use both default thresholds"),
			FMath::IsNearlyEqual(Timer->GetHackTimerBlendAlpha(19.5f, 60.0f), 0.5f, KINDA_SMALL_NUMBER));
	}
	Timer->HackTimerColorBlendStartRatio = 0.5f;
	Timer->HackTimerColorBlendEndRatio = 0.15f;
	TestEqual(TEXT("Slow pulse period is two seconds"), Timer->GetHackTimerPulsePeriod(0.0f), 2.0f);
	TestEqual(TEXT("Urgent pulse period is six tenths of a second"), Timer->GetHackTimerPulsePeriod(1.0f), 0.6f);
	UBreathingWidget* Pulse = NewObject<UBreathingWidget>(Fixture.World);
	Pulse->BreathingImage = NewObject<UImage>(Pulse);
	Timer->HackEffectPulseWidget = Pulse;
	Pulse->ApplyBreathingSettings();
	TestNull(TEXT("Missing material does not create a pulse MID"), Pulse->DynamicMaterial.Get());
	Hud->InitializeForEnemy(FirstEnemy);
	TestTrue(TEXT("Inactive binding is hidden"), Timer->GetVisibility() == ESlateVisibility::Collapsed);

	// 실제 HUD처럼 연출 중에 Construct가 호출되어도 이후 시작 이벤트를 받아야 한다.
	Hud->NativeConstruct();
	TestTrue(TEXT("Construct before effect start preserves the event binding"), Timer->bHackTimerBound);
	TestFalse(TEXT("Construct before effect start keeps the pulse inactive"), Pulse->bPlaying);
	TestTrue(TEXT("Construct before effect start keeps the timer hidden"),
		Timer->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Possession presentation keeps the frame hidden"),
		Timer->GetVisibility() == ESlateVisibility::Collapsed);

	FirstEnemy->BeginHackEffect();
	TestTrue(TEXT("Effect start after Construct shows the timer"),
		Timer->GetVisibility() == ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("Effect start shows the frame together with the timer"),
		Timer->GetVisibility() == ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("Frame starts with its own cyan color"), Pulse->PulseColor.Equals(Timer->HackTimerPulseNormalColor));
	TestEqual(TEXT("Minutes have two digits"), Timer->HackEffectMinutesText->GetText().ToString(), FString(TEXT("01")));
	TestEqual(TEXT("Seconds have two digits"), Timer->HackEffectSecondsText->GetText().ToString(), FString(TEXT("00")));
	Fixture.AdvanceTime(1.0f);
	Timer->RefreshHackTimer();
	TestEqual(TEXT("Minute rollover updates minutes"), Timer->HackEffectMinutesText->GetText().ToString(), FString(TEXT("00")));
	TestEqual(TEXT("Minute rollover updates seconds"), Timer->HackEffectSecondsText->GetText().ToString(), FString(TEXT("59")));
	const float MidUrgency = FMath::Square(1.0f - 19.5f / 60.0f);
	for (const auto& Sample : {TPair<float, float>(0.0f, 0.0f), {0.25f, 0.5f}, {0.5f, 1.0f}, {0.75f, 0.5f}, {1.0f, 0.0f}})
	{
		Timer->TextColorPhase = Sample.Key;
		TestTrue(TEXT("Seconds breathe within the current warning intensity"),
			FMath::IsNearlyEqual(Timer->GetHackTimerTextColorAlpha(19.5f, 60.0f), Sample.Value * MidUrgency, KINDA_SMALL_NUMBER));
	}
	Timer->TextColorPhase = 0.5f;
	for (const auto& Sample : {TPair<float, float>(60.0f, 0.0f), {54.0f, 0.01f}, {45.0f, 0.0625f},
		{30.0f, 0.25f}, {15.0f, 0.5625f}, {6.0f, 0.81f}, {0.0f, 1.0f}})
	{
		const float Peak = Timer->GetHackTimerTextColorAlpha(Sample.Key, 60.0f);
		TestTrue(TEXT("Warning intensity grows nonlinearly from full duration"), FMath::IsNearlyEqual(Peak, Sample.Value, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Color intensity and speed follow the same curve"),
			FMath::IsNearlyEqual(Timer->GetHackTimerPulsePeriod(Peak), FMath::Lerp(2.0f, 0.6f, Sample.Value), KINDA_SMALL_NUMBER));
	}
	TestEqual(TEXT("Invalid duration keeps seconds at their normal color"), Timer->GetHackTimerTextColorAlpha(0.0f, 0.0f), 0.0f);
	TestEqual(TEXT("Zero seconds always use the warning color"), Timer->GetHackTimerTextColorAlpha(0.0f, 60.0f), 1.0f);
	Timer->TextColorPhase = 0.25f;
	Timer->UpdateHackTimerColor(19.5f, 60.0f);
	TestTrue(TEXT("Seconds use the interpolated warning color"), Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor()
		.Equals(FMath::Lerp(Timer->HackTimerNormalColor, Timer->HackTimerWarningColor, MidUrgency * 0.5f)));
	TestTrue(TEXT("Frame independently interpolates its configured colors"),
		Pulse->PulseColor.Equals(FMath::Lerp(Timer->HackTimerPulseNormalColor, Timer->HackTimerPulseWarningColor, 0.5f)));
	// WBP에서 프레임 색만 바꾸어도 초 텍스트의 보간색은 그대로 유지해야 한다.
	const FLinearColor DefaultPulseNormal = Timer->HackTimerPulseNormalColor;
	const FLinearColor DefaultPulseWarning = Timer->HackTimerPulseWarningColor;
	Timer->HackTimerPulseNormalColor = FLinearColor::Blue;
	Timer->HackTimerPulseWarningColor = FLinearColor::Green;
	Timer->UpdateHackTimerColor(19.5f, 60.0f);
	TestTrue(TEXT("Frame color settings accept independent WBP overrides"),
		Pulse->PulseColor.Equals(FMath::Lerp(FLinearColor::Blue, FLinearColor::Green, 0.5f)));
	TestTrue(TEXT("Frame overrides never change seconds colors"), Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor()
		.Equals(FMath::Lerp(Timer->HackTimerNormalColor, Timer->HackTimerWarningColor, MidUrgency * 0.5f)));
	Timer->HackTimerPulseNormalColor = DefaultPulseNormal;
	Timer->HackTimerPulseWarningColor = DefaultPulseWarning;
	const FLinearColor MidColor = Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor();
	Timer->TextColorPhase = 0.5f;
	Timer->UpdateHackTimerColor(19.4f, 60.0f);
	TestFalse(TEXT("Color advances without changing the displayed second"),
		Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor().Equals(MidColor));
	TestEqual(TEXT("Color refresh never rewrites the seconds"), Timer->HackEffectSecondsText->GetText().ToString(), FString(TEXT("59")));
	const FLinearColor TextColor = Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor();
	TestTrue(TEXT("Minutes share the seconds breathing color"),
		Timer->HackEffectMinutesText->GetColorAndOpacity().GetSpecifiedColor().Equals(TextColor));
	TestTrue(TEXT("Colon shares the seconds breathing color"), Timer->Colon->GetColorAndOpacity().GetSpecifiedColor().Equals(TextColor));
	// 색상 최고점과 속도를 따로 편집해도 서로의 커브를 덮어쓰지 않는다.
	const FRuntimeFloatCurve DefaultColorCurve = Timer->HackTimerTextColorAmountCurve;
	const FRuntimeFloatCurve DefaultSpeedCurve = Timer->HackTimerTextPulseSpeedCurve;
	Timer->HackTimerTextColorAmountCurve.GetRichCurve()->Reset();
	TestTrue(TEXT("An empty curve falls back to the original nonlinear intensity"), FMath::IsNearlyEqual(Timer->GetHackTimerTextColorAlpha(30.0f, 60.0f), 0.25f));
	Timer->HackTimerTextColorAmountCurve.GetRichCurve()->AddKey(0.0f, 0.2f);
	TestTrue(TEXT("An edited color curve sets the pulse peak"), FMath::IsNearlyEqual(Timer->GetHackTimerTextColorAlpha(30.0f, 60.0f), 0.2f));
	UCurveFloat* ExternalColorCurve = NewObject<UCurveFloat>(Timer);
	ExternalColorCurve->FloatCurve.AddKey(0.0f, 0.7f);
	Timer->HackTimerTextColorAmountCurve.ExternalCurve = ExternalColorCurve;
	TestTrue(TEXT("An external curve can drive text color"), FMath::IsNearlyEqual(Timer->GetHackTimerTextColorAlpha(30.0f, 60.0f), 0.7f));
	Timer->HackTimerTextColorAmountCurve = DefaultColorCurve;

	// 머티리얼 에셋 없이 실제 파라미터 조회와 위젯별 MID 분리까지 검사한다. 패키지는 저장하지 않는다.
	UMaterial* PulseMaterial = NewObject<UMaterial>();
	for (FName Name : {FName(TEXT("PulsePhase")), FName(TEXT("PulseAmount")), FName(TEXT("AspectRatio")), FName(TEXT("BaseOpacity")),
		FName(TEXT("SlowPeakOpacity")), FName(TEXT("FastPeakOpacity")), FName(TEXT("Padding")),
		FName(TEXT("Feather")), FName(TEXT("CornerRadius")), FName(TEXT("Shape"))})
	{
		UMaterialExpressionScalarParameter* Parameter = NewObject<UMaterialExpressionScalarParameter>(PulseMaterial);
		Parameter->ParameterName = Name;
		PulseMaterial->GetExpressionCollection().AddExpression(Parameter);
	}
	UMaterialExpressionVectorParameter* PulseColorExpression = NewObject<UMaterialExpressionVectorParameter>(PulseMaterial);
	PulseColorExpression->ParameterName = TEXT("PulseColor");
	PulseMaterial->GetExpressionCollection().AddExpression(PulseColorExpression);
	PulseMaterial->UpdateCachedExpressionData();
	Pulse->BreathingImage->SetBrushFromMaterial(NewObject<UMaterial>());
	Pulse->ApplyBreathingSettings();
	TestNull(TEXT("A material without pulse parameters is safely ignored"), Pulse->DynamicMaterial.Get());
	UMaterialInstanceDynamic* SharedMID = UMaterialInstanceDynamic::Create(PulseMaterial, GetTransientPackage());
	Pulse->BreathingMaterial = SharedMID;
	Pulse->ApplyBreathingSettings();
	if (!TestNotNull(TEXT("Pulse material is prepared"), Pulse->DynamicMaterial.Get()))
	{
		return false;
	}
	UBreathingWidget* OtherPulse = NewObject<UBreathingWidget>(Fixture.World);
	OtherPulse->BreathingImage = NewObject<UImage>(OtherPulse);
	OtherPulse->BreathingMaterial = SharedMID;
	OtherPulse->ApplyBreathingSettings();
	if (!TestNotNull(TEXT("Other HUD also prepares its material"), OtherPulse->DynamicMaterial.Get()))
	{
		return false;
	}
	TestTrue(TEXT("HUDs never modify a shared MID"), Pulse->DynamicMaterial != OtherPulse->DynamicMaterial
		&& Pulse->DynamicMaterial != SharedMID && OtherPulse->DynamicMaterial != SharedMID);
	Pulse->BaseOpacity = 0.2f;
	Pulse->Shape = EBreathingShape::Circle;
	UMaterialInstanceDynamic* OriginalMID = Pulse->DynamicMaterial;
	Pulse->ApplyBreathingSettings();
	TestTrue(TEXT("Reapplying WBP settings reuses the existing MID"), Pulse->DynamicMaterial == OriginalMID);
	float SettingValue = -1.0f;
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("BaseOpacity")), SettingValue);
	TestEqual(TEXT("WBP opacity settings reach the private MID"), SettingValue, 0.2f);
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Shape")), SettingValue);
	TestEqual(TEXT("WBP shape selects the UV circle"), SettingValue, 0.0f);
	Pulse->Shape = EBreathingShape::Ellipse;
	Pulse->ApplyBreathingSettings();
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Shape")), SettingValue);
	TestEqual(TEXT("WBP ellipse reaches the material without falling back to rounded rectangle"), SettingValue, 2.0f);
	OtherPulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("BaseOpacity")), SettingValue);
	TestEqual(TEXT("WBP settings never affect another instance"), SettingValue, 0.08f);
	Pulse->Feather = std::numeric_limits<float>::quiet_NaN();
	Pulse->ApplyBreathingSettings();
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Feather")), SettingValue);
	TestEqual(TEXT("Invalid WBP softness uses a finite default"), SettingValue, 0.12f);
	Pulse->SetBreathingState(FLinearColor::Green, 0.75f, 1.0f);
	TestTrue(TEXT("Standalone effects accept their own color"), Pulse->PulseColor.Equals(FLinearColor::Green));
	TestEqual(TEXT("Standalone effects accept their own amount"), Pulse->PulseAmount, 0.75f);
	Pulse->SetBreathingState(FLinearColor::White, 0.0f, 2.0f);
	Pulse->UpdateAspectRatio(FVector2D(0.0, 0.0));
	TestEqual(TEXT("Pre-layout geometry preserves the unset ratio"), Pulse->CachedAspectRatio, -1.0f);
	Pulse->UpdateAspectRatio(FVector2D(300.0, 100.0));
	float MaterialAspectRatio = -1.0f;
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("AspectRatio")), MaterialAspectRatio);
	TestEqual(TEXT("Wide images pass their actual aspect ratio"), MaterialAspectRatio, 3.0f);
	Pulse->UpdateAspectRatio(FVector2D(300.0, 100.0));
	TestEqual(TEXT("An unchanged size preserves the cached ratio"), Pulse->CachedAspectRatio, 3.0f);
	Pulse->UpdateAspectRatio(FVector2D(100.0, 200.0));
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("AspectRatio")), MaterialAspectRatio);
	TestEqual(TEXT("Resizing to portrait updates the ratio"), MaterialAspectRatio, 0.5f);
	for (const FVector2D& InvalidSize : {FVector2D(-1.0, 100.0), FVector2D(100.0, 0.0),
		FVector2D(std::numeric_limits<double>::quiet_NaN(), 100.0),
		FVector2D(100.0, std::numeric_limits<double>::infinity())})
	{
		Pulse->UpdateAspectRatio(InvalidSize);
		TestEqual(TEXT("Invalid geometry preserves the last valid ratio"), Pulse->CachedAspectRatio, 0.5f);
	}
	OtherPulse->UpdateAspectRatio(FVector2D(200.0, 100.0));
	float OtherAspectRatio = -1.0f;
	OtherPulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("AspectRatio")), OtherAspectRatio);
	TestEqual(TEXT("Resizing one HUD never changes the other MID"), OtherAspectRatio, 2.0f);
	Timer->UpdateHackTimerColor(19.5f, 60.0f);
	FLinearColor MaterialPulseColor;
	Pulse->DynamicMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("PulseColor")), MaterialPulseColor);
	TestTrue(TEXT("Frame interpolation reaches the private MID"),
		MaterialPulseColor.Equals(FMath::Lerp(Timer->HackTimerPulseNormalColor, Timer->HackTimerPulseWarningColor, 0.5f)));
	float OtherAmount = -1.0f;
	OtherPulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PulseAmount")), OtherAmount);
	TestEqual(TEXT("Updating one HUD preserves the other HUD's amount"), OtherAmount, 1.0f);
	Pulse->bPlaying = true;
	Pulse->LastGameTime = Pulse->GetGameTime();
	Fixture.AdvanceTime(0.25f);
	Timer->UpdateHackTimerPulse();
	Pulse->UpdatePlayback();
	TestTrue(TEXT("Per-frame pulse updates preserve the independent normal color"),
		Pulse->PulseColor.Equals(Timer->HackTimerPulseNormalColor));
	TestTrue(TEXT("Slow pulse accumulates fractional cycles"), FMath::IsNearlyEqual(Pulse->Phase, 0.125f));
	const float PausedPhase = Pulse->Phase;
	Timer->UpdateHackTimerPulse();
	Pulse->UpdatePlayback();
	TestEqual(TEXT("A stopped game clock preserves the phase"), Pulse->Phase, PausedPhase);
	Timer->HackTimerPulseSlowPeriod = 0.6f;
	Timer->HackTimerPulseFastPeriod = 0.6f;
	Fixture.AdvanceTime(0.25f);
	Timer->UpdateHackTimerPulse();
	Pulse->UpdatePlayback();
	TestTrue(TEXT("Changing the period continues the previous phase"),
		FMath::IsNearlyEqual(Pulse->Phase, PausedPhase + 0.25f / 0.6f));
	for (const auto& Invalid : {TPair<float, float>(0.5f, 2.0f), {2.0f, 0.0f},
		{std::numeric_limits<float>::infinity(), 0.6f}, {2.0f, std::numeric_limits<float>::quiet_NaN()}})
	{
		Timer->HackTimerPulseSlowPeriod = Invalid.Key;
		Timer->HackTimerPulseFastPeriod = Invalid.Value;
		TestTrue(TEXT("Invalid periods use defaults"), FMath::IsNearlyEqual(Timer->GetHackTimerPulsePeriod(0.5f), 1.3f));
	}
	Timer->HackTimerPulseSlowPeriod = 2.0f;
	Timer->HackTimerPulseFastPeriod = 0.6f;

	Timer->ReleaseHackTimer();
	TestFalse(TEXT("Unbinding disables pulse work"), Pulse->bPlaying);
	TestEqual(TEXT("Unbinding resets the phase"), Pulse->Phase, 0.0f);
	TestTrue(TEXT("Unbinding restores the normal color"), Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor()
		.Equals(Timer->HackTimerNormalColor));
	TestTrue(TEXT("Unbinding restores the independent frame color"), Pulse->PulseColor.Equals(Timer->HackTimerPulseNormalColor));
	Fixture.AdvanceTime(10.0f);
	Hud->InitializeHackTimer();
	TestEqual(TEXT("Late binding resumes current server time"), Timer->HackEffectTime.Seconds, 49);
	APartnerPlayerController* Controller = Fixture.World->SpawnActor<APartnerPlayerController>();
	if (!TestNotNull(TEXT("HUD owner controller spawns"), Controller))
	{
		return false;
	}
	Controller->EnemyHudInstance = Hud;
	Fixture.AdvanceTime(29.0f);
	Timer->UpdateHackTimerPulse();
	Pulse->UpdatePlayback();
	float FinalAmount = -1.0f;
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PulseAmount")), FinalAmount);
	// 소유 Controller 통보 -> Partner 복귀 -> Enemy 종료 복제 순서도 마지막 숫자를 유지한다.
	Controller->ClientPrepareExpiredEnemyHud_Implementation(FirstEnemy);
	const float FinalPhase = Pulse->Phase;
	Timer->UpdateHackTimerPulse();
	Pulse->UpdatePlayback();
	TestEqual(TEXT("Expiry freezes the last background phase"), Pulse->Phase, FinalPhase);
	TestFalse(TEXT("Expiry stops pulse work"), Pulse->bPlaying);
	float ExpiredAmount = -1.0f;
	Pulse->DynamicMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PulseAmount")), ExpiredAmount);
	TestEqual(TEXT("Expiry preserves amplitude for the final brightness"), ExpiredAmount, FinalAmount);
	TestTrue(TEXT("Terminal zero uses the warning color"), Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor()
		.Equals(Timer->HackTimerWarningColor));
	TestTrue(TEXT("Expiry freezes the frame with its own warning color"), Pulse->PulseColor.Equals(Timer->HackTimerPulseWarningColor));
	Pulse->DynamicMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("PulseColor")), MaterialPulseColor);
	TestTrue(TEXT("Expiry forwards the frame warning color to its MID"), MaterialPulseColor.Equals(Timer->HackTimerPulseWarningColor));
	Controller->RequestPopEnemyHud();
	TestTrue(TEXT("Partner return defers only the expiring HUD removal"), Controller->bEnemyHudPopPending);
	TestTrue(TEXT("Expiring HUD remains until its final display"), Controller->EnemyHudInstance == Hud);
	Timer->RefreshHackTimer();
	Timer->HandleHackEffectStarted(FirstEnemy, 60.0f);
	TestEqual(TEXT("Late active state cannot overwrite terminal zero"), Timer->HackEffectTime.Seconds, 0);
	FirstEnemy->EndHackEffect(EEnemyHackEffectEndReason::Expired);
	TestTrue(TEXT("Duplicate expiry retains the hide callback"), Fixture.World->GetTimerManager().TimerExists(Timer->HackTimerHideHandle));
	TestEqual(TEXT("Natural expiry shows zero"), Timer->HackEffectTime.Seconds, 0);
	TestTrue(TEXT("Zero remains visible in the terminal frame"), Timer->GetVisibility() == ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("The terminal zero retains its frame"), Timer->GetVisibility() == ESlateVisibility::HitTestInvisible);
	Fixture.World->GetTimerManager().ClearTimer(Timer->HackTimerHideHandle);
	Timer->HideExpiredHackTimer();
	TestTrue(TEXT("A same-frame callback cannot hide zero"), Timer->bHackTimerShowingExpiry);
	// Slate 객체가 캐시되어 있어도 화면에 붙지 않은 위젯은 Paint를 무한히 기다리지 않는다.
	const TSharedRef<SWidget> TimerSlate = Timer->TakeWidget();
	Fixture.AdvanceTime(0.1f);
	TestFalse(TEXT("An unmounted timer finishes expiry without waiting for Paint"), Timer->bHackTimerShowingExpiry);
	TestTrue(TEXT("Expiry hides the complete timer module"), Timer->GetVisibility() == ESlateVisibility::Collapsed);
	TestNull(TEXT("Final display removes the old Enemy HUD"), Controller->EnemyHudInstance.Get());
	TestFalse(TEXT("Final display clears the pending Pop state"), Controller->bEnemyHudPopPending);

	// 다음 대상에 연결한 뒤 이전 대상의 종료/숨김 콜백을 재현한다.
	NextEnemy->BeginHackEffect();
	Hud->InitializeForEnemy(NextEnemy);
	TestEqual(TEXT("New target begins at a fresh phase"), Pulse->Phase, 0.0f);
	TestTrue(TEXT("New target immediately uses its current color"), Timer->HackEffectSecondsText->GetColorAndOpacity().GetSpecifiedColor()
		.Equals(Timer->HackTimerNormalColor));
	TestTrue(TEXT("New target restores the frame's own normal color"), Pulse->PulseColor.Equals(Timer->HackTimerPulseNormalColor));
	Timer->HandleHackEffectEnded(FirstEnemy, EEnemyHackEffectEndReason::Expired);
	Timer->HideExpiredHackTimer();
	TestEqual(TEXT("Old callbacks preserve the new target's minute"), Timer->HackEffectTime.Minutes, 1);
	TestTrue(TEXT("Old callbacks preserve the new timer's visibility"), Timer->GetVisibility() == ESlateVisibility::HitTestInvisible);
	NextEnemy->EndHackEffect(EEnemyHackEffectEndReason::ManualRelease);
	TestTrue(TEXT("Manual release hides the timer immediately"), Timer->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Manual release also hides the frame"), Timer->GetVisibility() == ESlateVisibility::Collapsed);
	TestFalse(TEXT("Manual release stops UI refresh"), Fixture.World->GetTimerManager().TimerExists(Timer->HackTimerRefreshHandle));
	TestFalse(TEXT("Manual release unsubscribes the HUD"), Timer->bHackTimerBound);
	Controller->EnemyHudInstance = Hud;
	Controller->RequestPopEnemyHud();
	TestNull(TEXT("Manual release removes the HUD immediately"), Controller->EnemyHudInstance.Get());
	NextEnemy->ResetHackEffectForPool();
	NextEnemy->BeginHackEffect();
	Hud->InitializeHackTimer();
	Hud->ReleaseDamageFeedback();
	TestFalse(TEXT("The existing HUD Pop path also unsubscribes time"), Timer->bHackTimerBound);
	TestFalse(TEXT("The existing HUD Pop path clears refresh"), Fixture.World->GetTimerManager().TimerExists(Timer->HackTimerRefreshHandle));
	Timer->BindHackTimer();
	NextEnemy->Destroy();
	TestFalse(TEXT("Target loss stops refresh even outside BeginPlay"), Hud->GetOwningEnemy() && IsValid(Hud->GetOwningEnemy()));
	Timer->RefreshHackTimer();
	TestTrue(TEXT("Target loss hides stale numbers"), Timer->GetVisibility() == ESlateVisibility::Collapsed);
	Hud->InitializeForEnemy(nullptr);
	TestNull(TEXT("Blueprint-style null binding releases the target"), Hud->GetOwningEnemy());
	TestFalse(TEXT("Null binding has no live timer subscription"), Timer->bHackTimerBound);
	// 모듈 누락은 종료 표시 대기를 만들지 않는다.
	Hud->HackEffectTimerWidget = nullptr;
	Controller->EnemyHudInstance = Hud;
	Controller->RequestPopEnemyHud();
	TestNull(TEXT("A HUD without a timer returns immediately"), Controller->EnemyHudInstance.Get());
	Hud->HackEffectTimerWidget = Timer;

	AEnemyBase* LastEnemy = Fixture.SpawnEnemy();
	if (!TestNotNull(TEXT("Lifecycle target spawns"), LastEnemy))
	{
		return false;
	}
	Timer->HackEffectPulseWidget = nullptr;
	Hud->InitializeForEnemy(LastEnemy);
	LastEnemy->BeginHackEffect();
	Timer->NativeConstruct();
	TestEqual(TEXT("Construct after binding preserves the current minute"), Timer->HackEffectTime.Minutes, 1);
	TestTrue(TEXT("Missing breathing child still shows the timer"), Timer->IsVisible());
	Timer->HackTimerTextPulseSpeedCurve.GetRichCurve()->Reset();
	Timer->HackTimerTextPulseSpeedCurve.GetRichCurve()->AddKey(0.0f, 1.0f);
	Timer->HackTimerPulseFastPeriod = 0.5f;
	Timer->TextColorPhase = 0.0f;
	Timer->LastTextColorTime = Fixture.World->GetTimeSeconds();
	Fixture.AdvanceTime(0.125f);
	Timer->UpdateHackTimerPulse();
	TestTrue(TEXT("The speed curve independently selects the fast period"), FMath::IsNearlyEqual(Timer->TextColorPhase, 0.25f, KINDA_SMALL_NUMBER));
	Timer->HackTimerTextPulseSpeedCurve = DefaultSpeedCurve;
	Timer->HackTimerPulseFastPeriod = 0.6f;
	Fixture.AdvanceTime(40.0f);
	Timer->UpdateHackTimerPulse();
	const float ColorPhase = Timer->TextColorPhase;
	Fixture.AdvanceTime(0.1f);
	Timer->UpdateHackTimerPulse();
	TestFalse(TEXT("Seconds color animates without a breathing child"), FMath::IsNearlyEqual(Timer->TextColorPhase, ColorPhase));
	const float ResumedColorPhase = Timer->TextColorPhase;
	Timer->UpdateHackTimerPulse();
	TestEqual(TEXT("A paused game clock also freezes the seconds color"), Timer->TextColorPhase, ResumedColorPhase);

	int32 FinishedCount = 0;
	Timer->OnHackTimerExpiryDisplayFinished.BindLambda([&FinishedCount]() { ++FinishedCount; });
	Timer->ShowHackTimerExpiry(LastEnemy);
	TestTrue(TEXT("Expiry fixes minutes at warning color"), Timer->HackEffectMinutesText->GetColorAndOpacity().GetSpecifiedColor().Equals(Timer->HackTimerWarningColor));
	TestTrue(TEXT("Expiry fixes colon at warning color"), Timer->Colon->GetColorAndOpacity().GetSpecifiedColor().Equals(Timer->HackTimerWarningColor));
	Timer->SetVisibility(ESlateVisibility::Hidden);
	Fixture.AdvanceTime(0.1f);
	Timer->FinishExpiryDisplay();
	TestEqual(TEXT("Hidden expiry completes exactly once"), FinishedCount, 1);
	TestTrue(TEXT("Hidden expiry collapses the whole module"), Timer->GetVisibility() == ESlateVisibility::Collapsed);

	Hud->InitializeForEnemy(LastEnemy);
	Timer->OnHackTimerExpiryDisplayFinished.BindLambda([&FinishedCount]() { ++FinishedCount; });
	Timer->ShowHackTimerExpiry(LastEnemy);
	Timer->NativeDestruct();
	Timer->NativeDestruct();
	TestEqual(TEXT("Destruction completes pending expiry exactly once"), FinishedCount, 2);
	Hud->InitializeForEnemy(LastEnemy);
	Controller->EnemyHudInstance = Hud;
	Hud->ShowHackTimerExpiry(LastEnemy);
	Controller->RequestPopEnemyHud();
	TestTrue(TEXT("Parent destruction starts with a pending Pop"), Controller->bEnemyHudPopPending);
	Hud->NativeDestruct();
	TestNull(TEXT("External parent destruction completes the pending Pop"), Controller->EnemyHudInstance.Get());
	TestFalse(TEXT("External parent destruction clears pending state"), Controller->bEnemyHudPopPending);
	Hud->InitializeForEnemy(nullptr);
	{
		// 공통 위젯은 해킹 HUD나 별도의 Graph 없이도 Image와 MID를 준비해야 한다.
		UBreathingWidget* Standalone = NewObject<UBreathingWidget>(Fixture.World);
		Standalone->BreathingMaterial = PulseMaterial;
		const TSharedRef<SWidget> StandaloneSlate = Standalone->TakeWidget();
		TestNotNull(TEXT("An empty common widget builds its own image"), Standalone->BreathingImage.Get());
		TestNotNull(TEXT("A standalone common widget prepares its configured material"), Standalone->DynamicMaterial.Get());
		TestTrue(TEXT("A standalone common widget uses WBP autoplay defaults"), Standalone->bPlaying);
	}
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
		UHackEffectTimerWidget* Timer = Hud->HackEffectTimerWidget;
		if (TestNotNull(FString::Printf(TEXT("Common timer child binds: %s"), Path), Timer))
		{
			TestNotNull(TEXT("Minutes binds inside timer"), Timer->HackEffectMinutesText.Get());
			TestNotNull(TEXT("Seconds binds inside timer"), Timer->HackEffectSecondsText.Get());
			TestNotNull(TEXT("Colon binds inside timer"), Timer->Colon.Get());
			TestNotNull(TEXT("Frame binds inside timer"), Timer->TimerFrame.Get());
			TestNotNull(TEXT("Breathing widget binds inside timer"), Timer->HackEffectPulseWidget.Get());
			// 범용 숨쉬기 배경을 프레임 뒤에 겹친다. 배경의 크기와 위치는 WBP에서 독립 조절한다.
			if (UBreathingWidget* Pulse = Timer->HackEffectPulseWidget)
			{
				const TSharedRef<SWidget> PulseSlate = Pulse->TakeWidget();
				Pulse->ApplyBreathingSettings();
				UImage* PulseImage = Cast<UImage>(Pulse->GetWidgetFromName(TEXT("BreathingImage")));
				if (TestNotNull(TEXT("Common WBP has its real breathing image"), PulseImage))
				{
					TestTrue(TEXT("Frame breathing image is visible"), PulseImage->IsVisible());
					TestNotNull(TEXT("Frame image receives a dynamic material"),
						Cast<UMaterialInstanceDynamic>(PulseImage->GetBrush().GetResourceObject()));
				}
				UMaterialInterface* Material = Pulse->BreathingMaterial;
				if (TestNotNull(TEXT("Frame breathing material is assigned"), Material))
				{
					TestEqual(TEXT("Timer uses the reusable breathing material"), Material->GetPathName(),
						FString(TEXT("/Game/Material/Widget/MI_UI_Breathing_RoundedRectangle.MI_UI_Breathing_RoundedRectangle")));
					TestEqual(TEXT("Timer background uses the ellipse shape"), Pulse->Shape, EBreathingShape::Ellipse);
				}
				UCanvasPanelSlot* PulseSlot = Cast<UCanvasPanelSlot>(Pulse->Slot);
				UCanvasPanelSlot* FrameSlot = Timer->TimerFrame ? Cast<UCanvasPanelSlot>(Timer->TimerFrame->Slot) : nullptr;
				UCanvasPanelSlot* SecondsSlot = Timer->HackEffectSecondsText ? Cast<UCanvasPanelSlot>(Timer->HackEffectSecondsText->Slot) : nullptr;
				if (TestNotNull(TEXT("Frame glow has a canvas slot"), PulseSlot)
					&& TestNotNull(TEXT("Frame has a canvas slot"), FrameSlot)
					&& TestNotNull(TEXT("Seconds have a canvas slot"), SecondsSlot))
				{
					TestTrue(TEXT("Breathing background has a positive size"), PulseSlot->GetSize().X > 0.0f && PulseSlot->GetSize().Y > 0.0f);
					TestTrue(TEXT("Breathing background is behind frame and numbers"), PulseSlot->GetZOrder() < FrameSlot->GetZOrder()
						&& PulseSlot->GetZOrder() < SecondsSlot->GetZOrder());
				}
			}
			for (const FName LegacyName : {FName(TEXT("TimerFrame")), FName(TEXT("HackEffectMinutesText")),
				FName(TEXT("Colon")), FName(TEXT("HackEffectSecondsText")), FName(TEXT("HackEffectPulseImage"))})
			{
				TestNull(TEXT("Parent HUD has no duplicate timer elements"), Hud->GetWidgetFromName(LegacyName));
			}
			UCanvasPanelSlot* TimerSlot = Cast<UCanvasPanelSlot>(Timer->Slot);
			if (TestNotNull(TEXT("Timer remains on the HUD canvas"), TimerSlot))
			{
				TestTrue(TEXT("Timer keeps the original frame size"), TimerSlot->GetSize().Equals(FVector2D(316.0f, 122.0f)));
				TestTrue(TEXT("Timer keeps the original position"), TimerSlot->GetPosition().Equals(FVector2D(0.0f, 56.0f)));
				TestTrue(TEXT("Timer keeps centered alignment"), TimerSlot->GetAlignment().Equals(FVector2D(0.5f, 0.0f)));
				TestFalse(TEXT("Timer uses explicit frame size"), TimerSlot->GetAutoSize());
			}
		}
		Hud->InitializeForEnemy(nullptr);
	}
	return true;
}

#endif
