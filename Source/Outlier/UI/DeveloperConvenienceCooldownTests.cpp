#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"

namespace DeveloperConvenienceCooldownTests
{
struct FFixture
{
	UWorld* World = nullptr;
	AShooterCharacter* Shooter = nullptr;
	APartnerCharacter* Partner = nullptr;
	UOutlierAbilitySystemComponent* ShooterASC = nullptr;
	UOutlierAbilitySystemComponent* PartnerASC = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!Test.TestNotNull(TEXT("Cooldown fixture world"), World))
		{
			return false;
		}
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		World->InitializeActorsForPlay(FURL());
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("Concrete Shooter class"), ShooterClass))
		{
			return false;
		}
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
		Partner = World->SpawnActor<APartnerCharacter>();
		if (!Shooter || !Partner)
		{
			Test.AddError(TEXT("Cooldown fixture actors must spawn"));
			return false;
		}
		ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
		PartnerASC = Partner->GetOutlierAbilitySystemComponent();
		ShooterASC->InitializeForPawn(Shooter);
		PartnerASC->InitializeForPawn(Partner);
		FOutlierPartnerAbilityConfig PartnerConfig;
		PartnerConfig.EMPCooldown = 10.0f;
		PartnerConfig.ShieldCooldown = 10.0f;
		PartnerConfig.ScanCooldown = 10.0f;
		PartnerConfig.HackCooldown = 10.0f;
		PartnerConfig.HackCancelCooldownRefundRate = 0.5f;
		FOutlierShooterSuitConfig ShooterConfig;
		ShooterConfig.QuantumLeap.CooldownSeconds = 10.0f;
		ShooterConfig.BulletReflection.CooldownSeconds = 10.0f;
		ShooterConfig.WeaponOvercharge.CooldownSeconds = 10.0f;
		ShooterConfig.Stealth.CooldownSeconds = 10.0f;
		const bool bPartnerConfigured = PartnerASC->IsPartnerAbilitiesConfigured()
			? PartnerASC->UpdatePartnerAbilityConfig(PartnerConfig) : PartnerASC->ConfigurePartnerAbilities(PartnerConfig);
		const bool bShooterConfigured = ShooterASC->IsShooterSuitConfigured()
			? ShooterASC->UpdateShooterSuitConfig(ShooterConfig) : ShooterASC->ConfigureShooterSuitAbilities(ShooterConfig);
		return Test.TestTrue(TEXT("Partner config"), bPartnerConfigured)
			&& Test.TestTrue(TEXT("Shooter config"), bShooterConfigured);
	}

	void Tick(float Duration)
	{
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(Duration / 0.01f));
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Duration / Steps);
		}
	}

	~FFixture()
	{
		if (World)
		{
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	}
};

