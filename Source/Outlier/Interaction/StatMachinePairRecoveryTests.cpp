#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Interaction/InteractionStatMachine.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierPartnerCombatAttributeSet.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Shooter/ShooterCharacter.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStatMachinePairRecoveryTest,
	"Outlier.Interaction.StatMachine.PairRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStatMachinePairRecoveryTest::RunTest(const FString& Parameters)
{
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("StatMachineRecoveryTest"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	FActorSpawnParameters Deferred;
	Deferred.bDeferConstruction = true;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(), FTransform::Identity, Deferred);
	AInteractionStatMachine* Machine = World->SpawnActor<AInteractionStatMachine>();
	if (!TestNotNull(TEXT("Shooter"), Shooter) || !TestNotNull(TEXT("Partner"), Partner)
		|| !TestNotNull(TEXT("Machine"), Machine)) return false;
	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->AddAttributeSetSubobject(const_cast<UOutlierVitalAttributeSet*>(Partner->GetVitalAttributeSet()));
	PartnerASC->AddAttributeSetSubobject(Cast<UOutlierPartnerCombatAttributeSet>(Partner->GetDefaultSubobjectByName(TEXT("CombatAttributeSet"))));
	PartnerASC->InitializeForPawn(Partner);
	ShooterASC->InitializeVitalityToSelf(100.0f);
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
	FDataTableRowHandle VitalityHandle;
	VitalityHandle.DataTable = VitalityTable;
	VitalityHandle.RowName = TEXT("Partner");
	FDataTableRowHandle SurvivalHandle;
	SurvivalHandle.DataTable = SurvivalTable;
	SurvivalHandle.RowName = TEXT("Default");
	UPartnerVitalityComponent* Vitality = Partner->GetPartnerVitalityComponent();
	if (!TestTrue(TEXT("Partner vitality initializes"), Vitality->InitializeFromDataTables(VitalityHandle, SurvivalHandle))) return false;

	// 실제 피해로 Reboot를 시작하여, 장치가 기존 효과 제거 콜백까지 정상 실행하는지 확인한다.
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("Partner entered Reboot"), Vitality->IsRebooting());
	const FActiveGameplayEffectHandle CooldownHandle = PartnerASC->ApplyTimedGameplayEffectToSelf(
		UOutlierPartnerEMPCooldownGameplayEffect::StaticClass(), 30.0f, Partner);
	TestTrue(TEXT("Existing skill cooldown starts"), CooldownHandle.IsValid());
	const float CooldownBefore = PartnerASC->GetActiveGameplayEffect(CooldownHandle)->GetTimeRemaining(World->GetTimeSeconds());
	const FActiveGameplayEffectHandle OtherProtection = PartnerASC->ApplyDamageImmuneStateToSelf();
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 5.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 40.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 25.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 30.0f);
	TestFalse(TEXT("Invalid Pair does not consume recovery"), Machine->TryRecoverPair(Shooter, Partner, INDEX_NONE));
	TestFalse(TEXT("Rejected interaction does not recover"), Machine->Interact(Shooter));
	TestTrue(TEXT("First recovery succeeds"), Machine->TryRecoverPair(Shooter, Partner, 7));
	TestEqual(TEXT("Shooter full HP"), Shooter->GetCurHealth(), 100.0f);
	TestEqual(TEXT("Shooter full base Shield"), Shooter->GetCurShield(), 100.0f);
	TestEqual(TEXT("Additional Shield unchanged"), Shooter->GetCurPartnerShield(), 25.0f);
	TestFalse(TEXT("Partner Reboot ends immediately"), Vitality->IsRebooting());
	TestEqual(TEXT("Partner full HP"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);
	TestTrue(TEXT("Other protection remains"), PartnerASC->GetActiveGameplayEffect(OtherProtection) != nullptr);
	const FActiveGameplayEffect* RemainingCooldown = PartnerASC->GetActiveGameplayEffect(CooldownHandle);
	if (!TestNotNull(TEXT("Skill cooldown remains after recovery"), RemainingCooldown)) return false;
	TestEqual(TEXT("Recovery does not reset remaining cooldown"), RemainingCooldown->GetTimeRemaining(World->GetTimeSeconds()), CooldownBefore);
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 0.0f);
	TestFalse(TEXT("Same Pair cannot recover again"), Machine->TryRecoverPair(Shooter, Partner, 7));
	TestEqual(TEXT("Repeated request does not heal"), Shooter->GetCurHealth(), 60.0f);
	TestTrue(TEXT("Repeated request does not end new Reboot"), Vitality->IsRebooting());
	AInteractionStatMachine* SecondMachine = World->SpawnActor<AInteractionStatMachine>();
	TestTrue(TEXT("Another machine has its own recovery"), SecondMachine->TryRecoverPair(Shooter, Partner, 7));
	ShooterASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	World->Tick(LEVELTICK_All, 1.1f);
	TestEqual(TEXT("Old Reboot expiry does not heal again"), Shooter->GetCurHealth(), 60.0f);
	TestEqual(TEXT("Partner stays recovered"), Partner->GetVitalAttributeSet()->GetHealth(), 100.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	World->Tick(LEVELTICK_All, 1.1f);
	TestEqual(TEXT("Removed Reboot cannot recover later damage"), Partner->GetVitalAttributeSet()->GetHealth(), 60.0f);

	// 성공 확정 경로가 사용하는 계산을 직접 검증한다. Partner 본체의 무기 보정과 분리되어야 한다.
	FFloatProperty* BaseMultiplier = FindFProperty<FFloatProperty>(Partner->GetClass(), TEXT("BasePossessionDamageMultiplier"));
	if (!TestNotNull(TEXT("Editable Partner base multiplier"), BaseMultiplier)) return false;
	BaseMultiplier->SetPropertyValue_InContainer(Partner, 2.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierPartnerCombatAttributeSet::GetHackedDroneDamageMultiplierAttribute(), 1.0f);
	TestEqual(TEXT("No upgrade still gets base bonus"), Partner->GetPossessedWeaponDamageMultiplier(), 2.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierPartnerCombatAttributeSet::GetHackedDroneDamageMultiplierAttribute(), 1.2f);
	TestTrue(TEXT("Base and upgrade multiply"), FMath::IsNearlyEqual(Partner->GetPossessedWeaponDamageMultiplier(), 2.4f));
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(AEnemyBase::StaticClass(), FTransform::Identity, Deferred);
	if (!TestNotNull(TEXT("Enemy damage provider"), Enemy)) return false;
	FWeaponDamageModifiers EnemyModifiers;
	Enemy->SetPossessionDamageMultiplier(Partner->GetPossessedWeaponDamageMultiplier());
	Enemy->GetWeaponDamageModifiers(EnemyModifiers);
	TestEqual(TEXT("AI enemy rejects possession bonus"), EnemyModifiers.DamageMultiplier, 1.0f);
	FBoolProperty* Possessed = FindFProperty<FBoolProperty>(Enemy->GetClass(), TEXT("bIsPossessed"));
	if (!TestNotNull(TEXT("Possession state fixture"), Possessed)) return false;
	Possessed->SetPropertyValue_InContainer(Enemy, true);
	Enemy->SetPossessionDamageMultiplier(Partner->GetPossessedWeaponDamageMultiplier());
	Enemy->GetWeaponDamageModifiers(EnemyModifiers);
	TestTrue(TEXT("Possessed enemy exposes composed weapon bonus"), FMath::IsNearlyEqual(EnemyModifiers.DamageMultiplier, 2.4f));
	FWeaponDamageModifiers OwnModifiers;
	Partner->GetWeaponDamageModifiers(OwnModifiers);
	TestEqual(TEXT("Partner own attacks do not get possession bonus"), OwnModifiers.DamageMultiplier, 1.0f);
	BaseMultiplier->SetPropertyValue_InContainer(Partner, -1.0f);
	TestEqual(TEXT("Base multiplier cannot reduce damage"), Partner->GetBasePossessionDamageMultiplier(), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeVitalityIncreaseTest,
	"Outlier.Upgrade.Vitality.IncreaseOnPurchase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeVitalityIncreaseTest::RunTest(const FString& Parameters)
{
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UpgradeVitalityTest"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter"), Shooter)) return false;
	UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
	ASC->InitializeForPawn(Shooter);
	ASC->InitializeVitalityToSelf(100.0f);
	ASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 40.0f);
	ASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 25.0f);
	UDataTable* Nodes = NewObject<UDataTable>();
	Nodes->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();
	UDataTable* Effects = NewObject<UDataTable>();
	Effects->RowStruct = FOutlierUpgradeEffectRow::StaticStruct();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FName NodeName(*FString::Printf(TEXT("Vitality%d"), Index));
		FOutlierUpgradeNodeRow Node;
		Node.Role = EOutlierUpgradeRole::Shooter;
		Nodes->AddRow(NodeName, Node);
		for (const TCHAR* Tag : {TEXT("Attribute.Health.Max"), TEXT("Attribute.Shield.Max")})
		{
			FOutlierUpgradeEffectRow Effect;
			Effect.NodeRowName = NodeName;
			Effect.TargetTag = FGameplayTag::RequestGameplayTag(FName(Tag));
			Effect.Magnitude = 20.0f;
			Effects->AddRow(FName(*(NodeName.ToString() + Tag)), Effect);
		}
	}
	UOutlierUpgradeComponent* Upgrade = Shooter->GetUpgradeComponent();
	Upgrade->SetUpgradeRole(EOutlierUpgradeRole::Shooter);
	Upgrade->SetUpgradeDataTable(Nodes);
	Upgrade->SetUpgradeEffectDataTable(Effects);
	TestTrue(TEXT("First vitality node purchased"), Upgrade->TryActivateNode(TEXT("Vitality0")));
	TestEqual(TEXT("Max HP increased"), Shooter->GetMaxHealth(), 120.0f);
	TestEqual(TEXT("Missing HP stays 40"), Shooter->GetCurHealth(), 80.0f);
	TestEqual(TEXT("Missing base Shield stays 40"), Shooter->GetCurShield(), 80.0f);
	Upgrade->ReconcileUpgradeProjection();
	Upgrade->ReconcileUpgradeProjection();
	TestEqual(TEXT("Reprojection does not heal again"), Shooter->GetCurHealth(), 80.0f);
	TestEqual(TEXT("Reprojection does not fill Shield again"), Shooter->GetCurShield(), 80.0f);
	TestFalse(TEXT("Same node cannot pay twice"), Upgrade->TryActivateNode(TEXT("Vitality0")));
	ASC->RestoreHealthToMax();
	ASC->ApplyShieldRecoveryToSelf(120.0f);
	TestTrue(TEXT("Second vitality node purchased"), Upgrade->TryActivateNode(TEXT("Vitality1")));
	TestEqual(TEXT("Full HP remains full"), Shooter->GetCurHealth(), 140.0f);
	TestEqual(TEXT("Full Shield remains full"), Shooter->GetCurShield(), 140.0f);
	TestEqual(TEXT("Partner Shield unaffected"), Shooter->GetCurPartnerShield(), 25.0f);

	FActorSpawnParameters Deferred;
	Deferred.bDeferConstruction = true;
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(APartnerCharacter::StaticClass(), FTransform::Identity, Deferred);
	if (!TestNotNull(TEXT("Partner upgrade fixture"), Partner)) return false;
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	PartnerASC->AddAttributeSetSubobject(const_cast<UOutlierVitalAttributeSet*>(Partner->GetVitalAttributeSet()));
	PartnerASC->InitializeForPawn(Partner);
	PartnerASC->InitializeVitalityToSelf(100.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 60.0f);
	UDataTable* PartnerNodes = NewObject<UDataTable>();
	PartnerNodes->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();
	UDataTable* PartnerEffects = NewObject<UDataTable>();
	PartnerEffects->RowStruct = FOutlierUpgradeEffectRow::StaticStruct();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FName NodeName(*FString::Printf(TEXT("PartnerVitality%d"), Index));
		FOutlierUpgradeNodeRow Node;
		Node.Role = EOutlierUpgradeRole::Partner;
		PartnerNodes->AddRow(NodeName, Node);
		FOutlierUpgradeEffectRow Effect;
		Effect.NodeRowName = NodeName;
		Effect.TargetTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health.Max"));
		Effect.Magnitude = 20.0f;
		PartnerEffects->AddRow(NodeName, Effect);
	}
	UOutlierUpgradeComponent* PartnerUpgrade = Partner->GetUpgradeComponent();
	PartnerUpgrade->SetUpgradeRole(EOutlierUpgradeRole::Partner);
	PartnerUpgrade->SetUpgradeDataTable(PartnerNodes);
	PartnerUpgrade->SetUpgradeEffectDataTable(PartnerEffects);
	TestTrue(TEXT("Partner HP upgrade purchased"), PartnerUpgrade->TryActivateNode(TEXT("PartnerVitality0")));
	TestEqual(TEXT("Partner missing HP stays 40"), Partner->GetVitalAttributeSet()->GetHealth(), 80.0f);
	PartnerUpgrade->ReconcileUpgradeProjection();
	TestEqual(TEXT("Partner reprojection does not heal again"), Partner->GetVitalAttributeSet()->GetHealth(), 80.0f);
	PartnerASC->SetNumericAttributeBase(UOutlierVitalAttributeSet::GetHealthAttribute(), 0.0f);
	PartnerASC->ApplyRebootStateToSelf(30.0f);
	TestTrue(TEXT("Purchase can project while Rebooting"), PartnerUpgrade->TryActivateNode(TEXT("PartnerVitality1")));
	TestEqual(TEXT("Max HP purchase does not revive Rebooting Partner"), Partner->GetVitalAttributeSet()->GetHealth(), 0.0f);
	return true;
}

#endif
