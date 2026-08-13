#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"

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

	TestFalse(
		TEXT("Native ASC cannot be added again through Blueprint Add Component"),
		UOutlierAbilitySystemComponent::StaticClass()->HasMetaData(TEXT("BlueprintSpawnableComponent")));

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

	const UOutlierVitalAttributeSet* Vital = GetDefault<UOutlierVitalAttributeSet>();
	const UOutlierShieldAttributeSet* Shield = GetDefault<UOutlierShieldAttributeSet>();

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
	FOutlierGasFoundationLegacyAuthorityBoundaryTest,
	"Outlier.GAS.Foundation.LegacyAuthorityBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierGasFoundationLegacyAuthorityBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const AEnemyBase* Enemy = GetDefault<AEnemyBase>();
	TestEqual(
		TEXT("Enemy legacy CurrentHealth remains independent during the topology slice"),
		Enemy->GetCurrentHealth(),
		0.0f);
	TestEqual(
		TEXT("Enemy GAS Health is only its untouched foundation default"),
		Enemy->GetVitalAttributeSet()->GetHealth(),
		100.0f);

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
