#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Drone/Partner/HackableComponent.h"
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
#include "Explosion/ExplosionComponent.h"
#include "Explosion/ExplosiveProp.h"
#include "Explosion/ExplosionTypes.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
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

	float ApplyDamageRequest(
		AActor* TargetActor,
		float DamageAmount,
		FOutlierDamageRequest DamageRequest,
		AController* EventInstigator,
		AActor* DamageCauser)
	{
		DamageRequest.DamageAmount = DamageAmount;
		DamageRequest.EventInstigator = EventInstigator;
		DamageRequest.DamageCauser = DamageCauser;
		return OutlierDamage::Apply(TargetActor, DamageRequest);
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
	FOutlierGasEnemyPossessPendingStateTest,
	"Outlier.GAS.Enemy.PossessPendingState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasEnemyPossessPendingStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Enemy possession pending world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Enemy possession pending world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass());
	if (!TestNotNull(TEXT("Enemy spawns for possession pending GAS state"), Enemy)
		|| !TestNotNull(TEXT("Partner spawns for possession pending GAS state"), Partner))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	if (!Enemy->HasActorBegunPlay())
	{
		Enemy->DispatchBeginPlay();
	}
	// This fixture only needs the Partner as the possession instigator. Initializing
	// its ASC directly avoids running the production BeginPlay DataTable contract.
	Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);

	UOutlierAbilitySystemComponent* EnemyASC = Enemy->GetOutlierAbilitySystemComponent();
	TestNotNull(TEXT("Enemy owns an ASC for possession pending"), EnemyASC);
	const FGameplayTag PossessPendingTag = OutlierGameplayTags::State::PossessPending();
	TestFalse(TEXT("Enemy starts without PossessPending"), Enemy->IsPossessionInProgress());
	TestEqual(TEXT("Enemy starts with no PossessPending GAS tag"), EnemyASC->GetGameplayTagCount(PossessPendingTag), 0);
	TestFalse(
		TEXT("PossessPending is no longer written into HackTags"),
		Enemy->GetHackableComponent()->HasHackTag(PossessPendingTag));

	FHackQueryContext QueryContext;
	QueryContext.InstigatorActor = Partner;
	Enemy->HandleHackStarted(QueryContext);
	TestTrue(TEXT("Hack start applies PossessPending through ASC"), Enemy->IsPossessionInProgress());
	TestEqual(TEXT("PossessPending GE grants exactly one tag"), EnemyASC->GetGameplayTagCount(PossessPendingTag), 1);
	TestFalse(
		TEXT("HackTags remain free of the GAS-owned PossessPending tag"),
		Enemy->GetHackableComponent()->HasHackTag(PossessPendingTag));

	Enemy->HandleHackStarted(QueryContext);
	TestEqual(TEXT("Repeated hack start does not stack PossessPending"), EnemyASC->GetGameplayTagCount(PossessPendingTag), 1);

	FHackResultContext FailedContext;
	FailedContext.InstigatorActor = Partner;
	FailedContext.TargetActor = Enemy;
	FailedContext.Result = EHackResult::Fail;
	Enemy->HandleHackCompleted(FailedContext);
	TestFalse(TEXT("Failed hack removes PossessPending"), Enemy->IsPossessionInProgress());
	TestEqual(TEXT("Failed hack removes the exact PossessPending effect"), EnemyASC->GetGameplayTagCount(PossessPendingTag), 0);

	Enemy->Destroy(true);
	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasExplosivePropVitalityTest,
	"Outlier.GAS.ExplosiveProp.Vitality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasExplosivePropVitalityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* PropTable = NewObject<UDataTable>();
	PropTable->RowStruct = FExplosivePropRow::StaticStruct();
	FExplosivePropRow PropRow;
	PropRow.MaxHP = 30.0f;
	PropRow.HitFlashDuration = 0.05f;
	PropTable->AddRow(TEXT("Default"), PropRow);

	UDataTable* ExplosionTable = NewObject<UDataTable>();
	ExplosionTable->RowStruct = FExplosionProfileRow::StaticStruct();
	FExplosionProfileRow ExplosionRow;
	ExplosionRow.ExplosionId = TEXT("TestExplosion");
	ExplosionRow.MaxDamage = 0.0f;
	ExplosionRow.OuterRadiusCm = 1.0f;
	ExplosionTable->AddRow(TEXT("Default"), ExplosionRow);

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		NAME_None,
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient ExplosiveProp vitality world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}

	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("ExplosiveProp vitality world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bDeferConstruction = true;
	AExplosiveProp* Prop = World->SpawnActor<AExplosiveProp>(
		AExplosiveProp::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Deferred ExplosiveProp spawns for vitality"), Prop))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}

	FDataTableRowHandle PropHandle = MakeDataTableRowHandle(PropTable, TEXT("Default"));
	FStructProperty* PropRowProperty = FindFProperty<FStructProperty>(
		Prop->GetClass(),
		TEXT("ExplosivePropRow"));
	if (TestNotNull(TEXT("ExplosivePropRow property is reflected"), PropRowProperty)
		&& TestTrue(TEXT("ExplosivePropRow uses FDataTableRowHandle"), PropRowProperty->Struct == FDataTableRowHandle::StaticStruct()))
	{
		*PropRowProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Prop) = PropHandle;
	}

	if (UExplosionComponent* ExplosionComponent = Prop->FindComponentByClass<UExplosionComponent>())
	{
		ExplosionComponent->SetExplosionProfileRow(MakeDataTableRowHandle(ExplosionTable, TEXT("Default")));
	}

	Prop->FinishSpawning(FTransform::Identity);
	if (!Prop->HasActorBegunPlay())
	{
		Prop->DispatchBeginPlay();
	}

	UOutlierAbilitySystemComponent* PropASC =
		Cast<UOutlierAbilitySystemComponent>(Prop->GetAbilitySystemComponent());
	TestNotNull(TEXT("ExplosiveProp owns an ASC"), PropASC);
	TestEqual(TEXT("ExplosiveProp MaxHP initializes as GAS MaxHealth"), Prop->GetCurrentHP(), 30.0f);

	FOutlierDamageRequest DamageRequest;
	DamageRequest.DamageAmount = 12.0f;
	DamageRequest.DamageTag = OutlierGameplayTags::Damage::Weapon();
	DamageRequest.DamageCauser = Prop;
	TestEqual(TEXT("ExplosiveProp accepts weapon damage through receiver"), OutlierDamage::Apply(Prop, DamageRequest), 12.0f);
	TestEqual(TEXT("ExplosiveProp damage reduces GAS Health"), Prop->GetCurrentHP(), 18.0f);

	DamageRequest.DamageAmount = 18.0f;
	TestEqual(TEXT("Lethal ExplosiveProp damage is accepted through GAS"), OutlierDamage::Apply(Prop, DamageRequest), 18.0f);
	TestEqual(TEXT("Lethal ExplosiveProp damage clamps GAS Health to zero"), Prop->GetCurrentHP(), 0.0f);
	TestTrue(TEXT("Lethal ExplosiveProp damage requests explosion"), Prop->IsExploded());

	Prop->ResetToInitialState();
	TestFalse(TEXT("Reset clears exploded state"), Prop->IsExploded());
	TestEqual(TEXT("Reset restores GAS Health from the row"), Prop->GetCurrentHP(), 30.0f);

	Prop->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
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

	FOutlierDamageRequest TaggedDamageEvent;
	TaggedDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	const float AppliedDamage = ApplyDamageRequest(
		Partner,
		25.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Partner damage request reports applied GAS damage"), AppliedDamage, 25.0f);
	TestEqual(TEXT("Partner damage request reduces GAS Health"), Partner->GetVitalAttributeSet()->GetHealth(), 75.0f);
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

	const float LethalDamage = ApplyDamageRequest(
		Partner,
		75.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Lethal Partner damage request reports applied GAS damage"), LethalDamage, 75.0f);
	TestEqual(TEXT("Lethal Partner damage request clamps Health to zero"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);
	TestTrue(TEXT("Lethal Partner damage request enters Reboot"), VitalityComponent->IsRebooting());
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

	const float RejectedDamage = ApplyDamageRequest(
		Partner,
		10.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Partner damage request reports zero when GAS immunity rejects damage"), RejectedDamage, 0.0f);
	TestEqual(TEXT("Rejected Partner damage leaves Health unchanged"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);

	TestTrue(TEXT("Explicit Reboot removal completes Partner recovery"), VitalityComponent->RemoveRebootEffect());
	TestTrue(TEXT("Recovered Partner accepts input again"), Partner->CanAcceptInput());
	TestEqual(TEXT("Recovered Partner restores Health to MaxHealth"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);

	Partner->SetEnemyPossessionProtection(true);
	TestEqual(
		TEXT("Possession protection grants one damage immunity tag"),
		AbilitySystem->GetGameplayTagCount(OutlierGameplayTags::State::DamageImmune()),
		1);
	const float ProtectedDamage = ApplyDamageRequest(
		Partner,
		10.0f,
		TaggedDamageEvent,
		nullptr,
		Partner);
	TestEqual(TEXT("Possession protection makes damage request report zero"), ProtectedDamage, 0.0f);
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

	const FActiveGameplayEffectHandle StealthHandle = AbilitySystem->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterStealthGameplayEffect::StaticClass(), 1.0f, Partner);
	TestTrue(TEXT("Stealth state applies for target rules"), StealthHandle.IsValid());
	TestTrue(
		TEXT("Exact Stealthed tag makes Partner unavailable"),
		OutlierEnemyTargetRules::IsUnavailable(Partner));
	TestTrue(TEXT("Exact Stealthed effect removal succeeds"), AbilitySystem->RemoveActiveEffectFromSelf(StealthHandle));
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
	TestTrue(TEXT("BulletReflecting state tag is registered"), OutlierGameplayTags::State::BulletReflecting().IsValid());
	TestTrue(TEXT("WeaponOvercharged state tag is registered"), OutlierGameplayTags::State::WeaponOvercharged().IsValid());
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
	FOutlierDamageRequest WeaponDamageEvent;
	WeaponDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	WeaponDamageEvent.HitResult.Component = CoreHitbox;
	TestEqual(
		TEXT("Enemy-team weapon damage cannot damage another Enemy-team actor"),
		ApplyDamageRequest(Enemy, 10.0f, WeaponDamageEvent, nullptr, Enemy),
		0.0f);
	FOutlierDamageRequest FriendlyExplosionDamageEvent;
	FriendlyExplosionDamageEvent.DamageTag = OutlierGameplayTags::Damage::Explosion();
	TestEqual(
		TEXT("Enemy-team explosion damage cannot damage another Enemy-team actor"),
		ApplyDamageRequest(Enemy, 10.0f, FriendlyExplosionDamageEvent, nullptr, Enemy),
		0.0f);
	TestEqual(TEXT("Rejected Enemy friendly fire preserves GAS Health"), Enemy->GetCurrentHealth(), 100.0f);

	const float AppliedWeakPointDamage = ApplyDamageRequest(
		Enemy,
		10.0f,
		WeaponDamageEvent,
		nullptr,
		nullptr);
	const float ExpectedWeakPointDamage = 10.0f * Enemy->GetWeakPointDamageMultiplier(CoreHitbox);
	TestTrue(TEXT("Core hit is configured as a weak point"), ExpectedWeakPointDamage > 10.0f);
	TestEqual(TEXT("Core weak-point multiplier is applied"), AppliedWeakPointDamage, ExpectedWeakPointDamage);
	TestEqual(
		TEXT("Weak-point damage is committed through GAS Health"),
		Enemy->GetCurrentHealth(),
		100.0f - ExpectedWeakPointDamage);

	FOutlierDamageRequest ExplosionDamageEvent;
	ExplosionDamageEvent.DamageTag = OutlierGameplayTags::Damage::Explosion();
	ApplyDamageRequest(Enemy, Enemy->GetCurrentHealth(), ExplosionDamageEvent, nullptr, nullptr);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasShooterSuitDataContractTest,
	"Outlier.GAS.ShooterSuit.DataContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasShooterSuitDataContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FOutlierShooterSuitAbilityDataRow::StaticStruct();

	FOutlierShooterSuitAbilityDataRow Common;
	Common.MaxPartnerDistance = 500.0f;
	Table->AddRow(TEXT("Common"), Common);
	FOutlierShooterSuitAbilityDataRow QuantumLeap;
	QuantumLeap.CastTimeSeconds = 1.0f;
	QuantumLeap.CooldownSeconds = 8.0f;
	QuantumLeap.PartnerOffset = 50.0f;
	Table->AddRow(TEXT("QuantumLeap"), QuantumLeap);
	FOutlierShooterSuitAbilityDataRow BulletReflection;
	BulletReflection.DurationSeconds = 4.0f;
	BulletReflection.CooldownSeconds = 20.0f;
	BulletReflection.ReflectionRadius = 2000.0f;
	Table->AddRow(TEXT("BulletReflection"), BulletReflection);
	FOutlierShooterSuitAbilityDataRow Stealth;
	Stealth.DurationSeconds = 5.0f;
	Stealth.CooldownSeconds = 20.0f;
	Table->AddRow(TEXT("Stealth"), Stealth);
	FOutlierShooterSuitAbilityDataRow WeaponOvercharge;
	WeaponOvercharge.DurationSeconds = 8.0f;
	WeaponOvercharge.CooldownSeconds = 25.0f;
	WeaponOvercharge.ShieldDrainPerSecond = 12.5f;
	WeaponOvercharge.FireRateMultiplier = 1.25f;
	WeaponOvercharge.SpreadMultiplier = 0.5f;
	WeaponOvercharge.ShieldRecoveryDelay = 3.0f;
	Table->AddRow(TEXT("WeaponOvercharge"), WeaponOvercharge);

	FOutlierShooterSuitConfig Config;
	FString Error;
	TestTrue(TEXT("Exact Shooter suit table resolves"), OutlierShooterSuitData::TryResolveConfiguration(Table, Config, Error));
	TestEqual(TEXT("Stealth duration comes from the table"), Config.Stealth.DurationSeconds, 5.0f);
	TestEqual(TEXT("Stealth cooldown comes from the table"), Config.Stealth.CooldownSeconds, 20.0f);
	TestEqual(TEXT("Common boundary comes from the table"), Config.MaxPartnerDistance, 500.0f);

	Table->RemoveRow(TEXT("WeaponOvercharge"));
	TestFalse(TEXT("Missing required row fails validation"), OutlierShooterSuitData::TryResolveConfiguration(Table, Config, Error));
	Table->AddRow(TEXT("WeaponOvercharge"), WeaponOvercharge);
	Stealth.DurationSeconds = 0.0f;
	Table->AddRow(TEXT("Stealth"), Stealth);
	TestFalse(TEXT("Non-positive gameplay value fails validation"), OutlierShooterSuitData::TryResolveConfiguration(Table, Config, Error));

	UDataTable* DiskTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/Datas/Character/Ability/Shooter/DT_AbilityShooter.DT_AbilityShooter"));
	if (!TestNotNull(TEXT("Shooter suit DataTable asset exists on disk"), DiskTable))
	{
		return false;
	}
	TestTrue(TEXT("Disk Shooter suit table satisfies the exact schema"), OutlierShooterSuitData::TryResolveConfiguration(DiskTable, Config, Error));
	TestEqual(TEXT("Disk Quantum Leap cooldown is 8 seconds"), Config.QuantumLeap.CooldownSeconds, 8.0f);
	TestEqual(TEXT("Disk Quantum Leap cast time is 1 second"), Config.QuantumLeap.CastTimeSeconds, 1.0f);
	TestEqual(TEXT("Disk Quantum Leap Partner offset is 50 centimeters"), Config.QuantumLeap.PartnerOffset, 50.0f);
	TestEqual(TEXT("Disk Bullet Reflection radius is 20 meters"), Config.BulletReflection.ReflectionRadius, 2000.0f);
	TestEqual(TEXT("Disk Stealth duration is 5 seconds"), Config.Stealth.DurationSeconds, 5.0f);
	TestEqual(TEXT("Disk Stealth cooldown is 20 seconds"), Config.Stealth.CooldownSeconds, 20.0f);
	TestEqual(TEXT("Disk Weapon Overcharge shield drain is 12.5 per second"), Config.WeaponOvercharge.ShieldDrainPerSecond, 12.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasShooterStealthLifecycleTest,
	"Outlier.GAS.ShooterSuit.StealthLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasShooterStealthLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Shooter stealth world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Shooter stealth world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	APartnerCharacter* Partner = PartnerClass ? World->SpawnActor<APartnerCharacter>(PartnerClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter spawns for stealth lifecycle"), Shooter)
		|| !TestNotNull(TEXT("Partner spawns for stealth lifecycle"), Partner))
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
	if (!Partner->HasActorBegunPlay())
	{
		Partner->DispatchBeginPlay();
	}
	Shooter->SetPartnerCharacter(Partner);
	Partner->SetShooterCharacter(Shooter);
	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->InitializeForPawn(Partner);
	TestTrue(TEXT("Shooter suit abilities are configured from the Blueprint DataTable"), ShooterASC->IsShooterSuitConfigured());
	TestTrue(
		TEXT("Stealth activates through the authoritative Shooter ASC"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	TestTrue(TEXT("Shooter receives exact Stealthed state"), ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()));
	TestTrue(TEXT("Partner receives exact Stealthed state"), PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()));
	TestFalse(TEXT("Cooldown does not run while Stealth is active"), ShooterASC->IsShooterStealthCooldownActive());
	TestFalse(
		TEXT("Active Stealth blocks Quantum Leap through the shared suit exclusion"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()));
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 20.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 20.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 10.0f);
	TestTrue(TEXT("Non-damage Partner shield decay does not break Stealth"), Shooter->IsStealthed());
	FOutlierDamageRequest NonLethalDamageEvent;
	NonLethalDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	TestEqual(
		TEXT("Incoming damage is applied while Stealth remains active"),
		ApplyDamageRequest(Shooter, 5.0f, NonLethalDamageEvent, nullptr, Shooter),
		5.0f);
	TestTrue(TEXT("Incoming damage does not break Stealth"), Shooter->IsStealthed());
	Shooter->NotifyStealthDetected();
	TestTrue(TEXT("Detector notification does not break Stealth"), Shooter->IsStealthed());
	Shooter->SetSuitDisabledByPartnerBoundary(true);
	TestTrue(TEXT("Crossing the Partner boundary does not break an active Stealth"), Shooter->IsStealthed());
	Shooter->SetSuitDisabledByPartnerBoundary(false);
	Partner->StopActionsForReboot();
	TestTrue(TEXT("Partner Reboot cleanup does not break Shooter Stealth"), Shooter->IsStealthed());

	TestTrue(
		TEXT("Pressing Stealth again intentionally cancels the active Stealth"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	TestFalse(TEXT("Shooter Stealthed state is removed together"), ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()));
	TestFalse(TEXT("Partner Stealthed state is removed together"), PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()));
	TestTrue(TEXT("Full configured cooldown starts only after Stealth ends"), ShooterASC->IsShooterStealthCooldownActive());
	TestTrue(TEXT("Stealth cooldown is approximately 20 seconds"), ShooterASC->GetShooterStealthCooldownRemaining() > 19.0f);
	TestFalse(
		TEXT("Cooldown blocks Stealth reactivation"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));

	FGameplayTagContainer StealthCooldownTags;
	StealthCooldownTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::Stealth());
	TestEqual(
		TEXT("Test fixture removes the first exact Stealth cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(StealthCooldownTags),
		1);
	TestTrue(
		TEXT("Stealth reactivates after its cooldown is removed"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	for (int32 TickIndex = 0; TickIndex < 260; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.02f);
	}
	TestFalse(TEXT("Natural expiry removes Shooter Stealth"), ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()));
	TestFalse(TEXT("Natural expiry removes Partner Stealth together"), PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()));
	TestTrue(TEXT("Natural expiry starts the full configured cooldown"), ShooterASC->IsShooterStealthCooldownActive());

	TestEqual(
		TEXT("Test fixture removes natural-expiry cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(StealthCooldownTags),
		1);
	TestTrue(
		TEXT("Stealth activates for Partner attack break coverage"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	Partner->NotifyOffensiveActionExecuted();
	TestFalse(TEXT("Partner attack removes paired Stealth"), Shooter->IsStealthed());
	TestTrue(TEXT("Partner attack starts full cooldown"), ShooterASC->IsShooterStealthCooldownActive());

	TestEqual(
		TEXT("Test fixture removes Partner attack cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(StealthCooldownTags),
		1);
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 0.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 0.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 0.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 0.0f);
	TestTrue(
		TEXT("Stealth activates for lethal damage ordering coverage"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	FOutlierDamageRequest LethalDamageEvent;
	LethalDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	TestEqual(
		TEXT("Lethal damage applies through the Shooter damage boundary while Stealth is active"),
		ApplyDamageRequest(Shooter, 100.0f, LethalDamageEvent, nullptr, Shooter),
		100.0f);
	TestFalse(TEXT("Death cleanup removes Stealth without committing a gameplay cooldown"), ShooterASC->IsShooterStealthCooldownActive());
	TestFalse(TEXT("Death cleanup removes the active Stealth state"), Shooter->IsStealthed());
	TestTrue(TEXT("Lethal damage applies Dead after Stealth cleanup"), Shooter->IsDead());

	Shooter->Destroy(true);
	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasShooterBulletReflectionTest,
	"Outlier.GAS.ShooterSuit.BulletReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasShooterBulletReflectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Bullet Reflection world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Bullet Reflection world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	UClass* EnemyClass = LoadClass<AEnemyBase>(
		nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	APartnerCharacter* Partner = PartnerClass ? World->SpawnActor<APartnerCharacter>(PartnerClass) : nullptr;
	AEnemyBase* Enemy = EnemyClass ? World->SpawnActor<AEnemyBase>(EnemyClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter spawns for Bullet Reflection"), Shooter)
		|| !TestNotNull(TEXT("Partner spawns for Bullet Reflection"), Partner)
		|| !TestNotNull(TEXT("Enemy spawns for Bullet Reflection"), Enemy))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	if (!Shooter->HasActorBegunPlay()) Shooter->DispatchBeginPlay();
	if (!Partner->HasActorBegunPlay()) Partner->DispatchBeginPlay();
	if (!Enemy->HasActorBegunPlay()) Enemy->DispatchBeginPlay();
	Shooter->SetPartnerCharacter(Partner);
	Partner->SetShooterCharacter(Shooter);
	Shooter->SetActorLocation(FVector::ZeroVector);
	Partner->SetActorLocation(FVector(200.0f, 200.0f, 0.0f));
	Enemy->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));

	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* EnemyASC = Enemy->GetOutlierAbilitySystemComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->InitializeForPawn(Partner);
	EnemyASC->InitializeForPawn(Enemy);
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 0.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 0.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 0.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 0.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);
	EnemyASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	EnemyASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 100.0f);

	TestNull(TEXT("Bullet Reflection fixture starts without an equipped weapon"), Shooter->GetCurrentWeapon());
	TestTrue(
		TEXT("Bullet Reflection activates independently of the equipped weapon"),
		ShooterASC->TryActivateShooterSuitAbility(
			OutlierGameplayTags::Ability::Shooter::BulletReflection()));
	TestTrue(TEXT("Bullet Reflection grants its exact replicated state"), Shooter->IsBulletReflecting());
	TestFalse(
		TEXT("Active Bullet Reflection blocks Stealth through suit mutual exclusion"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	TestFalse(
		TEXT("Bullet Reflection cooldown does not run while active"),
		ShooterASC->IsShooterBulletReflectionCooldownActive());

	FOutlierDamageRequest WeaponDamageEvent;
	WeaponDamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
	WeaponDamageEvent.DamageOrigin = Enemy->GetActorLocation();
	WeaponDamageEvent.HitResult.ImpactPoint = Shooter->GetActorLocation();
	TestEqual(
		TEXT("Eligible enemy weapon damage is fully rejected by Shooter"),
		ApplyDamageRequest(Shooter, 25.0f, WeaponDamageEvent, nullptr, Enemy),
		0.0f);
	TestEqual(TEXT("Reflected weapon damage preserves Shooter Health"), Shooter->GetCurHealth(), 100.0f);
	TestEqual(TEXT("Reflected weapon damage applies the same amount to the source"), Enemy->GetCurrentHealth(), 75.0f);

	FOutlierDamageRequest ExplosionDamageEvent;
	ExplosionDamageEvent.DamageTag = OutlierGameplayTags::Damage::Explosion();
	ExplosionDamageEvent.DamageOrigin = Enemy->GetActorLocation();
	ExplosionDamageEvent.HitResult.ImpactPoint = Shooter->GetActorLocation();
	TestEqual(
		TEXT("Eligible enemy explosion damage is also reflected"),
		ApplyDamageRequest(Shooter, 10.0f, ExplosionDamageEvent, nullptr, Enemy),
		0.0f);
	TestEqual(TEXT("Reflected explosion applies the same amount to the source"), Enemy->GetCurrentHealth(), 65.0f);

	FOutlierDamageRequest FriendlyDamageEvent = WeaponDamageEvent;
	FriendlyDamageEvent.DamageOrigin = Partner->GetActorLocation();
	TestEqual(
		TEXT("Friendly weapon damage is reflected without a team restriction"),
		ApplyDamageRequest(Shooter, 5.0f, FriendlyDamageEvent, nullptr, Partner),
		0.0f);
	TestEqual(TEXT("Reflected friendly damage preserves Shooter Health"), Shooter->GetCurHealth(), 100.0f);
	TestEqual(
		TEXT("The friendly source can receive its reflected damage"),
		Partner->GetVitalAttributeSet()->GetHealth(),
		95.0f);

	FOutlierDamageRequest ReflectedDamageEvent = WeaponDamageEvent;
	ReflectedDamageEvent.bReflectedDamage = true;
	TestEqual(
		TEXT("Already-reflected damage cannot recurse through Bullet Reflection"),
		ApplyDamageRequest(Shooter, 5.0f, ReflectedDamageEvent, nullptr, Enemy),
		5.0f);
	TestEqual(TEXT("Non-recursive reflected damage follows the normal damage path"), Shooter->GetCurHealth(), 95.0f);

	FOutlierDamageRequest OutOfRangeDamageEvent = WeaponDamageEvent;
	OutOfRangeDamageEvent.DamageOrigin = FVector(2500.0f, 0.0f, 0.0f);
	TestEqual(
		TEXT("Enemy damage outside the configured 20 meter radius is not reflected"),
		ApplyDamageRequest(Shooter, 5.0f, OutOfRangeDamageEvent, nullptr, Enemy),
		5.0f);
	TestEqual(TEXT("Out-of-range damage reaches Shooter Health"), Shooter->GetCurHealth(), 90.0f);

	TestTrue(TEXT("Gameplay end removes Bullet Reflection"), Shooter->EndActiveBulletReflection(true));
	TestFalse(TEXT("Bullet Reflection state is removed on end"), Shooter->IsBulletReflecting());
	TestTrue(
		TEXT("Full configured cooldown starts after Bullet Reflection ends"),
		ShooterASC->IsShooterBulletReflectionCooldownActive());
	TestTrue(
		TEXT("Bullet Reflection cooldown is approximately 20 seconds"),
		ShooterASC->GetShooterBulletReflectionCooldownRemaining() > 19.0f);

	FGameplayTagContainer ReflectionCooldownTags;
	ReflectionCooldownTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::BulletReflection());
	TestEqual(
		TEXT("Fixture removes the first exact Bullet Reflection cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(ReflectionCooldownTags),
		1);
	TestTrue(
		TEXT("Bullet Reflection reactivates after cooldown removal"),
		ShooterASC->TryActivateShooterSuitAbility(
			OutlierGameplayTags::Ability::Shooter::BulletReflection()));
	for (int32 TickIndex = 0; TickIndex < 210; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.02f);
	}
	TestFalse(TEXT("Natural four-second expiry removes Bullet Reflection"), Shooter->IsBulletReflecting());
	TestTrue(
		TEXT("Natural expiry starts the full configured cooldown"),
		ShooterASC->IsShooterBulletReflectionCooldownActive());

	Shooter->Destroy(true);
	Partner->Destroy(true);
	Enemy->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasShooterWeaponOverchargeTest,
	"Outlier.GAS.ShooterSuit.WeaponOvercharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasShooterWeaponOverchargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Weapon Overcharge world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Weapon Overcharge world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(
		nullptr, TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	APartnerCharacter* Partner = PartnerClass ? World->SpawnActor<APartnerCharacter>(PartnerClass) : nullptr;
	ARangedWeaponBase* Rifle = RifleClass ? World->SpawnActor<ARangedWeaponBase>(RifleClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter spawns for Weapon Overcharge"), Shooter)
		|| !TestNotNull(TEXT("Partner spawns for Weapon Overcharge"), Partner)
		|| !TestNotNull(TEXT("Primary rifle spawns for Weapon Overcharge"), Rifle))
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	if (!Shooter->HasActorBegunPlay()) Shooter->DispatchBeginPlay();
	if (!Partner->HasActorBegunPlay()) Partner->DispatchBeginPlay();
	if (!Rifle->HasActorBegunPlay()) Rifle->DispatchBeginPlay();
	Shooter->SetPartnerCharacter(Partner);
	Partner->SetShooterCharacter(Shooter);

	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->InitializeForPawn(Partner);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 40.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 5.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 5.0f);

	TestFalse(
		TEXT("Weapon Overcharge rejects activation without a primary rifle"),
		ShooterASC->TryActivateShooterSuitAbility(
			OutlierGameplayTags::Ability::Shooter::WeaponOvercharge()));
	Shooter->EquipWeapon(Rifle);
	TestEqual(TEXT("Rifle is equipped as the primary weapon"), Shooter->GetWeaponMode(), EWeaponMode::Primary);
	const FActiveGameplayEffectHandle PartnerRebootHandle = PartnerASC->ApplyRebootStateToSelf(1.0f);
	TestTrue(TEXT("Partner Reboot fixture effect is applied"), PartnerRebootHandle.IsValid());
	TestTrue(
		TEXT("Partner Reboot disables the Shooter suit even inside the distance boundary"),
		Shooter->IsShooterSuitUseDisabled());
	const TArray<FGameplayTag> SuitAbilitiesBlockedByPartnerReboot = {
		OutlierGameplayTags::Ability::Shooter::QuantumLeap(),
		OutlierGameplayTags::Ability::Shooter::BulletReflection(),
		OutlierGameplayTags::Ability::Shooter::WeaponOvercharge(),
		OutlierGameplayTags::Ability::Shooter::Stealth()
	};
	for (const FGameplayTag& AbilityTag : SuitAbilitiesBlockedByPartnerReboot)
	{
		TestFalse(
			*FString::Printf(TEXT("Partner Reboot blocks %s"), *AbilityTag.ToString()),
			ShooterASC->TryActivateShooterSuitAbility(AbilityTag));
	}
	TestTrue(
		TEXT("Partner Reboot fixture effect is removed"),
		PartnerASC->RemoveActiveEffectFromSelf(PartnerRebootHandle));
	TestFalse(
		TEXT("Removing Partner Reboot restores Shooter suit availability inside the boundary"),
		Shooter->IsShooterSuitUseDisabled());
	Rifle->ConsumeAmmo();
	Rifle->BeginReload();
	TestTrue(TEXT("Fixture begins a rifle reload"), Rifle->IsReloading());
	const float BaseSpread = Rifle->GetCurrentSpread();
	TestTrue(
		TEXT("Weapon Overcharge activates with a primary rifle and valid Partner"),
		ShooterASC->TryActivateShooterSuitAbility(
			OutlierGameplayTags::Ability::Shooter::WeaponOvercharge()));
	TestTrue(TEXT("Weapon Overcharge grants its exact state"), Shooter->IsWeaponOvercharged());
	TestFalse(TEXT("Activation cancels the in-progress reload"), Rifle->IsReloading());
	TestEqual(TEXT("Activation immediately fills the magazine"), Rifle->GetCurrentAmmo(), Rifle->GetMagazineSize());
	TestEqual(TEXT("Activation fills only the base Shield"), Shooter->GetCurShield(), 100.0f);
	TestEqual(TEXT("Activation preserves Partner Shield"), Shooter->GetCurPartnerShield(), 5.0f);
	TestFalse(TEXT("Reload input is ignored while overcharged"), Rifle->CanReload());
	Rifle->ConsumeAmmo();
	TestEqual(TEXT("Shots do not consume magazine ammo while overcharged"), Rifle->GetCurrentAmmo(), Rifle->GetMagazineSize());
	TestTrue(
		TEXT("Active overcharge applies the configured spread multiplier"),
		FMath::IsNearlyEqual(Rifle->GetCurrentSpread(), BaseSpread * 0.5f, 0.01f));
	TestFalse(
		TEXT("Active Weapon Overcharge blocks other suit abilities"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	TestFalse(
		TEXT("Weapon Overcharge cooldown does not run while active"),
		ShooterASC->IsShooterWeaponOverchargeCooldownActive());

	for (int32 TickIndex = 0; TickIndex < 24; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	}
	TestEqual(TEXT("Shield drain consumes Partner Shield first"), Shooter->GetCurPartnerShield(), 0.0f);
	TestTrue(
		TEXT("Shield drain continues into base Shield after Partner Shield"),
		Shooter->GetCurShield() < 92.0f && Shooter->GetCurShield() > 88.0f);

	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 0.25f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 0.25f);
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.05f);
	TestFalse(TEXT("Shield depletion ends Weapon Overcharge immediately"), Shooter->IsWeaponOvercharged());
	TestTrue(
		TEXT("Full configured cooldown starts after Weapon Overcharge ends"),
		ShooterASC->IsShooterWeaponOverchargeCooldownActive());
	TestTrue(
		TEXT("Weapon Overcharge cooldown is approximately 25 seconds"),
		ShooterASC->GetShooterWeaponOverchargeCooldownRemaining() > 24.0f);
	FGameplayTagContainer OverchargeCooldownTags;
	OverchargeCooldownTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge());
	TestEqual(
		TEXT("Fixture removes the first exact Weapon Overcharge cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(OverchargeCooldownTags),
		1);
	TestTrue(
		TEXT("Weapon Overcharge reactivates after cooldown removal"),
		ShooterASC->TryActivateShooterSuitAbility(
			OutlierGameplayTags::Ability::Shooter::WeaponOvercharge()));
	for (int32 TickIndex = 0; TickIndex < 165; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	}
	TestFalse(TEXT("Natural eight-second expiry removes Weapon Overcharge"), Shooter->IsWeaponOvercharged());
	TestTrue(
		TEXT("Natural expiry starts the full configured cooldown"),
		ShooterASC->IsShooterWeaponOverchargeCooldownActive());

	Rifle->Destroy(true);
	Shooter->Destroy(true);
	Partner->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierGasShooterQuantumLeapTest,
	"Outlier.GAS.ShooterSuit.QuantumLeap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasShooterQuantumLeapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FName WorldName = MakeUniqueObjectName(
		nullptr, UWorld::StaticClass(), NAME_None, EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Transient Quantum Leap world is created"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	TestTrue(TEXT("Quantum Leap world creates an authority game mode"), World->SetGameMode(FURL()));
	World->InitializeActorsForPlay(FURL());

	UClass* ShooterClass = LoadClass<AShooterCharacter>(
		nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(
		nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	APartnerCharacter* Partner = PartnerClass ? World->SpawnActor<APartnerCharacter>(PartnerClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter spawns for Quantum Leap"), Shooter)
		|| !TestNotNull(TEXT("Partner spawns for Quantum Leap"), Partner))
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
	if (!Partner->HasActorBegunPlay())
	{
		Partner->DispatchBeginPlay();
	}
	Shooter->SetPartnerCharacter(Partner);
	Partner->SetShooterCharacter(Shooter);
	Partner->SetActorEnableCollision(false);
	Shooter->SetActorLocationAndRotation(FVector::ZeroVector, FRotator(0.0f, 45.0f, 0.0f));
	Partner->SetActorLocationAndRotation(FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);

	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->InitializeForPawn(Partner);
	TestEqual(TEXT("Four implemented Shooter suit abilities are granted exactly once"), ShooterASC->GetGrantedShooterSuitAbilityCount(), 4);

	AActor* Blocker = World->SpawnActor<AActor>();
	UBoxComponent* BlockingBox = Blocker ? NewObject<UBoxComponent>(Blocker) : nullptr;
	if (TestNotNull(TEXT("Blocked-destination fixture is created"), Blocker)
		&& TestNotNull(TEXT("Blocked-destination collision is created"), BlockingBox))
	{
		Blocker->SetRootComponent(BlockingBox);
		BlockingBox->SetBoxExtent(FVector(140.0f, 140.0f, 200.0f));
		BlockingBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockingBox->SetCollisionResponseToAllChannels(ECR_Block);
		BlockingBox->RegisterComponent();
		Blocker->SetActorLocation(Partner->GetActorLocation());
	}
	TestTrue(
		TEXT("A valid Quantum Leap input reaches the authoritative attempt even when all destinations are blocked"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()));
	TestFalse(
		TEXT("Blocked Quantum Leap ends without leaving damage immunity active"),
		ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestTrue(TEXT("Blocked Quantum Leap starts a failure cooldown"), ShooterASC->IsShooterQuantumLeapCooldownActive());
	const float FailureCooldown = ShooterASC->GetShooterQuantumLeapCooldownRemaining();
	TestTrue(TEXT("Quantum Leap failure cooldown is half of 8 seconds"), FailureCooldown > 3.5f && FailureCooldown <= 4.0f);

	FGameplayTagContainer QuantumCooldownTags;
	QuantumCooldownTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::QuantumLeap());
	TestEqual(
		TEXT("Test fixture removes the failed-attempt Quantum Leap cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(QuantumCooldownTags),
		1);
	if (Blocker)
	{
		Blocker->SetActorEnableCollision(false);
		Blocker->Destroy();
	}

	const FVector SnapshotDestination(250.0f, 0.0f, 0.0f);
	const FRotator PreservedRotation = Shooter->GetActorRotation();
	TestTrue(
		TEXT("Quantum Leap starts when the Partner is in range and a destination is clear"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()));
	TestTrue(
		TEXT("Quantum Leap grants exact damage immunity during its cast"),
		ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestFalse(
		TEXT("An active Quantum Leap blocks Stealth through the shared suit exclusion"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth()));
	TestFalse(
		TEXT("Damage is rejected while Quantum Leap is casting"),
		ShooterASC->ApplyDamageToSelf(10.0f, nullptr, Shooter, OutlierGameplayTags::Damage::Weapon()));
	Partner->SetActorLocation(FVector(400.0f, 0.0f, 0.0f));
	for (int32 TickIndex = 0; TickIndex < 55; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.02f);
	}
	TestTrue(
		TEXT("Quantum Leap uses the Partner position captured when the cast started"),
		Shooter->GetActorLocation().Equals(SnapshotDestination, 1.0f));
	TestTrue(TEXT("Quantum Leap preserves Shooter facing"), Shooter->GetActorRotation().Equals(PreservedRotation, 0.01f));
	TestFalse(
		TEXT("Quantum Leap removes its exact damage immunity after relocation"),
		ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestTrue(TEXT("Successful Quantum Leap starts its full cooldown"), ShooterASC->IsShooterQuantumLeapCooldownActive());
	TestTrue(
		TEXT("Successful Quantum Leap cooldown is approximately 8 seconds"),
		ShooterASC->GetShooterQuantumLeapCooldownRemaining() > 7.0f);
	TestFalse(
		TEXT("Quantum Leap cooldown blocks reactivation"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()));

	TestEqual(
		TEXT("Test fixture removes the successful Quantum Leap cooldown"),
		ShooterASC->RemoveActiveEffectsWithGrantedTags(QuantumCooldownTags),
		1);
	Shooter->SetActorLocation(FVector::ZeroVector);
	Partner->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
	TestFalse(
		TEXT("Out-of-range Quantum Leap is rejected before an attempt starts"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()));
	TestFalse(
		TEXT("An invalid out-of-range input does not start a failure cooldown"),
		ShooterASC->IsShooterQuantumLeapCooldownActive());

	Partner->SetActorLocation(FVector(300.0f, 0.0f, 0.0f));
	TestTrue(
		TEXT("Quantum Leap starts for Partner teardown cleanup coverage"),
		ShooterASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::QuantumLeap()));
	TestTrue(
		TEXT("Teardown coverage starts with cast damage immunity"),
		ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	Partner->Destroy(true);
	TestFalse(
		TEXT("Partner teardown removes Quantum Leap cast damage immunity"),
		ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()));
	TestFalse(
		TEXT("Partner teardown does not start a Quantum Leap cooldown"),
		ShooterASC->IsShooterQuantumLeapCooldownActive());
	for (int32 TickIndex = 0; TickIndex < 55; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.02f);
	}
	TestTrue(
		TEXT("Partner teardown leaves Shooter at the pre-cast location"),
		Shooter->GetActorLocation().Equals(FVector::ZeroVector, 1.0f));
	TestFalse(
		TEXT("Partner teardown remains cooldown-free after the former cast delay"),
		ShooterASC->IsShooterQuantumLeapCooldownActive());

	Shooter->Destroy(true);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
