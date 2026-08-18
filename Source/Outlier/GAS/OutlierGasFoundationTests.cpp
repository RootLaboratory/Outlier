#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SphereComponent.h"
#include "Damage/OutlierTaggedDamageEvent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyTargetRules.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "UObject/UnrealType.h"

namespace
{
	FDataTableRowHandle MakeDataTableRowHandle(UDataTable* DataTable, FName RowName)
	{
		FDataTableRowHandle Handle;
		Handle.DataTable = DataTable;
		Handle.RowName = RowName;
		return Handle;
	}

	bool ReadBoolProperty(const UObject* Object, FName PropertyName)
	{
		const FBoolProperty* Property = Object
			? FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName)
			: nullptr;
		return Property && Property->GetPropertyValue_InContainer(Object);
	}

	FActiveGameplayEffectHandle ApplyTaggedInfiniteEffect(
		UAbilitySystemComponent* AbilitySystem,
		const FGameplayTag& AssetTag)
	{
		UGameplayEffect* Effect = NewObject<UGameplayEffect>();
		Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;

		FInheritedTagContainer AssetTags;
		AssetTags.AddTag(AssetTag);
		Effect->FindOrAddComponent<UAssetTagsGameplayEffectComponent>()
			.SetAndApplyAssetTagChanges(AssetTags);

		return AbilitySystem->ApplyGameplayEffectToSelf(
			Effect,
			1.0f,
			AbilitySystem->MakeEffectContext());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerRebootLifecycleTest,
	"Outlier.GAS.Partner.RebootLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerRebootLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* VitalityTable = NewObject<UDataTable>();
	VitalityTable->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 120.0f;
	VitalityTable->AddRow(TEXT("Partner"), VitalityRow);

	UDataTable* SurvivalTable = NewObject<UDataTable>();
	SurvivalTable->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 0.05f;
	SurvivalTable->AddRow(TEXT("Default"), SurvivalRow);

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Partner reboot world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bDeferConstruction = true;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Deferred native Partner spawns for reboot lifecycle"), Partner))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Partner->GetOutlierAbilitySystemComponent();
	AbilitySystem->AddAttributeSetSubobject(
		const_cast<UOutlierVitalAttributeSet*>(Partner->GetVitalAttributeSet()));
	AbilitySystem->InitializeForPawn(Partner);
	UPartnerVitalityComponent* VitalityComponent = Partner->GetPartnerVitalityComponent();
	if (!TestTrue(
		TEXT("Partner vitality initializes for reboot lifecycle"),
		VitalityComponent->InitializeFromDataTables(
			MakeDataTableRowHandle(VitalityTable, TEXT("Partner")),
			MakeDataTableRowHandle(SurvivalTable, TEXT("Default")))))
	{
		return false;
	}
	const FActiveGameplayEffectHandle DebuffHandle = ApplyTaggedInfiniteEffect(
		AbilitySystem,
		OutlierGameplayTags::Effect::Debuff());
	const FActiveGameplayEffectHandle BuffHandle = ApplyTaggedInfiniteEffect(
		AbilitySystem,
		OutlierGameplayTags::Effect::Buff());
	const FActiveGameplayEffectHandle CooldownHandle = ApplyTaggedInfiniteEffect(
		AbilitySystem,
		FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Partner.EMP")));

	TestTrue(
		TEXT("Lethal damage is committed before reboot begins"),
		AbilitySystem->ApplyDamageToSelf(
			120.0f,
			nullptr,
			Partner,
			OutlierGameplayTags::Damage::Weapon()));
	TestTrue(TEXT("Lethal transition enters exactly one reboot"), VitalityComponent->IsRebooting());
	TestEqual(
		TEXT("Reboot grants one rebooting tag"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::Rebooting()),
		1);
	TestEqual(
		TEXT("Reboot grants one damage immunity tag"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		1);
	TestNull(TEXT("Reboot removes active debuffs"), AbilitySystem->GetActiveGameplayEffect(DebuffHandle));
	TestNotNull(TEXT("Reboot preserves active buffs"), AbilitySystem->GetActiveGameplayEffect(BuffHandle));
	TestNotNull(TEXT("Reboot preserves cooldown effects"), AbilitySystem->GetActiveGameplayEffect(CooldownHandle));

	FGameplayTagContainer RebootTags;
	RebootTags.AddTag(OutlierGameplayTags::State::Rebooting());
	const TArray<TPair<float, float>> RebootTimes =
		AbilitySystem->GetActiveEffectsTimeRemainingAndDuration(
			FGameplayEffectQuery::MakeQuery_MatchAllOwningTags(RebootTags));
	TestEqual(TEXT("Exactly one reboot duration effect is active"), RebootTimes.Num(), 1);
	if (RebootTimes.Num() == 1)
	{
		TestEqual(TEXT("Reboot duration comes from the transient survival row"), RebootTimes[0].Value, 0.05f);
	}

	TestFalse(
		TEXT("Reboot immunity rejects additional damage"),
		AbilitySystem->ApplyDamageToSelf(
			10.0f,
			nullptr,
			Partner,
			OutlierGameplayTags::Damage::Weapon()));
	TestEqual(TEXT("Rejected damage leaves depleted Health unchanged"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);

	VitalityComponent->SetEnemyPossessionProtection(true);
	VitalityComponent->SetEnemyPossessionProtection(true);
	TestEqual(
		TEXT("Reboot and idempotent possession protection overlap as two immunity stacks"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		2);
	TestTrue(TEXT("Explicit early reboot removal succeeds"), VitalityComponent->RemoveRebootEffect());
	TestFalse(TEXT("Exact reboot removal completes reboot"), VitalityComponent->IsRebooting());
	TestEqual(TEXT("Early completion restores full Health"), Partner->GetVitalAttributeSet()->GetHealth(), 120.0f);
	TestEqual(
		TEXT("Early reboot removal preserves possession protection"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		1);
	VitalityComponent->SetEnemyPossessionProtection(false);
	TestEqual(
		TEXT("Disabling possession protection removes only its exact immunity effect"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		0);

	TestTrue(
		TEXT("Damage can apply immediately after reboot completion"),
		AbilitySystem->ApplyDamageToSelf(
			10.0f,
			nullptr,
			Partner,
			OutlierGameplayTags::Damage::Weapon()));
	TestEqual(TEXT("Immediate subsequent damage changes Health"), Partner->GetVitalAttributeSet()->GetHealth(), 110.0f);

	TestTrue(
		TEXT("A later lethal transition can enter a new reboot"),
		AbilitySystem->ApplyDamageToSelf(
			110.0f,
			nullptr,
			Partner,
			OutlierGameplayTags::Damage::Weapon()));
	for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
	{
		World->Tick(LEVELTICK_All, 0.05f);
		++GFrameCounter;
	}
	TestFalse(TEXT("Natural duration expiry completes reboot"), VitalityComponent->IsRebooting());
	TestEqual(TEXT("Natural duration expiry restores full Health"), Partner->GetVitalAttributeSet()->GetHealth(), 120.0f);

	TestTrue(
		TEXT("A third lethal transition enters reboot before teardown"),
		AbilitySystem->ApplyDamageToSelf(
			120.0f,
			nullptr,
			Partner,
			OutlierGameplayTags::Damage::Weapon()));
	VitalityComponent->BeginOwnerTeardown();
	TestFalse(TEXT("Teardown removes the exact reboot effect"), VitalityComponent->IsRebooting());
	TestFalse(TEXT("Teardown leaves no reboot effect for a second removal"), VitalityComponent->RemoveRebootEffect());
	TestEqual(TEXT("Reboot removal during teardown never heals the terminating Pawn"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);

	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerAssetContractTest,
	"Outlier.GAS.Partner.AssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerAssetContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!TestNotNull(TEXT("Partner Blueprint class loads from disk"), PartnerClass))
	{
		return false;
	}

	const APartnerCharacter* PartnerCDO = PartnerClass->GetDefaultObject<APartnerCharacter>();
	if (!TestNotNull(TEXT("Partner Blueprint CDO exists"), PartnerCDO))
	{
		return false;
	}

	auto GetRowHandle = [this, PartnerClass, PartnerCDO](const FName PropertyName)
		-> const FDataTableRowHandle*
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(PartnerClass, PropertyName);
		if (!TestNotNull(*FString::Printf(TEXT("%s property is reflected"), *PropertyName.ToString()), Property))
		{
			return nullptr;
		}
		if (!TestTrue(
			*FString::Printf(TEXT("%s uses FDataTableRowHandle"), *PropertyName.ToString()),
			Property->Struct == FDataTableRowHandle::StaticStruct()))
		{
			return nullptr;
		}

		return Property->ContainerPtrToValuePtr<FDataTableRowHandle>(PartnerCDO);
	};

	const FDataTableRowHandle* VitalityHandle = GetRowHandle(TEXT("VitalityDataRow"));
	const FDataTableRowHandle* SurvivalHandle = GetRowHandle(TEXT("PartnerSurvivalDataRow"));
	if (!VitalityHandle || !SurvivalHandle)
	{
		return false;
	}

	UDataTable* VitalityTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/Datas/Character/Stat/Common/DT_Vitality.DT_Vitality"));
	UDataTable* SurvivalTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/Datas/Character/Stat/Partner/DT_PartnerSurvivalDataRow.DT_PartnerSurvivalDataRow"));
	if (!TestNotNull(TEXT("Vitality DataTable asset loads"), VitalityTable)
		|| !TestNotNull(TEXT("Partner survival DataTable asset loads"), SurvivalTable))
	{
		return false;
	}

	TestTrue(TEXT("Vitality table uses the shared row struct"), VitalityTable->GetRowStruct() == FOutlierVitalityDataRow::StaticStruct());
	TestTrue(TEXT("Survival table uses the narrowed row struct"), SurvivalTable->GetRowStruct() == FPartnerSurvivalDataRow::StaticStruct());
	const TArray<FName> VitalityRowNames = VitalityTable->GetRowNames();
	const TArray<FName> SurvivalRowNames = SurvivalTable->GetRowNames();
	TestEqual(TEXT("Vitality table contains exactly one row"), VitalityRowNames.Num(), 1);
	TestEqual(TEXT("Survival table contains exactly one row"), SurvivalRowNames.Num(), 1);
	if (VitalityRowNames.Num() == 1)
	{
		TestEqual(TEXT("Vitality table singleton is Partner"), VitalityRowNames[0], FName(TEXT("Partner")));
	}
	if (SurvivalRowNames.Num() == 1)
	{
		TestEqual(TEXT("Survival table singleton is Default"), SurvivalRowNames[0], FName(TEXT("Default")));
	}
	TestTrue(TEXT("Partner CDO references the vitality table"), VitalityHandle->DataTable.Get() == VitalityTable);
	TestEqual(TEXT("Partner CDO selects the Partner vitality row"), VitalityHandle->RowName, FName(TEXT("Partner")));
	TestTrue(TEXT("Partner CDO references the survival table"), SurvivalHandle->DataTable.Get() == SurvivalTable);
	TestEqual(TEXT("Partner CDO selects the Default survival row"), SurvivalHandle->RowName, FName(TEXT("Default")));

	const FOutlierVitalityDataRow* VitalityRow = VitalityTable->FindRow<FOutlierVitalityDataRow>(
		TEXT("Partner"),
		TEXT("Outlier.GAS.Partner.AssetContract"));
	const FPartnerSurvivalDataRow* SurvivalRow = SurvivalTable->FindRow<FPartnerSurvivalDataRow>(
		TEXT("Default"),
		TEXT("Outlier.GAS.Partner.AssetContract"));
	if (!TestNotNull(TEXT("Partner vitality row exists"), VitalityRow)
		|| !TestNotNull(TEXT("Default survival row exists"), SurvivalRow))
	{
		return false;
	}

	TestEqual(TEXT("Partner MaxHealth is sourced from CSV"), VitalityRow->MaxHealth, 100.0f);
	TestTrue(TEXT("Default RebootTime is positive"), SurvivalRow->RebootTime > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerDamageBoundaryTest,
	"Outlier.GAS.Partner.DamageBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerDamageBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* VitalityTable = NewObject<UDataTable>();
	VitalityTable->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 100.0f;
	VitalityTable->AddRow(TEXT("Partner"), VitalityRow);

	UDataTable* SurvivalTable = NewObject<UDataTable>();
	SurvivalTable->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 1.0f;
	SurvivalTable->AddRow(TEXT("Default"), SurvivalRow);

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Partner damage boundary world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Partner damage world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bDeferConstruction = true;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Deferred native Partner spawns for damage boundary"), Partner))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Partner->GetOutlierAbilitySystemComponent();
	AbilitySystem->AddAttributeSetSubobject(
		const_cast<UOutlierVitalAttributeSet*>(Partner->GetVitalAttributeSet()));
	AbilitySystem->InitializeForPawn(Partner);
	UPartnerVitalityComponent* VitalityComponent = Partner->GetPartnerVitalityComponent();
	if (!TestTrue(
		TEXT("Partner vitality initializes for damage boundary"),
		VitalityComponent->InitializeFromDataTables(
			MakeDataTableRowHandle(VitalityTable, TEXT("Partner")),
			MakeDataTableRowHandle(SurvivalTable, TEXT("Default")))))
	{
		return false;
	}

	FOutlierTaggedDamageEvent TaggedDamageEvent;
	TaggedDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	const float AppliedDamage = static_cast<AActor*>(Partner)->TakeDamage(
		25.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Partner TakeDamage reports applied GAS damage"), AppliedDamage, 25.0f);
	TestEqual(TEXT("Partner TakeDamage reduces GAS Health"), Partner->GetVitalAttributeSet()->GetHealth(), 75.0f);
	TestFalse(TEXT("Non-lethal Partner damage does not enter Reboot"), VitalityComponent->IsRebooting());

	UPartnerHackComponent* HackComponent = Partner->FindComponentByClass<UPartnerHackComponent>();
	UPartnerEMPComponent* EMPComponent = Partner->FindComponentByClass<UPartnerEMPComponent>();
	UPartnerSupportComponent* SupportComponent = Partner->FindComponentByClass<UPartnerSupportComponent>();
	if (!TestNotNull(TEXT("Partner owns a Hack component"), HackComponent)
		|| !TestNotNull(TEXT("Partner owns an EMP component"), EMPComponent)
		|| !TestNotNull(TEXT("Partner owns a Support component"), SupportComponent))
	{
		return false;
	}
	HackComponent->RefreshCharacterRefsFromPlayerState();
	EMPComponent->RefreshCharacterRefsFromPlayerState();
	SupportComponent->RefreshCharacterRefsFromPlayerState();
	HackComponent->TryHack();
	EMPComponent->TryEMP();
	SupportComponent->TryScan_Server();
	TestTrue(TEXT("Hack interaction starts before Reboot"), HackComponent->IsHackInteractionActive());
	TestTrue(TEXT("EMP interaction starts before Reboot"), EMPComponent->IsEMPInteractionActive());
	TestTrue(TEXT("Scan starts before Reboot"), ReadBoolProperty(Partner, TEXT("bScanning")));

	const float LethalDamage = static_cast<AActor*>(Partner)->TakeDamage(
		75.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Lethal Partner TakeDamage reports applied GAS damage"), LethalDamage, 75.0f);
	TestEqual(TEXT("Lethal Partner TakeDamage clamps Health to zero"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);
	TestTrue(TEXT("Lethal Partner TakeDamage enters Reboot"), VitalityComponent->IsRebooting());
	TestFalse(TEXT("Reboot tag blocks new Partner input"), Partner->CanAcceptInput());
	TestFalse(TEXT("Reboot cancels an active Hack interaction"), HackComponent->IsHackInteractionActive());
	TestFalse(TEXT("Reboot cancels an active EMP interaction"), EMPComponent->IsEMPInteractionActive());
	TestFalse(TEXT("Reboot cancels an active Scan"), ReadBoolProperty(Partner, TEXT("bScanning")));

	HackComponent->TryHack();
	EMPComponent->TryEMP();
	SupportComponent->TryScan_Server();
	TestFalse(TEXT("Reboot rejects a new Hack interaction"), HackComponent->IsHackInteractionActive());
	TestFalse(TEXT("Reboot rejects a new EMP interaction"), EMPComponent->IsEMPInteractionActive());
	TestFalse(TEXT("Reboot rejects a new Scan"), ReadBoolProperty(Partner, TEXT("bScanning")));
	HackComponent->CancelForReboot();
	EMPComponent->CancelForReboot();
	SupportComponent->CancelForReboot();
	TestFalse(TEXT("Reboot cleanup remains allowed for Hack"), HackComponent->IsHackInteractionActive());
	TestFalse(TEXT("Reboot cleanup remains allowed for EMP"), EMPComponent->IsEMPInteractionActive());
	TestFalse(TEXT("Reboot cleanup remains allowed for Scan"), ReadBoolProperty(Partner, TEXT("bScanning")));

	FGameplayTagContainer OwnedTags = Partner->GetOwnedGameplayTagsForQuery();
	TestTrue(
		TEXT("Partner query tags include ASC Reboot tag"),
		OwnedTags.HasTagExact(OutlierGameplayTags::State::Rebooting()));

	const float RejectedDamage = static_cast<AActor*>(Partner)->TakeDamage(
		10.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Partner TakeDamage reports zero when GAS immunity rejects damage"), RejectedDamage, 0.0f);
	TestEqual(TEXT("Rejected Partner damage leaves Health unchanged"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);

	TestTrue(TEXT("Explicit Reboot removal completes Partner recovery"), VitalityComponent->RemoveRebootEffect());
	TestTrue(TEXT("Recovered Partner accepts input again"), Partner->CanAcceptInput());
	TestEqual(TEXT("Recovered Partner restores Health to MaxHealth"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);

	Partner->SetEnemyPossessionProtection(true);
	TestEqual(
		TEXT("Possession protection grants one damage immunity tag"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		1);
	const float ProtectedDamage = static_cast<AActor*>(Partner)->TakeDamage(
		10.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Possession protection makes TakeDamage report zero"), ProtectedDamage, 0.0f);
	TestEqual(TEXT("Possession protection leaves Health unchanged"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);
	Partner->SetEnemyPossessionProtection(false);
	TestEqual(
		TEXT("Possession protection removal clears its damage immunity tag"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		0);

	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerDataTableValidationTest,
	"Outlier.GAS.Partner.DataTableValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerDataTableValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* VitalityTable = NewObject<UDataTable>();
	VitalityTable->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 160.0f;
	VitalityTable->AddRow(TEXT("Partner"), VitalityRow);

	UDataTable* SurvivalTable = NewObject<UDataTable>();
	SurvivalTable->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 4.0f;
	SurvivalTable->AddRow(TEXT("Default"), SurvivalRow);

	const FDataTableRowHandle ValidVitalityHandle = MakeDataTableRowHandle(VitalityTable, TEXT("Partner"));
	const FDataTableRowHandle ValidSurvivalHandle = MakeDataTableRowHandle(SurvivalTable, TEXT("Default"));
	float MaxHealth = 0.0f;
	float RebootTime = 0.0f;
	FString Error;
	TestTrue(
		TEXT("Valid transient Partner rows resolve"),
		UPartnerVitalityComponent::ValidateDataTableRows(
			ValidVitalityHandle,
			ValidSurvivalHandle,
			MaxHealth,
			RebootTime,
			Error));
	TestEqual(TEXT("Validated MaxHealth comes from the selected row"), MaxHealth, 160.0f);
	TestEqual(TEXT("Validated RebootTime comes from the selected row"), RebootTime, 4.0f);

	auto TestInvalidRows = [this](
		const TCHAR* What,
		const FDataTableRowHandle& VitalityHandle,
		const FDataTableRowHandle& SurvivalHandle)
	{
		float InvalidMaxHealth = 0.0f;
		float InvalidRebootTime = 0.0f;
		FString ValidationError;
		TestFalse(
			What,
			UPartnerVitalityComponent::ValidateDataTableRows(
				VitalityHandle,
				SurvivalHandle,
				InvalidMaxHealth,
				InvalidRebootTime,
				ValidationError));
		TestFalse(TEXT("Invalid configuration reports a precise error"), ValidationError.IsEmpty());
	};

	TestInvalidRows(
		TEXT("Missing vitality table is rejected"),
		FDataTableRowHandle{},
		ValidSurvivalHandle);
	TestInvalidRows(
		TEXT("Missing vitality row is rejected"),
		MakeDataTableRowHandle(VitalityTable, TEXT("Missing")),
		ValidSurvivalHandle);
	TestInvalidRows(
		TEXT("Wrong vitality row type is rejected"),
		MakeDataTableRowHandle(SurvivalTable, TEXT("Default")),
		ValidSurvivalHandle);

	VitalityRow.MaxHealth = 0.0f;
	VitalityTable->AddRow(TEXT("Invalid"), VitalityRow);
	TestInvalidRows(
		TEXT("Non-positive MaxHealth is rejected"),
		MakeDataTableRowHandle(VitalityTable, TEXT("Invalid")),
		ValidSurvivalHandle);

	TestInvalidRows(
		TEXT("Missing survival table is rejected"),
		ValidVitalityHandle,
		FDataTableRowHandle{});
	TestInvalidRows(
		TEXT("Missing survival row is rejected"),
		ValidVitalityHandle,
		MakeDataTableRowHandle(SurvivalTable, TEXT("Missing")));
	TestInvalidRows(
		TEXT("Wrong survival row type is rejected"),
		ValidVitalityHandle,
		MakeDataTableRowHandle(VitalityTable, TEXT("Partner")));

	SurvivalRow.RebootTime = 0.0f;
	SurvivalTable->AddRow(TEXT("Invalid"), SurvivalRow);
	TestInvalidRows(
		TEXT("Non-positive RebootTime is rejected"),
		ValidVitalityHandle,
		MakeDataTableRowHandle(SurvivalTable, TEXT("Invalid")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasPartnerDataTableInitializationTest,
	"Outlier.GAS.Partner.DataTableInitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasPartnerDataTableInitializationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* VitalityTable = NewObject<UDataTable>();
	VitalityTable->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 160.0f;
	VitalityTable->AddRow(TEXT("Partner"), VitalityRow);

	UDataTable* SurvivalTable = NewObject<UDataTable>();
	SurvivalTable->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 4.0f;
	SurvivalTable->AddRow(TEXT("Default"), SurvivalRow);

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Partner initialization world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bDeferConstruction = true;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Deferred native Partner spawns without BP defaults"), Partner))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	Partner->GetOutlierAbilitySystemComponent()->AddAttributeSetSubobject(
		const_cast<UOutlierVitalAttributeSet*>(Partner->GetVitalAttributeSet()));
	Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);
	UPartnerVitalityComponent* VitalityComponent = Partner->GetPartnerVitalityComponent();
	TestNotNull(TEXT("Partner owns a native vitality component"), VitalityComponent);
	TestTrue(
		TEXT("Authority initializes Partner vitality from transient rows"),
		VitalityComponent->InitializeFromDataTables(
			MakeDataTableRowHandle(VitalityTable, TEXT("Partner")),
			MakeDataTableRowHandle(SurvivalTable, TEXT("Default"))));
	TestTrue(TEXT("Component records successful initialization"), VitalityComponent->IsInitialized());
	TestEqual(TEXT("GAS MaxHealth comes from the vitality row"), Partner->GetVitalAttributeSet()->GetMaxHealth(), 160.0f);
	TestEqual(TEXT("GAS Health starts at configured MaxHealth"), Partner->GetVitalAttributeSet()->GetHealth(), 160.0f);
	TestEqual(TEXT("Configured RebootTime comes from the survival row"), VitalityComponent->GetConfiguredRebootTime(), 4.0f);

	Partner->GetOutlierAbilitySystemComponent()->SetNumericAttributeBase(
		UOutlierVitalAttributeSet::GetHealthAttribute(),
		90.0f);
	TestTrue(
		TEXT("Repeated successful initialization is an idempotent success"),
		VitalityComponent->InitializeFromDataTables(
			MakeDataTableRowHandle(VitalityTable, TEXT("Partner")),
			MakeDataTableRowHandle(SurvivalTable, TEXT("Default"))));
	TestEqual(TEXT("Repeated initialization does not reset current Health"), Partner->GetVitalAttributeSet()->GetHealth(), 90.0f);

	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierEnemyTargetRulesTest,
	"Outlier.GAS.Enemy.TargetRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierEnemyTargetRulesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("Null target is unavailable"), OutlierEnemyTargetRules::IsUnavailable(nullptr));

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Enemy target rules world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy target rules world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	AActor* PlainActor = World->SpawnActor<AActor>();
	TestNotNull(TEXT("Plain Actor spawns for target rules"), PlainActor);
	TestFalse(
		TEXT("Valid non-provider target remains available"),
		OutlierEnemyTargetRules::IsUnavailable(PlainActor));

	UDataTable* VitalityTable = NewObject<UDataTable>();
	VitalityTable->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 100.0f;
	VitalityTable->AddRow(TEXT("Partner"), VitalityRow);

	UDataTable* SurvivalTable = NewObject<UDataTable>();
	SurvivalTable->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 1.0f;
	SurvivalTable->AddRow(TEXT("Default"), SurvivalRow);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bDeferConstruction = true;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(
		APartnerCharacter::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Deferred native Partner spawns for target rules"), Partner))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Partner->GetOutlierAbilitySystemComponent();
	AbilitySystem->AddAttributeSetSubobject(
		const_cast<UOutlierVitalAttributeSet*>(Partner->GetVitalAttributeSet()));
	AbilitySystem->InitializeForPawn(Partner);
	UPartnerVitalityComponent* VitalityComponent = Partner->GetPartnerVitalityComponent();
	if (!TestTrue(
		TEXT("Partner vitality initializes for target rules"),
		VitalityComponent->InitializeFromDataTables(
			MakeDataTableRowHandle(VitalityTable, TEXT("Partner")),
			MakeDataTableRowHandle(SurvivalTable, TEXT("Default")))))
	{
		return false;
	}

	TestFalse(
		TEXT("Initial Partner target remains available"),
		OutlierEnemyTargetRules::IsUnavailable(Partner));

	Partner->SetTestStealthed(true);
	TestTrue(
		TEXT("Exact Stealthed tag makes Partner unavailable"),
		OutlierEnemyTargetRules::IsUnavailable(Partner));
	Partner->SetTestStealthed(false);
	TestFalse(
		TEXT("Removing exact Stealthed tag makes Partner available"),
		OutlierEnemyTargetRules::IsUnavailable(Partner));

	const FActiveGameplayEffectHandle RebootHandle = AbilitySystem->ApplyRebootStateToSelf(1.0f);
	TestTrue(TEXT("Reboot state applies for target rules"), RebootHandle.IsValid());
	TestTrue(
		TEXT("Exact Rebooting tag makes Partner unavailable"),
		OutlierEnemyTargetRules::IsUnavailable(Partner));
	TestTrue(TEXT("Exact Rebooting effect removal succeeds"), AbilitySystem->RemoveActiveEffectFromSelf(RebootHandle));
	TestFalse(
		TEXT("Removing exact Rebooting tag makes Partner available"),
		OutlierEnemyTargetRules::IsUnavailable(Partner));

	PlainActor->Destroy(true);
	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasWeaponReuseCooldownTest,
	"Outlier.GAS.Weapon.ReuseCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasWeaponReuseCooldownTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient weapon cooldown world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* WeaponClass = LoadClass<ARangedWeaponBase>(
		nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	ARangedWeaponBase* FirstWeapon = WeaponClass
		? World->SpawnActor<ARangedWeaponBase>(WeaponClass)
		: nullptr;
	ARangedWeaponBase* SecondWeapon = WeaponClass
		? World->SpawnActor<ARangedWeaponBase>(WeaponClass)
		: nullptr;
	if (!TestNotNull(TEXT("Shooter is spawned"), Shooter)
		|| !TestNotNull(TEXT("First weapon is spawned"), FirstWeapon)
		|| !TestNotNull(TEXT("Second weapon is spawned"), SecondWeapon))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}
	if (!FirstWeapon->HasActorBegunPlay())
	{
		FirstWeapon->DispatchBeginPlay();
	}
	if (!SecondWeapon->HasActorBegunPlay())
	{
		SecondWeapon->DispatchBeginPlay();
	}
	FirstWeapon->OnEquipped(Shooter);
	SecondWeapon->OnEquipped(Shooter);
	UOutlierAbilitySystemComponent* AbilitySystem = Shooter->GetOutlierAbilitySystemComponent();
	TestNotNull(TEXT("Shooter ASC exists"), AbilitySystem);

	const FGameplayTag ReuseCooldownTag = OutlierGameplayTags::Cooldown::Weapon::Reuse();
	const FActiveGameplayEffectHandle CooldownHandle = AbilitySystem->CommitTimedCooldown(
		UOutlierWeaponReuseCooldownGameplayEffect::StaticClass(),
		ReuseCooldownTag,
		0.05f,
		FirstWeapon);
	TestTrue(TEXT("Weapon reuse cooldown applies through a GameplayEffect"), CooldownHandle.IsValid());
	TestTrue(TEXT("The source weapon is on cooldown"), FirstWeapon->IsOnReuseCooldown());
	TestFalse(TEXT("A different weapon is not blocked by the source weapon cooldown"), SecondWeapon->IsOnReuseCooldown());
	TestTrue(
		TEXT("Remaining time comes from the active GameplayEffect"),
		FMath::IsNearlyEqual(FirstWeapon->GetReuseCooldownRemaining(), 0.05f, 0.02f));
	TestTrue(TEXT("The shared cooldown tag is present on the owner ASC"), AbilitySystem->HasMatchingGameplayTag(ReuseCooldownTag));
	const FActiveGameplayEffectHandle SecondCooldownHandle = AbilitySystem->CommitTimedCooldown(
		UOutlierWeaponReuseCooldownGameplayEffect::StaticClass(),
		ReuseCooldownTag,
		5.0f,
		SecondWeapon);
	TestTrue(TEXT("A second weapon can own an independent cooldown"), SecondCooldownHandle.IsValid());
	TestTrue(TEXT("The second weapon now reports its own cooldown"), SecondWeapon->IsOnReuseCooldown());
	Shooter->EquipWeapon(SecondWeapon);
	for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
	{
		World->Tick(LEVELTICK_All, 0.05f);
		++GFrameCounter;
	}
	TestFalse(TEXT("The first weapon cooldown elapses while another weapon is equipped"), FirstWeapon->IsOnReuseCooldown());
	TestTrue(TEXT("The equipped second weapon cooldown remains independent"), SecondWeapon->IsOnReuseCooldown());
	TestTrue(TEXT("The shared tag remains while another weapon effect exists"), AbilitySystem->HasMatchingGameplayTag(ReuseCooldownTag));
	TestTrue(TEXT("Removing the second cooldown succeeds"), AbilitySystem->RemoveActiveEffectFromSelf(SecondCooldownHandle));
	TestFalse(TEXT("The shared tag clears after the final weapon cooldown"), AbilitySystem->HasMatchingGameplayTag(ReuseCooldownTag));

	FirstWeapon->Destroy(true);
	SecondWeapon->Destroy(true);
	Shooter->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasFoundationTopologyTest,
	"Outlier.GAS.Foundation.Topology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasFoundationTopologyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const AShooterCharacter* Shooter = GetDefault<AShooterCharacter>();
	const APartnerCharacter* Partner = GetDefault<APartnerCharacter>();
	const AEnemyBase* Enemy = GetDefault<AEnemyBase>();
	const AAutoTurret* Turret = GetDefault<AAutoTurret>();

#if WITH_EDITOR
	TestFalse(
		TEXT("Native ASC cannot be added again through Blueprint Add Component"),
		UOutlierAbilitySystemComponent::StaticClass()->HasMetaData(TEXT("BlueprintSpawnableComponent")));
#endif

	TestNotNull(TEXT("Shooter has one inherited ASC"), Shooter->GetOutlierAbilitySystemComponent());
	TestNotNull(TEXT("Shooter has the common Vital set"), Shooter->GetVitalAttributeSet());
	TestNotNull(TEXT("Shooter has the optional Shield set"), Shooter->GetShieldAttributeSet());
	TestEqual(
		TEXT("Shooter ASC uses fixed Mixed replication"),
		Shooter->GetOutlierAbilitySystemComponent()->GetConfiguredReplicationMode(),
		EGameplayEffectReplicationMode::Mixed);

	TestNotNull(TEXT("Partner has one inherited ASC"), Partner->GetOutlierAbilitySystemComponent());
	TestNotNull(TEXT("Partner has the common Vital set"), Partner->GetVitalAttributeSet());
	TestEqual(
		TEXT("Partner ASC uses fixed Mixed replication"),
		Partner->GetOutlierAbilitySystemComponent()->GetConfiguredReplicationMode(),
		EGameplayEffectReplicationMode::Mixed);

	TestNotNull(TEXT("Enemy has one inherited ASC"), Enemy->GetOutlierAbilitySystemComponent());
	TestNotNull(TEXT("Enemy has the common Vital set"), Enemy->GetVitalAttributeSet());
	TestEqual(
		TEXT("Possession-capable Enemy ASC uses fixed Mixed replication"),
		Enemy->GetOutlierAbilitySystemComponent()->GetConfiguredReplicationMode(),
		EGameplayEffectReplicationMode::Mixed);

	TestEqual(
		TEXT("Never-possessable AutoTurret ASC uses fixed Minimal replication"),
		Turret->GetOutlierAbilitySystemComponent()->GetConfiguredReplicationMode(),
		EGameplayEffectReplicationMode::Minimal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasFoundationAttributeDefaultsTest,
	"Outlier.GAS.Foundation.AttributeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasFoundationAttributeDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FOutlierVitalityDataRow VitalityRow;
	const UOutlierVitalAttributeSet* Vital = GetDefault<UOutlierVitalAttributeSet>();
	const UOutlierShieldAttributeSet* Shield = GetDefault<UOutlierShieldAttributeSet>();

	TestEqual(TEXT("Vitality row default MaxHealth"), VitalityRow.MaxHealth, 100.0f);
	TestEqual(TEXT("Vital Health starts at the foundation default"), Vital->GetHealth(), 100.0f);
	TestEqual(TEXT("Vital MaxHealth starts at the foundation default"), Vital->GetMaxHealth(), 100.0f);
	TestEqual(TEXT("IncomingDamage is a zeroed meta attribute"), Vital->GetIncomingDamage(), 0.0f);
	TestEqual(TEXT("Shield starts at the foundation default"), Shield->GetShield(), 100.0f);
	TestEqual(TEXT("MaxShield starts at the foundation default"), Shield->GetMaxShield(), 100.0f);
	TestEqual(TEXT("PartnerShield starts empty"), Shield->GetPartnerShield(), 0.0f);
	TestEqual(TEXT("MaxPartnerShield starts empty"), Shield->GetMaxPartnerShield(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasFoundationVitalityPrimitiveTest,
	"Outlier.GAS.Foundation.VitalityPrimitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasFoundationVitalityPrimitiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("Health data tag is registered"), OutlierGameplayTags::Data::Health().IsValid());
	TestTrue(TEXT("MaxHealth data tag is registered"), OutlierGameplayTags::Data::MaxHealth().IsValid());
	TestTrue(TEXT("Rebooting state tag is registered"), OutlierGameplayTags::State::Rebooting().IsValid());
	TestTrue(TEXT("DamageImmune state tag is registered"), OutlierGameplayTags::State::DamageImmune().IsValid());
	TestTrue(TEXT("Buff effect tag is registered"), OutlierGameplayTags::Effect::Buff().IsValid());
	TestTrue(TEXT("Debuff effect tag is registered"), OutlierGameplayTags::Effect::Debuff().IsValid());

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient vitality primitive world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	if (!TestNotNull(TEXT("Shooter spawns for vitality primitive test"), Shooter))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Shooter->GetOutlierAbilitySystemComponent();
	AbilitySystem->InitializeForPawn(Shooter);

	TestTrue(TEXT("Initialize vitality GameplayEffect applies"), AbilitySystem->InitializeVitalityToSelf(175.0f));
	TestEqual(TEXT("Initialize sets MaxHealth"), Shooter->GetVitalAttributeSet()->GetMaxHealth(), 175.0f);
	TestEqual(TEXT("Initialize sets Health"), Shooter->GetVitalAttributeSet()->GetHealth(), 175.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 0.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 0.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 0.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 0.0f);

	TestTrue(
		TEXT("Damage can reduce initialized health"),
		AbilitySystem->ApplyDamageToSelf(40.0f, nullptr, Shooter, OutlierGameplayTags::Damage::Weapon()));
	TestEqual(TEXT("Damage changed Health before restore"), Shooter->GetVitalAttributeSet()->GetHealth(), 135.0f);

	TestTrue(TEXT("Restore Health to Max GameplayEffect applies"), AbilitySystem->RestoreHealthToMax());
	TestEqual(TEXT("Restore sets Health back to MaxHealth"), Shooter->GetVitalAttributeSet()->GetHealth(), 175.0f);

	const FActiveGameplayEffectHandle RebootHandle = AbilitySystem->ApplyRebootStateToSelf(2.5f);
	TestTrue(TEXT("Reboot state GameplayEffect applies"), RebootHandle.IsValid());
	TestTrue(
		TEXT("Reboot grants rebooting tag"),
		AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()));
	TestTrue(
		TEXT("Reboot grants damage immunity tag"),
		AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	float RebootStartTime = 0.0f;
	float RebootDuration = 0.0f;
	AbilitySystem->GetGameplayEffectStartTimeAndDuration(
		RebootHandle,
		RebootStartTime,
		RebootDuration);
	TestEqual(TEXT("Reboot duration comes from the outgoing spec"), RebootDuration, 2.5f);
	TestTrue(TEXT("Reboot effect can be removed by handle"), AbilitySystem->RemoveActiveEffectFromSelf(RebootHandle));

	const FActiveGameplayEffectHandle ImmuneHandle = AbilitySystem->ApplyDamageImmuneStateToSelf();
	TestTrue(TEXT("Damage-immune GameplayEffect applies"), ImmuneHandle.IsValid());
	TestTrue(
		TEXT("DamageImmune tag blocks incoming damage"),
		AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestFalse(
		TEXT("Damage is rejected while damage immunity is active"),
		AbilitySystem->ApplyDamageToSelf(10.0f, nullptr, Shooter, OutlierGameplayTags::Damage::Weapon()));
	TestEqual(TEXT("Rejected damage leaves Health unchanged"), Shooter->GetVitalAttributeSet()->GetHealth(), 175.0f);
	TestTrue(TEXT("Damage-immune effect can be removed by handle"), AbilitySystem->RemoveActiveEffectFromSelf(ImmuneHandle));

	Shooter->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasFoundationAttributeMaxClampTest,
	"Outlier.GAS.Foundation.AttributeMaxClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasFoundationAttributeMaxClampTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient GAS test world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	if (!TestNotNull(TEXT("Shooter spawns in the transient GAS test world"), Shooter))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	Shooter->GetOutlierAbilitySystemComponent()->InitializeForPawn(Shooter);
	UOutlierVitalAttributeSet* Vital = const_cast<UOutlierVitalAttributeSet*>(Shooter->GetVitalAttributeSet());
	UOutlierShieldAttributeSet* Shield = const_cast<UOutlierShieldAttributeSet*>(Shooter->GetShieldAttributeSet());

	Vital->SetMaxHealth(40.0f);
	TestEqual(TEXT("Reducing MaxHealth clamps Health"), Vital->GetHealth(), 40.0f);
	Vital->SetMaxHealth(100.0f);
	Vital->SetHealth(100.0f);

	Shield->SetMaxShield(25.0f);
	TestEqual(TEXT("Reducing MaxShield clamps Shield"), Shield->GetShield(), 25.0f);
	Shield->SetMaxShield(100.0f);
	Shield->SetShield(100.0f);

	Shield->SetMaxPartnerShield(50.0f);
	Shield->SetPartnerShield(50.0f);
	Shield->SetMaxPartnerShield(10.0f);
	TestEqual(
		TEXT("Reducing MaxPartnerShield clamps PartnerShield"),
		Shield->GetPartnerShield(),
		10.0f);
	Shield->SetPartnerShield(0.0f);
	Shield->SetMaxPartnerShield(0.0f);

	Shooter->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasEnemyAttributeAuthorityBoundaryTest,
	"Outlier.GAS.Enemy.AttributeAuthorityBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasEnemyAttributeAuthorityBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const AEnemyBase* Enemy = GetDefault<AEnemyBase>();
	TestEqual(
		TEXT("Enemy health query reads the GAS Vital attribute"),
		Enemy->GetCurrentHealth(),
		Enemy->GetVitalAttributeSet()->GetHealth());
	TestEqual(
		TEXT("Enemy CDO keeps the shared Vital foundation default"),
		Enemy->GetVitalAttributeSet()->GetHealth(),
		100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasEnemyDamageFlowTest,
	"Outlier.GAS.Enemy.DamageFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasEnemyDamageFlowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Enemy damage world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy damage world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	UClass* EnemyClass = LoadClass<AEnemyBase>(
		nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	AEnemyBase* Enemy = EnemyClass ? World->SpawnActor<AEnemyBase>(EnemyClass) : nullptr;
	if (!TestNotNull(TEXT("Enemy spawns for GAS damage flow"), Enemy))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	if (!Enemy->HasActorBegunPlay())
	{
		Enemy->DispatchBeginPlay();
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Enemy->GetOutlierAbilitySystemComponent();
	AbilitySystem->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);

	USphereComponent* CoreHitbox = nullptr;
	TInlineComponentArray<USphereComponent*> SphereComponents(Enemy);
	for (USphereComponent* SphereComponent : SphereComponents)
	{
		if (SphereComponent && SphereComponent->GetFName() == TEXT("CoreHitboxComponent"))
		{
			CoreHitbox = SphereComponent;
			break;
		}
	}
	TestNotNull(TEXT("Enemy exposes its core weak-point component"), CoreHitbox);
	FOutlierTaggedDamageEvent WeaponDamageEvent;
	WeaponDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	WeaponDamageEvent.HitResult.Component = CoreHitbox;
	const float AppliedWeakPointDamage = Enemy->TakeDamage(
		10.0f,
		WeaponDamageEvent,
		nullptr,
		Enemy);
	const float ExpectedWeakPointDamage = 10.0f * Enemy->GetWeakPointDamageMultiplier(CoreHitbox);
	TestTrue(TEXT("Core hit is configured as a weak point"), ExpectedWeakPointDamage > 10.0f);
	TestEqual(TEXT("Core weak-point multiplier is applied"), AppliedWeakPointDamage, ExpectedWeakPointDamage);
	TestEqual(
		TEXT("Weak-point damage is committed through GAS Health"),
		Enemy->GetCurrentHealth(),
		100.0f - ExpectedWeakPointDamage);

	FOutlierTaggedDamageEvent ExplosionDamageEvent;
	ExplosionDamageEvent.DamageTag = OutlierGameplayTags::Damage::Explosion();
	Enemy->TakeDamage(Enemy->GetCurrentHealth(), ExplosionDamageEvent, nullptr, Enemy);
	TestEqual(TEXT("Lethal Enemy damage clamps GAS Health to zero"), AbilitySystem->GetNumericAttribute(
		UOutlierVitalAttributeSet::GetHealthAttribute()), 0.0f);
	TestTrue(
		TEXT("Lethal Enemy damage grants the authoritative dead tag"),
		AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()));
	TestTrue(TEXT("Enemy death enters the existing destruction path"), Enemy->IsActorBeingDestroyed());

	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasShooterDamageFlowTest,
	"Outlier.GAS.Shooter.DamageFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasShooterDamageFlowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Shooter damage world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass
		? World->SpawnActor<AShooterCharacter>(ShooterClass)
		: nullptr;
	if (!TestNotNull(TEXT("Shooter spawns for GAS damage flow"), Shooter))
	{
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	if (!Shooter->HasActorBegunPlay())
	{
		Shooter->DispatchBeginPlay();
	}

	UOutlierAbilitySystemComponent* AbilitySystem = Shooter->GetOutlierAbilitySystemComponent();
	AbilitySystem->InitializeForPawn(Shooter);
	AbilitySystem->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 5.0f);
	AbilitySystem->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 5.0f);
	TestTrue(
		TEXT("Partner shield grant applies through a GameplayEffect"),
		AbilitySystem->ApplyPartnerShieldDeltaToSelf(20.0f, 20.0f));
	TestTrue(
		TEXT("Shield recovery GameplayEffect applies"),
		AbilitySystem->ApplyShieldRecoveryToSelf(20.0f));
	TestEqual(TEXT("Shield recovery cannot exceed MaxShield"), Shooter->GetCurShield(), 5.0f);
	TestTrue(
		TEXT("Partner shield decay GameplayEffect applies"),
		AbilitySystem->ApplyPartnerShieldDeltaToSelf(-25.0f, 0.0f));
	TestEqual(TEXT("Partner shield cannot decay below zero"), Shooter->GetCurPartnerShield(), 0.0f);
	TestTrue(
		TEXT("Partner shield can be restored after the underflow clamp"),
		AbilitySystem->ApplyPartnerShieldDeltaToSelf(20.0f, 0.0f));

	TestTrue(
		TEXT("Damage GameplayEffect applies on authority"),
		AbilitySystem->ApplyDamageToSelf(30.0f, nullptr, Shooter, OutlierGameplayTags::Damage::Weapon()));
	TestEqual(TEXT("Partner shield absorbs first"), Shooter->GetCurPartnerShield(), 0.0f);
	TestEqual(TEXT("Personal shield absorbs second"), Shooter->GetCurShield(), 0.0f);
	TestEqual(TEXT("Remaining damage reaches health"), Shooter->GetCurHealth(), 95.0f);

	TestTrue(
		TEXT("Lethal damage GameplayEffect applies"),
		AbilitySystem->ApplyDamageToSelf(95.0f, nullptr, Shooter, OutlierGameplayTags::Damage::Explosion()));
	TestEqual(TEXT("Lethal damage clamps health to zero"), Shooter->GetCurHealth(), 0.0f);
	TestTrue(
		TEXT("Health depletion grants the authoritative dead tag"),
		AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()));

	Shooter->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();

	return true;
}

#endif
