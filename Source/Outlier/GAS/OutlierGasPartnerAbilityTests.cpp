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
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/App.h"

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
		Controller = World->SpawnActor<APartnerPlayerController>();
		if (!Test.TestNotNull(TEXT("Native Partner controller is spawned"), Controller))
		{
			Shutdown();
			return false;
		}
		Controller->Possess(Partner);
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
};

FOutlierPartnerAbilityConfig MakeTestConfig()
{
	FOutlierPartnerAbilityConfig Config;
	Config.EMPCooldown = 30.0f;
	Config.ShieldCooldown = 20.0f;
	Config.HackCooldown = 3.0f;
	Config.ScanCooldown = 10.0f;
	Config.ScanDuration = 3.0f;
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
	HackComponent->OnHackFinished.Broadcast(EHackResult::Cancelled, false);
	HackComponent->CancelForReboot();
	TestTrue(TEXT("Cancelled Hack commits cooldown"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestTrue(
		TEXT("Cancelled Hack commits half of the configured cooldown"),
		IsNearlyEqualDuration(
			AbilitySystem->GetPartnerCooldownRemaining(HackCooldown),
			MakeTestConfig().HackCooldown * 0.5f));
	AbilitySystem->RemoveActiveEffects(
		FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(HackCooldown)));
	TestTrue(TEXT("Hack can restart after the cancellation cooldown is cleared"), AbilitySystem->TryActivatePartnerAbility(HackAbility));
	HackComponent->OnHackFinished.Broadcast(EHackResult::Fail, false);
	HackComponent->CancelForReboot();
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
	FPartnerEMPAbilityData EMPData;
	EMPData.StunDuration = 0.05f;
	EMPComponent->CacheAbilityData(EMPData);
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
	HackComponent->OnHackFinished.Broadcast(EHackResult::Success, true);
	HackComponent->CancelForReboot();
	TestFalse(TEXT("Possession Hack success still has no cooldown before commit"), AbilitySystem->IsPartnerCooldownActive(HackCooldown));
	TestTrue(TEXT("An existing EMP cooldown is active before possession"), AbilitySystem->CommitPartnerCooldown(EMPCooldown, 17.0f));
	TestTrue(TEXT("Successful possession commit starts one cooldown session"), Fixture.Controller->BeginCommittedPartnerAbilityCooldownSession(Fixture.Partner));
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

#endif
