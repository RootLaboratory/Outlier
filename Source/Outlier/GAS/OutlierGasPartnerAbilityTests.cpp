#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "GAS/Abilities/Partner/OutlierPartnerGameplayAbilities.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/App.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
struct FPartnerAbilityTestWorld
{
	FWorldContext* Context = nullptr;
	UWorld* World = nullptr;
	APartnerCharacter* Partner = nullptr;
	APartnerPlayerController* Controller = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Partner ability test world is created"), World))
		{
			return false;
		}

		World->AddToRoot();
		Context->SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		Partner = World->SpawnActor<APartnerCharacter>();
		if (!Test.TestNotNull(TEXT("Native Partner is spawned"), Partner))
		{
			Shutdown();
			return false;
		}

		Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);
		// Partner ability activation reads the acquired suit through its paired Shooter PS.
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
		AOutlierPlayerState* ShooterState = World->SpawnActor<AOutlierPlayerState>();
		AOutlierPlayerState* PartnerState = World->SpawnActor<AOutlierPlayerState>();
		if (!Test.TestNotNull(TEXT("Paired Shooter spawns for the suit gate"), Shooter)
			|| !Test.TestNotNull(TEXT("Shooter PlayerState spawns"), ShooterState)
			|| !Test.TestNotNull(TEXT("Partner PlayerState spawns"), PartnerState))
		{
			Shutdown();
			return false;
		}
		Shooter->SetPlayerState(ShooterState);
		Partner->SetPlayerState(PartnerState);
		PartnerState->SetShooterCharacter(Shooter);
		ShooterState->SetAcquiredSuit(true);
		Controller = World->SpawnActor<APartnerPlayerController>();
		if (!Test.TestNotNull(TEXT("Native Partner controller is spawned"), Controller))
		{
			Shutdown();
			return false;
		}
		Controller->Possess(Partner);
		// The transient controller has no GameMode-created PlayerState of its own.
		Partner->SetPlayerState(PartnerState);
		if (!Test.TestTrue(TEXT("Controller possesses native Partner"), Controller->GetPawn() == Partner))
		{
			Shutdown();
			return false;
		}

		Partner->FindComponentByClass<UPartnerEMPComponent>()->RefreshCharacterRefsFromPlayerState();
		Partner->FindComponentByClass<UPartnerHackComponent>()->RefreshCharacterRefsFromPlayerState();
		Partner->FindComponentByClass<UPartnerSupportComponent>()->RefreshCharacterRefsFromPlayerState();
		return true;
	}

	void Shutdown()
	{
		if (!World)
		{
			return;
		}

		if (Controller)
		{
			Controller->UnPossess();
			Controller->Destroy(true);
		}
		if (Partner)
		{
			Partner->Destroy(true);
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		World = nullptr;
		Context = nullptr;
		Partner = nullptr;
		Controller = nullptr;
	}

	~FPartnerAbilityTestWorld()
	{
		Shutdown();
	}

	void Tick(float Duration)
	{
		// 타이머와 GameplayEffect 만료가 같은 프레임에 처리되도록 실제 월드 시간을 진행한다.
		for (float Elapsed = 0.0f; Elapsed < Duration; Elapsed += 0.02f)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.02f);
		}
	}
};

FOutlierPartnerAbilityConfig MakeTestConfig()
{
	FOutlierPartnerAbilityConfig Config;
	Config.EMPCooldown = 30.0f;
	Config.ShieldCooldown = 20.0f;
	Config.HackCooldown = 3.0f;
	Config.ScanCooldown = 10.0f;
	Config.ScanDuration = 3.0f;
	Config.ScanRange = 1000.0f;
	Config.HackEffectiveRange = 500.0f;
	Config.ShieldAmount = 100.0f;
	Config.MarkDuration = 3.0f;
	Config.StunDuration = 3.0f;
	return Config;
}

int32 CountGrantedAbilityClass(
	const UOutlierAbilitySystemComponent& AbilitySystem,
	TSubclassOf<UGameplayAbility> AbilityClass)
{
	int32 Count = 0;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
		{
			++Count;
		}
	}
	return Count;
}

bool IsAbilityWithTagActive(
	const UOutlierAbilitySystemComponent& AbilitySystem,
	const FGameplayTag& AbilityTag)
{
	for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		if (Spec.IsActive()
			&& Spec.Ability
			&& Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
		{
			return true;
		}
	}
	return false;
}