bool Near(float Actual, float Expected)
{
	return FMath::IsNearlyEqual(Actual, Expected, 0.04f);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperCooldownRestorationTest,
	"Outlier.UI.DeveloperConvenience.FastCooldown.Restoration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperCooldownRestorationTest::RunTest(const FString& Parameters)
{
	using namespace DeveloperConvenienceCooldownTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	auto* ASC = Fixture.PartnerASC;
	const auto EMP = OutlierGameplayTags::Cooldown::Partner::EMP();
	TestTrue(TEXT("Start normal EMP"), ASC->CommitPartnerCooldown(EMP));
	ASC->SetDeveloperConvenienceFastCooldown(true);
	TestTrue(TEXT("Existing cooldown shortened"), Near(ASC->GetPartnerCooldownRemaining(EMP), 1.0f));
	Fixture.Tick(0.5f);
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestTrue(TEXT("Restore original start"), Near(ASC->GetPartnerCooldownRemaining(EMP), 9.5f));
	ASC->SetDeveloperConvenienceFastCooldown(true);
	Fixture.Tick(0.2f);
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestTrue(TEXT("Repeated toggles preserve elapsed time"), Near(ASC->GetPartnerCooldownRemaining(EMP), 9.3f));
	Fixture.Tick(9.0f);
	ASC->SetDeveloperConvenienceFastCooldown(true);
	TestTrue(TEXT("Subsecond remaining is not lengthened"), Near(ASC->GetPartnerCooldownRemaining(EMP), 0.3f));
	Fixture.Tick(0.4f);
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestFalse(TEXT("Completed cooldown never restored"), ASC->IsPartnerCooldownActive(EMP));
	ASC->SetDeveloperConvenienceFastCooldown(true);
	TestTrue(TEXT("Start new fast EMP"), ASC->CommitPartnerCooldown(EMP));
	TestTrue(TEXT("New cooldown is one second"), Near(ASC->GetPartnerCooldownRemaining(EMP), 1.0f));
	Fixture.Tick(1.1f);
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestFalse(TEXT("Fast completion never restored"), ASC->IsPartnerCooldownActive(EMP));
	for (const auto Tag : { OutlierGameplayTags::Cooldown::Partner::Shield(), OutlierGameplayTags::Cooldown::Partner::Scan() })
	{
		TestTrue(TEXT("Other Partner cooldown starts"), ASC->CommitPartnerCooldown(Tag));
		ASC->SetDeveloperConvenienceFastCooldown(true);
		TestTrue(TEXT("Other Partner cooldown shortened"), Near(ASC->GetPartnerCooldownRemaining(Tag), 1.0f));
		ASC->SetDeveloperConvenienceFastCooldown(false);
		TestTrue(TEXT("Other Partner cooldown restored"), Near(ASC->GetPartnerCooldownRemaining(Tag), 10.0f));
	}
	auto* ShooterASC = Fixture.ShooterASC;
	TestTrue(TEXT("Stealth cooldown starts"), ShooterASC->CommitShooterStealthCooldown());
	TestTrue(TEXT("Reflection cooldown starts"), ShooterASC->CommitShooterBulletReflectionCooldown());
	TestTrue(TEXT("Overcharge cooldown starts"), ShooterASC->CommitShooterWeaponOverchargeCooldown());
	ShooterASC->SetDeveloperConvenienceFastCooldown(true);
	Fixture.Tick(0.5f);
	ShooterASC->SetDeveloperConvenienceFastCooldown(false);
	TestTrue(TEXT("Stealth uses original cooldown start"), Near(ShooterASC->GetShooterStealthCooldownRemaining(), 9.5f));
	TestTrue(TEXT("Reflection restored"), Near(ShooterASC->GetShooterBulletReflectionCooldownRemaining(), 9.5f));
	TestTrue(TEXT("Overcharge restored"), Near(ShooterASC->GetShooterWeaponOverchargeCooldownRemaining(), 9.5f));
	ShooterASC->SetDeveloperConvenienceFastCooldown(true);
	ShooterASC->CommitShooterQuantumLeapRetryDelay();
	TestTrue(TEXT("Short Leap retry stays short"), Near(ShooterASC->GetTimedCooldownRemaining(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeapRetry(), Fixture.Shooter), 0.5f));
	const auto Reboot = ShooterASC->ApplyRebootStateToSelf(6.0f);
	const auto ActiveSkill = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterStealthGameplayEffect::StaticClass(), 6.0f, Fixture.Shooter);
	const auto Weapon = ShooterASC->CommitTimedCooldown(UOutlierWeaponReuseCooldownGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Weapon::Reuse(), 6.0f, Fixture.Shooter);
	for (const auto Handle : { Reboot, ActiveSkill, Weapon })
	{
		const auto* Effect = ShooterASC->GetActiveGameplayEffect(Handle);
		TestTrue(TEXT("Non-skill timers retain normal duration"), Effect && Near(Effect->GetTimeRemaining(Fixture.World->GetTimeSeconds()), 6.0f));
	}
	FOutlierPartnerAbilityConfig ZeroConfig = ASC->GetPartnerAbilityConfig();
	ZeroConfig.EMPCooldown = 0.0f;
	ASC->UpdatePartnerAbilityConfig(ZeroConfig);
	ASC->SetDeveloperConvenienceFastCooldown(true);
	ASC->CommitPartnerCooldown(EMP);
	TestTrue(TEXT("New regular cooldown uses one second even with zero baseline"), Near(ASC->GetPartnerCooldownRemaining(EMP), 1.0f));
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestFalse(TEXT("Zero baseline becomes ready immediately on OFF"), ASC->IsPartnerCooldownActive(EMP));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperCooldownChargesTest,
	"Outlier.UI.DeveloperConvenience.FastCooldown.SequentialCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperCooldownChargesTest::RunTest(const FString& Parameters)
{
	using namespace DeveloperConvenienceCooldownTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	auto* ShooterASC = Fixture.ShooterASC;
	auto* PartnerASC = Fixture.PartnerASC;
	ShooterASC->AddLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::DoubleCharge());
	PartnerASC->AddLooseGameplayTag(OutlierGameplayTags::Upgrade::Partner::Hacking::DoubleCharge());
	ShooterASC->RefreshShooterQuantumLeapCharges();
	PartnerASC->RefreshPartnerHackCharges();
	const auto LeapSerial = ShooterASC->GetShooterQuantumLeapChargeState().RecoverySerial;
	const auto HackSerial = PartnerASC->GetPartnerHackChargeState().RecoverySerial;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestTrue(TEXT("Consume Leap charge"), ShooterASC->CommitShooterQuantumLeapCooldown());
		TestTrue(TEXT("Consume Hack charge"), PartnerASC->CommitPartnerHackCharge());
	}
	ShooterASC->SetDeveloperConvenienceFastCooldown(true);
	PartnerASC->SetDeveloperConvenienceFastCooldown(true);
	TestEqual(TEXT("Retiming grants no Leap charge"), ShooterASC->GetShooterQuantumLeapChargeState().CurrentCharges, 0);
	TestEqual(TEXT("Retiming grants no Hack charge"), PartnerASC->GetPartnerHackChargeState().CurrentCharges, 0);
	Fixture.Tick(1.1f);
	TestEqual(TEXT("One Leap charge recovered"), ShooterASC->GetShooterQuantumLeapChargeState().CurrentCharges, 1);
	TestEqual(TEXT("One Hack charge recovered"), PartnerASC->GetPartnerHackChargeState().CurrentCharges, 1);
	Fixture.Tick(1.1f);
	ShooterASC->SetDeveloperConvenienceFastCooldown(false);
	PartnerASC->SetDeveloperConvenienceFastCooldown(false);
	Fixture.Tick(10.1f);
	TestEqual(TEXT("Both Leap charges ready"), ShooterASC->GetShooterQuantumLeapChargeState().CurrentCharges, 2);
	TestEqual(TEXT("Both Hack charges ready"), PartnerASC->GetPartnerHackChargeState().CurrentCharges, 2);
	TestEqual(TEXT("Leap recovered exactly twice"), ShooterASC->GetShooterQuantumLeapChargeState().RecoverySerial, LeapSerial + 2);
	TestEqual(TEXT("Hack recovered exactly twice"), PartnerASC->GetPartnerHackChargeState().RecoverySerial, HackSerial + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperCooldownPauseTest,
	"Outlier.UI.DeveloperConvenience.FastCooldown.PauseAndPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperCooldownPauseTest::RunTest(const FString& Parameters)
{
	using namespace DeveloperConvenienceCooldownTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	auto* ASC = Fixture.PartnerASC;
	const auto EMP = OutlierGameplayTags::Cooldown::Partner::EMP();
	ASC->CommitPartnerCooldown(EMP);
	ASC->CommitPartnerHackRetryDelay();
	ASC->SetDeveloperConvenienceFastCooldown(true);
	Fixture.Tick(0.5f);
	auto* Pauser = Fixture.World->SpawnActor<AOutlierPlayerState>();
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	const double PausedAt = Fixture.World->GetTimeSeconds();
	Fixture.Tick(2.0f);
	TestEqual(TEXT("Pause freezes game time"), Fixture.World->GetTimeSeconds(), PausedAt);
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestTrue(TEXT("Pause excluded from normal cooldown"), Near(ASC->GetPartnerCooldownRemaining(EMP), 9.5f));
	TestTrue(TEXT("Pause excluded from retry"), Near(ASC->GetPartnerHackRetryRemaining(), 4.5f));
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	ASC->SuspendPartnerSkillCooldownsForPossession();
	Fixture.Tick(3.0f);
	ASC->SetDeveloperConvenienceFastCooldown(true);
	TestTrue(TEXT("Frozen cooldown shortened"), Near(ASC->GetSuspendedPartnerCooldownRemaining(EMP), 1.0f));
	Fixture.Tick(2.0f);
	ASC->SetDeveloperConvenienceFastCooldown(false);
	ASC->ResumePartnerSkillCooldownsAfterPossession();
	TestTrue(TEXT("Possession excluded from cooldown"), Near(ASC->GetPartnerCooldownRemaining(EMP), 9.5f));
	TestTrue(TEXT("Possession excluded from retry"), Near(ASC->GetPartnerHackRetryRemaining(), 4.5f));
	Fixture.Tick(4.6f);
	TestFalse(TEXT("Retry expires normally"), ASC->GetPartnerHackChargeState().bRetryBlocked);
	FOutlierPartnerAbilityConfig Config = ASC->GetPartnerAbilityConfig();
	Config.HackCancelCooldownRefundRate = 0.97f;
	ASC->UpdatePartnerAbilityConfig(Config);
	ASC->SetDeveloperConvenienceFastCooldown(true);
	ASC->CommitPartnerHackRetryDelay();
	TestTrue(TEXT("Short retry stays short"), Near(ASC->GetPartnerHackRetryRemaining(), 0.3f));
	Fixture.Tick(0.4f);
	// 빙의 성공 시 소모만 하고 복귀 후 처음 충전을 시작하는 경로를 검증한다.
	TestTrue(TEXT("Consume deferred Hack"), ASC->CommitPartnerHackCharge(true));
	ASC->SuspendPartnerSkillCooldownsForPossession();
	Fixture.Tick(2.0f);
	ASC->ResumePartnerSkillCooldownsAfterPossession();
	TestTrue(TEXT("Fresh recharge on return uses fast option"), Near(ASC->GetPartnerHackRechargeRemaining(), 1.0f));
	ASC->SetDeveloperConvenienceFastCooldown(false);
	TestTrue(TEXT("Fresh recharge retains normal basis"), Near(ASC->GetPartnerHackRechargeRemaining(), 10.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceCooldownTest,
	"Outlier.UI.DeveloperConvenience.FastCooldown.SharedProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceCooldownTest::RunTest(const FString& Parameters)
{
	using namespace DeveloperConvenienceCooldownTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	if (!TestNotNull(TEXT("GameMode class"), ModeClass))
	{
		return false;
	}
	auto* Mode = Fixture.World->SpawnActor<AOutlierGameMode>(ModeClass);
	auto* ShooterPC = Fixture.World->SpawnActor<AFirstPersonPlayerController>();
	auto* PartnerPC = Fixture.World->SpawnActor<AFirstPersonPlayerController>();
	auto* ShooterState = Fixture.World->SpawnActor<AOutlierPlayerState>();
	auto* PartnerState = Fixture.World->SpawnActor<AOutlierPlayerState>();
	if (!Mode || !ShooterPC || !PartnerPC || !ShooterState || !PartnerState)
	{
		AddError(TEXT("Projection fixture actors must spawn"));
		return false;
	}
	ShooterState->SetOwner(ShooterPC);
	PartnerState->SetOwner(PartnerPC);
	ShooterPC->PlayerState = ShooterState;
	PartnerPC->PlayerState = PartnerState;
	ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
	PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
	for (auto* State : { ShooterState, PartnerState })
	{
		State->SetPairId(7);
		State->SetShooterCharacter(Fixture.Shooter);
		State->SetPartnerCharacter(Fixture.Partner);
	}
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	Mode->DeveloperConvenienceState.Options.bFastCooldown = true;
	Mode->PublishDeveloperConvenienceState();
	TestTrue(TEXT("Shooter snapshot shares option"), ShooterPC->GetDeveloperConvenienceState().Options.bFastCooldown);
	TestTrue(TEXT("Partner snapshot shares option"), PartnerPC->GetDeveloperConvenienceState().Options.bFastCooldown);
	Fixture.ShooterASC->CommitShooterStealthCooldown();
	Fixture.PartnerASC->CommitPartnerCooldown(OutlierGameplayTags::Cooldown::Partner::EMP());
	TestTrue(TEXT("Projected to Shooter"), Near(Fixture.ShooterASC->GetShooterStealthCooldownRemaining(), 1.0f));
	TestTrue(TEXT("Projected to original Partner"), Near(Fixture.PartnerASC->GetPartnerCooldownRemaining(OutlierGameplayTags::Cooldown::Partner::EMP()), 1.0f));
	auto* Replacement = Fixture.World->SpawnActor<APartnerCharacter>();
	if (!TestNotNull(TEXT("Replacement Partner"), Replacement))
	{
		return false;
	}
	auto* ReplacementASC = Replacement->GetOutlierAbilitySystemComponent();
	ReplacementASC->InitializeForPawn(Replacement);
	ReplacementASC->ConfigurePartnerAbilities(Fixture.PartnerASC->GetPartnerAbilityConfig());
	ShooterState->SetPartnerCharacter(Replacement);
	PartnerState->SetPartnerCharacter(Replacement);
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	ReplacementASC->CommitPartnerCooldown(OutlierGameplayTags::Cooldown::Partner::EMP());
	TestTrue(TEXT("Same-generation replacement receives option"), Near(ReplacementASC->GetPartnerCooldownRemaining(OutlierGameplayTags::Cooldown::Partner::EMP()), 1.0f));
	Mode->ResetDeveloperConvenienceState();
	TestTrue(TEXT("Reset restores current Partner cooldown"), Near(ReplacementASC->GetPartnerCooldownRemaining(OutlierGameplayTags::Cooldown::Partner::EMP()), 10.0f));
	return true;
}

#endif