bool IsNearlyEqualDuration(float Actual, float Expected, float Tolerance = 0.05f)
{
	return FMath::IsNearlyEqual(Actual, Expected, Tolerance);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerAbilityGrantTest,
	"Outlier.GAS.Partner.Ability.Grants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerAbilityGrantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPartnerAbilityTestWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem =
		Fixture.Partner->GetOutlierAbilitySystemComponent();
	const FOutlierPartnerAbilityConfig Config = MakeTestConfig();

	TestTrue(TEXT("Partner abilities configure on authority"), AbilitySystem->ConfigurePartnerAbilities(Config));
	TestEqual(TEXT("Exactly four Partner abilities are granted"), AbilitySystem->GetGrantedPartnerAbilityCount(), 4);
	TestTrue(TEXT("Repeated configuration is idempotent"), AbilitySystem->ConfigurePartnerAbilities(Config));
	TestEqual(TEXT("Repeated configuration does not duplicate grants"), AbilitySystem->GetGrantedPartnerAbilityCount(), 4);
	TestEqual(TEXT("Exactly one native EMP ability is granted"), CountGrantedAbilityClass(*AbilitySystem, UOutlierPartnerEMPAbility::StaticClass()), 1);
	TestEqual(TEXT("Exactly one native Shield ability is granted"), CountGrantedAbilityClass(*AbilitySystem, UOutlierPartnerShieldAbility::StaticClass()), 1);
	TestEqual(TEXT("Exactly one native Hack ability is granted"), CountGrantedAbilityClass(*AbilitySystem, UOutlierPartnerHackAbility::StaticClass()), 1);
	TestEqual(TEXT("Exactly one native Scan ability is granted"), CountGrantedAbilityClass(*AbilitySystem, UOutlierPartnerScanAbility::StaticClass()), 1);

	const UOutlierPartnerEMPAbility* EMP = GetDefault<UOutlierPartnerEMPAbility>();
	const UOutlierPartnerShieldAbility* Shield = GetDefault<UOutlierPartnerShieldAbility>();
	const UOutlierPartnerHackAbility* Hack = GetDefault<UOutlierPartnerHackAbility>();
	const UOutlierPartnerScanAbility* Scan = GetDefault<UOutlierPartnerScanAbility>();
	TestTrue(TEXT("EMP carries its exact ability tag"), EMP->GetAssetTags().HasTagExact(OutlierGameplayTags::Ability::Partner::EMP()));
	TestTrue(TEXT("Shield carries its exact ability tag"), Shield->GetAssetTags().HasTagExact(OutlierGameplayTags::Ability::Partner::Shield()));
	TestTrue(TEXT("Hack carries its exact ability tag"), Hack->GetAssetTags().HasTagExact(OutlierGameplayTags::Ability::Partner::Hacking()));
	TestTrue(TEXT("Scan carries its exact ability tag"), Scan->GetAssetTags().HasTagExact(OutlierGameplayTags::Ability::Partner::Scan()));
	TestTrue(TEXT("EMP blocks the common Partner execution group"), EMP->BlocksPartnerAbilityExecution());
	TestTrue(TEXT("Shield blocks the common Partner execution group"), Shield->BlocksPartnerAbilityExecution());
	TestTrue(TEXT("Hack blocks the common Partner execution group"), Hack->BlocksPartnerAbilityExecution());
	TestTrue(TEXT("Scan blocks the common Partner execution group"), Scan->BlocksPartnerAbilityExecution());
	TestEqual(TEXT("EMP executes only on the server"), EMP->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);
	TestEqual(TEXT("Shield executes only on the server"), Shield->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);
	TestEqual(TEXT("Hack executes only on the server"), Hack->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);
	TestEqual(TEXT("Scan executes only on the server"), Scan->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerCooldownSuspensionTest,
	"Outlier.GAS.Partner.Ability.CooldownSuspension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerCooldownSuspensionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPartnerAbilityTestWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem =
		Fixture.Partner->GetOutlierAbilitySystemComponent();
	const FOutlierPartnerAbilityConfig Config = MakeTestConfig();
	TestTrue(TEXT("Partner abilities configure before cooldown use"), AbilitySystem->ConfigurePartnerAbilities(Config));

	const FGameplayTag EMPCooldown = OutlierGameplayTags::Cooldown::Partner::EMP();
	const FGameplayTag ShieldCooldown = OutlierGameplayTags::Cooldown::Partner::Shield();
	TestTrue(TEXT("EMP cooldown commits through a GameplayEffect"), AbilitySystem->CommitPartnerCooldown(EMPCooldown));
	TestTrue(TEXT("Shield cooldown commits through a GameplayEffect"), AbilitySystem->CommitPartnerCooldown(ShieldCooldown));
	TestTrue(TEXT("EMP cooldown duration comes from configuration"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(EMPCooldown), 30.0f));
	TestTrue(TEXT("Shield cooldown duration comes from configuration"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(ShieldCooldown), 20.0f));

	const FActiveGameplayEffectHandle DamageImmuneHandle = AbilitySystem->ApplyDamageImmuneStateToSelf();
	TestTrue(TEXT("A non-cooldown GameplayEffect is active before suspension"), DamageImmuneHandle.IsValid());
	TestTrue(TEXT("Non-cooldown granted tag is present before suspension"), AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));

	TestTrue(TEXT("Possession suspension snapshots active Partner cooldowns"), AbilitySystem->SuspendPartnerSkillCooldownsForPossession());
	TestFalse(TEXT("EMP cooldown GE is removed while suspended"), AbilitySystem->IsPartnerCooldownActive(EMPCooldown));
	TestFalse(TEXT("Shield cooldown GE is removed while suspended"), AbilitySystem->IsPartnerCooldownActive(ShieldCooldown));
	const float SuspendedEMPRemaining = AbilitySystem->GetSuspendedPartnerCooldownRemaining(EMPCooldown);
	const float SuspendedShieldRemaining = AbilitySystem->GetSuspendedPartnerCooldownRemaining(ShieldCooldown);
	TestTrue(TEXT("EMP suspended remaining time is retained"), IsNearlyEqualDuration(SuspendedEMPRemaining, 30.0f));
	TestTrue(TEXT("Shield suspended remaining time is retained"), IsNearlyEqualDuration(SuspendedShieldRemaining, 20.0f));
	TestTrue(TEXT("Suspension leaves unrelated GameplayEffects active"), AbilitySystem->GetActiveGameplayEffect(DamageImmuneHandle) != nullptr);
	TestTrue(TEXT("Suspension leaves unrelated granted tags active"), AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestFalse(TEXT("A second suspension cannot duplicate the snapshot"), AbilitySystem->SuspendPartnerSkillCooldownsForPossession());

	for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
	{
		++GFrameCounter;
		Fixture.World->Tick(LEVELTICK_All, 1.0f);
	}
	TestTrue(TEXT("EMP snapshot does not elapse during committed possession"), IsNearlyEqualDuration(AbilitySystem->GetSuspendedPartnerCooldownRemaining(EMPCooldown), SuspendedEMPRemaining));
	TestTrue(TEXT("Shield snapshot does not elapse during committed possession"), IsNearlyEqualDuration(AbilitySystem->GetSuspendedPartnerCooldownRemaining(ShieldCooldown), SuspendedShieldRemaining));

	TestTrue(TEXT("Partner cooldowns resume once"), AbilitySystem->ResumePartnerSkillCooldownsAfterPossession());
	TestTrue(TEXT("EMP cooldown is active after resume"), AbilitySystem->IsPartnerCooldownActive(EMPCooldown));
	TestTrue(TEXT("Shield cooldown is active after resume"), AbilitySystem->IsPartnerCooldownActive(ShieldCooldown));
	TestTrue(TEXT("EMP resumes from the frozen duration"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(EMPCooldown), SuspendedEMPRemaining));
	TestTrue(TEXT("Shield resumes from the frozen duration"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(ShieldCooldown), SuspendedShieldRemaining));
	TestFalse(TEXT("A second resume has no session to consume"), AbilitySystem->ResumePartnerSkillCooldownsAfterPossession());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerAbilityExecutionTest,
	"Outlier.GAS.Partner.Ability.Execution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerAbilityExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPartnerAbilityTestWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Fixture.Partner->GetOutlierAbilitySystemComponent();
	TestTrue(TEXT("Partner abilities configure for execution"), AbilitySystem->ConfigurePartnerAbilities(MakeTestConfig()));

	const FGameplayTag ScanAbility = OutlierGameplayTags::Ability::Partner::Scan();
	const FGameplayTag EMPAbility = OutlierGameplayTags::Ability::Partner::EMP();
	const FGameplayTag ScanCooldown = OutlierGameplayTags::Cooldown::Partner::Scan();
	TestTrue(TEXT("Scan starts through its granted native AbilitySpec"), AbilitySystem->TryActivatePartnerAbility(ScanAbility));
	TestTrue(TEXT("Scan ability stays active for its duration"), IsAbilityWithTagActive(*AbilitySystem, ScanAbility));
	TestTrue(TEXT("Scan commits cooldown only after authoritative start"), AbilitySystem->IsPartnerCooldownActive(ScanCooldown));
	TestFalse(TEXT("A second Partner ability is blocked while Scan executes"), AbilitySystem->TryActivatePartnerAbility(EMPAbility));

	AbilitySystem->CancelActivePartnerAbilities();
	TestFalse(TEXT("Cancelling the execution group ends Scan"), IsAbilityWithTagActive(*AbilitySystem, ScanAbility));

	const FGameplayTag HackAbility = OutlierGameplayTags::Ability::Partner::Hacking();
	const FGameplayTag HackCooldown = OutlierGameplayTags::Cooldown::Partner::Hacking();
	UPartnerHackComponent* HackComponent = Fixture.Partner->FindComponentByClass<UPartnerHackComponent>();
	TestTrue(TEXT("Hack starts without pre-committing cooldown"), AbilitySystem->TryActivatePartnerAbility(HackAbility));
	TestFalse(TEXT("Hack search has no cooldown before its result"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	// 실제 재입력으로 검색을 취소해야 결과 통지부터 재시도 제한까지 함께 검증된다.
	HackComponent->TryHack();
	TestFalse(TEXT("Cancelling search ends the interaction"), HackComponent->IsHackInteractionActive());
	TestFalse(TEXT("Cancelling search ends its hacking ability"), IsAbilityWithTagActive(*AbilitySystem, HackAbility));
	TestFalse(TEXT("Cancelled Hack does not start charge recovery"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestEqual(TEXT("Cancelled Hack preserves its charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestTrue(TEXT("Cancelled Hack starts its separate retry restriction"), AbilitySystem->GetPartnerHackChargeState().bRetryBlocked);
	TestTrue(TEXT("Cancellation exposes the remaining retry time for HUD"),
		IsNearlyEqualDuration(AbilitySystem->GetPartnerHackRetryRemaining(), MakeTestConfig().HackCooldown * 0.5f));
	TestFalse(TEXT("Cancellation retry blocks an otherwise available charge"), AbilitySystem->TryActivatePartnerAbility(HackAbility));
	// 클라이언트에 남아 있던 검색 입력이 직접 Component RPC를 보내도 GAS 제한을 우회하지 못한다.
	HackComponent->TryHack();
	TestFalse(TEXT("A direct search RPC cannot bypass the cancellation retry"), HackComponent->IsHackInteractionActive());
	TestFalse(TEXT("A rejected search RPC cannot leave a hacking ability active"), IsAbilityWithTagActive(*AbilitySystem, HackAbility));
	Fixture.Tick(MakeTestConfig().HackCooldown * 0.5f - 0.1f);
	TestTrue(TEXT("Cancellation restriction remains until half the configured cooldown"), AbilitySystem->GetPartnerHackChargeState().bRetryBlocked);
	Fixture.Tick(0.2f);
	TestFalse(TEXT("Cancellation retry restriction expires independently"), AbilitySystem->GetPartnerHackChargeState().bRetryBlocked);
	TestEqual(TEXT("Expired retry clears the HUD duration"), AbilitySystem->GetPartnerHackChargeState().RetryDuration, 0.0f);
	HackComponent->TryHack();
	TestTrue(TEXT("A direct search request routes through the ability after retry expires"), IsAbilityWithTagActive(*AbilitySystem, HackAbility));
	HackComponent->OnHackFinished.Broadcast(EHackResult::Fail, false);
	HackComponent->CancelForReboot();
	TestEqual(TEXT("Failed Hack consumes the single charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);
	HackComponent->OnHackFinished.Broadcast(EHackResult::Fail, false);
	TestEqual(TEXT("A repeated result does not consume another charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);
	HackComponent->TryHack();
	TestFalse(TEXT("A direct search RPC cannot bypass empty charges"), HackComponent->IsHackInteractionActive());
	TestTrue(TEXT("Failed Hack commits cooldown"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestTrue(
		TEXT("Failed Hack commits the full configured cooldown"),
		IsNearlyEqualDuration(
			AbilitySystem->GetPartnerCooldownRemaining(HackCooldown),
			MakeTestConfig().HackCooldown));
	AbilitySystem->RemoveActiveEffects(
		FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(HackCooldown)));
	TestTrue(TEXT("Hack can restart after the failure cooldown is cleared"), AbilitySystem->TryActivatePartnerAbility(HackAbility));
	HackComponent->OnHackFinished.Broadcast(EHackResult::Success, false);
	HackComponent->CancelForReboot();
	TestTrue(TEXT("Normal confirmed Hack success commits cooldown"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestEqual(TEXT("Normal confirmed Hack success consumes the single charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);

	const FGameplayTag EMPCooldown = OutlierGameplayTags::Cooldown::Partner::EMP();
	AEnemyBase* EMPEnemy = Fixture.World->SpawnActor<AEnemyBase>();
	if (!TestNotNull(TEXT("EMP target Enemy is spawned"), EMPEnemy))
	{
		return false;
	}
	EMPEnemy->SetActorLocation(Fixture.Partner->GetActorLocation() + FVector(100.0f, 0.0f, 0.0f));
	UPartnerEMPComponent* EMPComponent = Fixture.Partner->FindComponentByClass<UPartnerEMPComponent>();
	if (!TestNotNull(TEXT("Partner owns its EMP execution component"), EMPComponent))
	{
		EMPEnemy->Destroy(true);
		return false;
	}
	EMPComponent->bRequireLineOfSight = false;
	FOutlierPartnerAbilityConfig RuntimeConfig = AbilitySystem->GetPartnerAbilityConfig();
	RuntimeConfig.StunDuration = 0.05f;
	TestTrue(
		TEXT("EMP runtime values update through the Partner ASC config"),
		AbilitySystem->UpdatePartnerAbilityConfig(RuntimeConfig));
	TestTrue(TEXT("EMP starts through its granted native AbilitySpec"), AbilitySystem->TryActivatePartnerAbility(EMPAbility));
	TestTrue(TEXT("EMP commits cooldown when target selection starts"), AbilitySystem->IsPartnerCooldownActive(EMPCooldown));
	EMPComponent->ServerCompleteEMP(TArray<AActor*>{EMPEnemy});
	TestTrue(
		TEXT("Partner EMP grants Stunned through the target Enemy ASC"),
		EMPEnemy->GetOutlierAbilitySystemComponent()->HasMatchingGameplayTag(
			OutlierGameplayTags::State::Stunned()));
	TestFalse(
		TEXT("Partner EMP does not mirror migrated Stunned state into EMPTags"),
		EMPEnemy->GetEMPableComponent()->HasEMPTag(OutlierGameplayTags::State::Stunned()));
	for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
	{
		++GFrameCounter;
		Fixture.World->Tick(ELevelTick::LEVELTICK_All, 0.05f);
	}
	TestFalse(
		TEXT("Partner EMP Stunned GameplayEffect expires at its configured duration"),
		EMPEnemy->GetOutlierAbilitySystemComponent()->HasMatchingGameplayTag(
			OutlierGameplayTags::State::Stunned()));
	const float EMPRemainingAfterStart = AbilitySystem->GetPartnerCooldownRemaining(EMPCooldown);
	TestFalse(TEXT("Repeated EMP activation cannot create a second ability instance"), AbilitySystem->TryActivatePartnerAbility(EMPAbility));
	TestTrue(TEXT("Repeated EMP activation does not refresh its cooldown"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(EMPCooldown), EMPRemainingAfterStart));
	AbilitySystem->CancelActivePartnerAbilities();
	EMPEnemy->Destroy(true);

	const FGameplayTag ShieldAbility = OutlierGameplayTags::Ability::Partner::Shield();
	const FGameplayTag ShieldCooldown = OutlierGameplayTags::Cooldown::Partner::Shield();
	AbilitySystem->TryActivatePartnerAbility(ShieldAbility);
	TestFalse(TEXT("Failed Shield application does not commit cooldown"), AbilitySystem->IsPartnerCooldownActive(ShieldCooldown));

	const FActiveGameplayEffectHandle RebootHandle = AbilitySystem->ApplyRebootStateToSelf(5.0f);
	TestTrue(TEXT("Reboot state applies before activation rejection"), RebootHandle.IsValid());
	TestFalse(TEXT("Reboot rejects fresh Partner ability activation"), AbilitySystem->TryActivatePartnerAbility(EMPAbility));
	TestTrue(TEXT("Reboot state removes cleanly"), AbilitySystem->RemoveActiveEffectFromSelf(RebootHandle));

	TestTrue(TEXT("Committed possession suspends active cooldowns"), AbilitySystem->SuspendPartnerSkillCooldownsForPossession());
	TestFalse(TEXT("Committed possession rejects fresh Partner ability activation"), AbilitySystem->TryActivatePartnerAbility(EMPAbility));
	AbilitySystem->DiscardSuspendedPartnerSkillCooldowns();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerPossessionCooldownSessionTest,
	"Outlier.GAS.Partner.Ability.PossessionSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerPossessionCooldownSessionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPartnerAbilityTestWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Fixture.Partner->GetOutlierAbilitySystemComponent();
	const FOutlierPartnerAbilityConfig Config = MakeTestConfig();
	TestTrue(TEXT("Partner abilities configure for possession session"), AbilitySystem->ConfigurePartnerAbilities(Config));
	const FGameplayTag EMPCooldown = OutlierGameplayTags::Cooldown::Partner::EMP();
	const FGameplayTag HackCooldown = OutlierGameplayTags::Cooldown::Partner::Hacking();

	TestFalse(TEXT("Pre-commit finalization has no session"), Fixture.Controller->FinalizeCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	TestFalse(TEXT("Pre-commit finalization does not start Hack cooldown"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	UPartnerHackComponent* HackComponent = Fixture.Partner->FindComponentByClass<UPartnerHackComponent>();
	TestTrue(TEXT("Possession Hack starts without pre-committing cooldown"), AbilitySystem->TryActivatePartnerAbility(OutlierGameplayTags::Ability::Partner::Hacking()));
	TestTrue(TEXT("An existing EMP cooldown is active before possession"), AbilitySystem->CommitPartnerCooldown(EMPCooldown, 17.0f));
	TestTrue(TEXT("Successful possession commit starts one cooldown session"), Fixture.Controller->BeginCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	HackComponent->OnHackFinished.Broadcast(EHackResult::Success, true);
	HackComponent->CancelForReboot();
	TestFalse(TEXT("Possession Hack has no cooldown while committed"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestFalse(TEXT("Duplicate possession commit cannot replace the snapshot"), Fixture.Controller->BeginCommittedPartnerAbilityCooldownSession(Fixture.Partner));

	const float FrozenEMPRemaining = AbilitySystem->GetSuspendedPartnerCooldownRemaining(EMPCooldown);
	for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
	{
		++GFrameCounter;
		Fixture.World->Tick(LEVELTICK_All, 1.0f);
	}
	TestTrue(TEXT("Possession keeps the EMP cooldown frozen"), IsNearlyEqualDuration(AbilitySystem->GetSuspendedPartnerCooldownRemaining(EMPCooldown), FrozenEMPRemaining));
	TestTrue(TEXT("Partner return consumes the committed session"), Fixture.Controller->FinalizeCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	TestTrue(TEXT("Partner return resumes EMP at the frozen value"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(EMPCooldown), FrozenEMPRemaining));
	TestTrue(TEXT("Partner return starts full current Hack cooldown"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(HackCooldown), Config.HackCooldown));
	TestFalse(TEXT("Duplicate terminal callback cannot finalize twice"), Fixture.Controller->FinalizeCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	TestTrue(TEXT("Duplicate terminal callback does not refresh Hack cooldown"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(HackCooldown), Config.HackCooldown));

	AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(HackCooldown)));
	TestTrue(TEXT("A second possession session can begin"), Fixture.Controller->BeginCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	Fixture.Controller->DiscardCommittedPartnerAbilityCooldownSession(Fixture.Partner);
	TestFalse(TEXT("Logout-style discard clears the suspended session"), AbilitySystem->ArePartnerSkillCooldownsSuspended());
	TestFalse(TEXT("Logout-style discard does not start Hack cooldown"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerHackChargesTest,
	"Outlier.GAS.Partner.Ability.HackCharges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerHackChargesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPartnerAbilityTestWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Fixture.Partner->GetOutlierAbilitySystemComponent();
	FOutlierPartnerAbilityConfig Config = MakeTestConfig();
	Config.HackCooldown = 0.8f;
	TestTrue(TEXT("Charge fixture configures the Partner abilities"), AbilitySystem->ConfigurePartnerAbilities(Config));
	const FGameplayTag HackCooldown = OutlierGameplayTags::Cooldown::Partner::Hacking();
	const FGameplayTag DoubleChargeTag = FGameplayTag::RequestGameplayTag(TEXT("Upgrade.Partner.Hacking.DoubleCharge"));
	const FGameplayTag HackAbility = OutlierGameplayTags::Ability::Partner::Hacking();
	UPartnerHackComponent* HackComponent = Fixture.Partner->FindComponentByClass<UPartnerHackComponent>();
	if (!TestNotNull(TEXT("Charge fixture owns its hack component"), HackComponent))
	{
		return false;
	}
	TestEqual(TEXT("Hacking initially has one charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestEqual(TEXT("Hacking initially has capacity one"), AbilitySystem->GetPartnerHackChargeState().MaxCharges, 1);
	AbilitySystem->AddLooseGameplayTag(DoubleChargeTag);
	AbilitySystem->RefreshPartnerHackCharges();
	TestEqual(TEXT("Double Charge increases maximum capacity to two"), AbilitySystem->GetPartnerHackChargeState().MaxCharges, 2);
	TestEqual(TEXT("Acquiring Double Charge while ready provides two uses"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 2);

	TestTrue(TEXT("The first charged hack activates"), AbilitySystem->TryActivatePartnerAbility(HackAbility));
	TestEqual(TEXT("Searching without a result does not consume a charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 2);
	HackComponent->OnHackFinished.Broadcast(EHackResult::Fail, false);
	HackComponent->CancelForReboot();
	TestEqual(TEXT("The first use leaves one charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	HackComponent->OnHackFinished.Broadcast(EHackResult::Fail, false);
	TestEqual(TEXT("A duplicate completed failure cannot consume the remaining charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestTrue(TEXT("A remaining charge is usable while recovery runs"), AbilitySystem->CanUsePartnerHackCharge());
	const float FirstRechargeStart = AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime;
	const uint32 RecoveryBeforeUse = AbilitySystem->GetPartnerHackChargeState().RecoverySerial;
	Fixture.Tick(0.2f);
	TestTrue(TEXT("The remaining charge activates through the ability gate during recharge"), AbilitySystem->TryActivatePartnerAbility(HackAbility));
	HackComponent->OnHackFinished.Broadcast(EHackResult::Success, false);
	HackComponent->CancelForReboot();
	TestEqual(TEXT("Two results exhaust both charges"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);
	TestEqual(TEXT("The second result preserves the first recharge clock"), AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime, FirstRechargeStart);
	TestFalse(TEXT("Zero charges prevent a further use"), AbilitySystem->CanUsePartnerHackCharge());
	TestFalse(TEXT("Zero charges cannot be consumed twice"), AbilitySystem->CommitPartnerHackCharge());
	Fixture.Tick(0.7f);
	TestEqual(TEXT("The first completed cycle recovers exactly one charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestEqual(TEXT("The first natural recovery increments its event serial once"), AbilitySystem->GetPartnerHackChargeState().RecoverySerial, RecoveryBeforeUse + 1);
	TestTrue(TEXT("The second charge starts its own sequential cycle"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	Fixture.Tick(0.9f);
	TestEqual(TEXT("The second cycle restores full capacity"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 2);
	TestEqual(TEXT("Two natural recoveries produce two events"), AbilitySystem->GetPartnerHackChargeState().RecoverySerial, RecoveryBeforeUse + 2);
	TestFalse(TEXT("Full capacity stops recharge"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));

	// 1회 충전 중 획득한 추가 횟수와 취소 제한은 진행 중 주기를 건드리지 않는다.
	AbilitySystem->RemoveLooseGameplayTag(DoubleChargeTag);
	AbilitySystem->RefreshPartnerHackCharges();
	TestEqual(TEXT("Removing the upgrade clamps current charges to one"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestTrue(TEXT("The basic charge starts recovery"), AbilitySystem->CommitPartnerHackCharge());
	Fixture.Tick(0.2f);
	const float RechargeBeforeUpgrade = AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime;
	const uint32 RecoveryBeforeUpgrade = AbilitySystem->GetPartnerHackChargeState().RecoverySerial;
	AbilitySystem->AddLooseGameplayTag(DoubleChargeTag);
	AbilitySystem->RefreshPartnerHackCharges();
	TestEqual(TEXT("Upgrading during recovery supplies only the new capacity"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestEqual(TEXT("Upgrading preserves the active recharge clock"), AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime, RechargeBeforeUpgrade);
	TestEqual(TEXT("Upgrade grants do not trigger recovery feedback"), AbilitySystem->GetPartnerHackChargeState().RecoverySerial, RecoveryBeforeUpgrade);
	TestTrue(TEXT("The same upgrade config can be projected again"), AbilitySystem->UpdatePartnerAbilityConfig(Config));
	AbilitySystem->RefreshPartnerHackCharges();
	TestEqual(TEXT("Repeated projection does not grant another use"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	// 실제 업그레이드처럼 새 태그 GE를 먼저 적용하고 이전 GE를 제거해 태그 소유권을 이어 준다.
	const auto ApplyDoubleChargeEffect = [AbilitySystem, DoubleChargeTag]()
	{
		UGameplayEffect* Effect = NewObject<UGameplayEffect>();
		Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
		FInheritedTagContainer Tags;
		Tags.AddTag(DoubleChargeTag);
		Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>().SetAndApplyTargetTagChanges(Tags);
		return AbilitySystem->ApplyGameplayEffectToSelf(Effect, 1.0f, AbilitySystem->MakeEffectContext());
	};
	const FActiveGameplayEffectHandle OriginalUpgrade = ApplyDoubleChargeEffect();
	if (!TestTrue(TEXT("The first upgrade tag effect applies"), OriginalUpgrade.IsValid()))
	{
		return false;
	}
	AbilitySystem->RemoveLooseGameplayTag(DoubleChargeTag);
	const FActiveGameplayEffectHandle ReplacementUpgrade = ApplyDoubleChargeEffect();
	if (!TestTrue(TEXT("The replacement upgrade tag effect applies"), ReplacementUpgrade.IsValid()))
	{
		return false;
	}
	AbilitySystem->RemoveActiveGameplayEffect(OriginalUpgrade);
	TestEqual(TEXT("Replacing the actual upgrade effect does not repeat its charge grant"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestEqual(TEXT("Replacing the actual upgrade effect preserves recharge timing"), AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime, RechargeBeforeUpgrade);
	TestTrue(TEXT("Cancellation starts a separate retry delay"), AbilitySystem->CommitPartnerHackRetryDelay());
	TestEqual(TEXT("Cancellation preserves the available charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestEqual(TEXT("Cancellation preserves the active recharge clock"), AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime, RechargeBeforeUpgrade);
	TestFalse(TEXT("Retry delay blocks the remaining charge"), AbilitySystem->CanUsePartnerHackCharge());
	Fixture.Tick(0.5f);
	TestFalse(TEXT("Cancellation retry expires independently of recovery"), AbilitySystem->GetPartnerHackChargeState().bRetryBlocked);
	TestTrue(TEXT("The remaining charge becomes usable after retry"), AbilitySystem->CanUsePartnerHackCharge());
	TestEqual(TEXT("Retry expiry does not prematurely recover a charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	Fixture.Tick(0.2f);
	TestEqual(TEXT("Original recovery still completes on schedule after cancellation"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 2);

	// 0초 설정은 재귀 충전이나 잔여 효과 없이 즉시 반환되어야 한다.
	Config.HackCooldown = 0.0f;
	TestTrue(TEXT("Zero hack cooldown remains a valid runtime configuration"), AbilitySystem->UpdatePartnerAbilityConfig(Config));
	const uint32 RecoveryBeforeZeroDuration = AbilitySystem->GetPartnerHackChargeState().RecoverySerial;
	TestTrue(TEXT("A successful use with zero duration still records consumption"), AbilitySystem->CommitPartnerHackCharge());
	TestEqual(TEXT("Zero-duration recovery immediately restores both charges"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 2);
	TestEqual(TEXT("Zero-duration recovery emits one recovery for the spent charge"), AbilitySystem->GetPartnerHackChargeState().RecoverySerial, RecoveryBeforeZeroDuration + 1);
	TestFalse(TEXT("Zero-duration recovery leaves no active recharge effect"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestEqual(TEXT("Zero-duration recovery clears recharge presentation"), AbilitySystem->GetPartnerHackChargeState().RechargeDuration, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerHackPossessionRechargeTest,
	"Outlier.GAS.Partner.Ability.HackPossessionRecharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerHackPossessionRechargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPartnerAbilityTestWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Fixture.Partner->GetOutlierAbilitySystemComponent();
	FOutlierPartnerAbilityConfig Config = MakeTestConfig();
	Config.HackCooldown = 1.0f;
	TestTrue(TEXT("Possession recharge fixture configures Partner abilities"), AbilitySystem->ConfigurePartnerAbilities(Config));
	const FGameplayTag HackCooldown = OutlierGameplayTags::Cooldown::Partner::Hacking();
	AbilitySystem->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Upgrade.Partner.Hacking.DoubleCharge")));
	AbilitySystem->RefreshPartnerHackCharges();
	TestTrue(TEXT("First hack starts an ordinary recharge"), AbilitySystem->CommitPartnerHackCharge());
	Fixture.Tick(0.2f);
	const float RemainingBeforePossession = AbilitySystem->GetPartnerCooldownRemaining(HackCooldown);
	const uint32 RecoveryBeforePossession = AbilitySystem->GetPartnerHackChargeState().RecoverySerial;
	TestTrue(TEXT("Possession suspends the already running recharge"), Fixture.Controller->BeginCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	// 실제 완료 순서와 같이 빙의가 먼저 확정된 뒤 성공 결과의 1회를 기록한다.
	TestTrue(TEXT("Possession result consumes its charge while recovery is deferred"), AbilitySystem->CommitPartnerHackCharge(true));
	TestEqual(TEXT("Possession result exhausts the second charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);
	TestFalse(TEXT("No recovery effect runs during possession"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestTrue(TEXT("Suspension retains the running recharge remainder"), IsNearlyEqualDuration(AbilitySystem->GetSuspendedPartnerCooldownRemaining(HackCooldown), RemainingBeforePossession));
	Fixture.Tick(1.2f);
	TestEqual(TEXT("Possession time cannot recover a charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);
	TestEqual(TEXT("Suspension emits no false recovery feedback"), AbilitySystem->GetPartnerHackChargeState().RecoverySerial, RecoveryBeforePossession);
	TestTrue(TEXT("Possession keeps the recharge remainder frozen"), IsNearlyEqualDuration(AbilitySystem->GetSuspendedPartnerCooldownRemaining(HackCooldown), RemainingBeforePossession));
	TestTrue(TEXT("Partner return resumes its committed session"), Fixture.Controller->FinalizeCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	TestTrue(TEXT("Return resumes the partial cycle rather than restarting it"), IsNearlyEqualDuration(AbilitySystem->GetPartnerCooldownRemaining(HackCooldown), RemainingBeforePossession));
	TestEqual(TEXT("Return does not consume another charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 0);
	TestFalse(TEXT("Duplicate return cannot replay the session"), Fixture.Controller->FinalizeCommittedPartnerAbilityCooldownSession(Fixture.Partner));
	Fixture.Tick(RemainingBeforePossession + 0.1f);
	TestEqual(TEXT("Resumed partial cycle returns exactly one charge"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 1);
	TestTrue(TEXT("Next missing charge uses a full configured cycle"), IsNearlyEqualDuration(AbilitySystem->GetPartnerHackChargeState().RechargeDuration, Config.HackCooldown));
	Fixture.Tick(1.1f);
	TestEqual(TEXT("The second cycle returns full capacity after possession"), AbilitySystem->GetPartnerHackChargeState().CurrentCharges, 2);
	TestEqual(TEXT("Only the two completed cycles produce recovery events"), AbilitySystem->GetPartnerHackChargeState().RecoverySerial, RecoveryBeforePossession + 2);

	// 성공 직후 대상이 사라져 전환이 취소되면, 지연된 충전을 시작해 영구 사용 불가를 방지한다.
	Fixture.Controller->PendingEnemyPossessionSource = Fixture.Partner;
	TestTrue(TEXT("Pending transition defers possession recharge"), Fixture.Controller->ShouldDeferPartnerHackRecharge(Fixture.Partner));
	TestTrue(TEXT("Pending possession consumes one charge without starting a new cycle"), AbilitySystem->CommitPartnerHackCharge(true));
	TestFalse(TEXT("Pending transition keeps its recharge deferred"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	Fixture.Controller->CancelPendingEnemyPossessionTransition();
	TestFalse(TEXT("Cancelled transition no longer defers recharge"), Fixture.Controller->ShouldDeferPartnerHackRecharge(Fixture.Partner));
	TestTrue(TEXT("Cancelled transition releases the remaining charge"), AbilitySystem->CanUsePartnerHackCharge());
	TestTrue(TEXT("Cancelled transition starts recovery for its spent charge"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	const float RechargeAfterCancel = AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime;
	Fixture.Controller->CancelPendingEnemyPossessionTransition();
	TestEqual(TEXT("Duplicate transition cancellation cannot restart recovery"), AbilitySystem->GetPartnerHackChargeState().RechargeStartServerTime, RechargeAfterCancel);
	return true;
}

#endif
