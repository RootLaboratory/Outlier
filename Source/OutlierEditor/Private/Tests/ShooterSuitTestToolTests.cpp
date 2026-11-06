#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierShooterSuitTestToolWidget.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Engine/Engine.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Data/OutlierVitalityDataRow.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "OutlierPlayerState.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "InputKeyEventArgs.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Widgets/Input/SCheckBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSuitTestToolNodesTest, "Outlier.Editor.ShooterSuitTester.NodeToggles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSuitTestToolNodesTest::RunTest(const FString& Parameters)
{
	using namespace OutlierShooterSuitTest;
	const FName Unrelated(TEXT("UnrelatedUpgrade"));
	const FName AddedLater(TEXT("PurchasedDuringTesting"));
	const FName FeatureA(TEXT("FeatureA"));
	const FName FeatureB(TEXT("FeatureB"));
	const TArray<FName> Matching{FeatureA, FeatureB};
	const TArray<FName> Original{Unrelated, FeatureB};

	TArray<FName> Enabled = ToggleNodes({Unrelated}, Matching, true);
	TestTrue(TEXT("Enabling preserves unrelated nodes"), Enabled.Contains(Unrelated));
	TestTrue(TEXT("Enabling adds one matching node rather than stacking alternatives"), Enabled.Contains(FeatureA) && !Enabled.Contains(FeatureB) && Enabled.Num() == 2);
	TestTrue(TEXT("Repeated enable is idempotent"), ToggleNodes(Enabled, Matching, true) == Enabled);
	TestTrue(TEXT("An already active alternative is preserved without another grant"), ToggleNodes(Original, Matching, true) == Original);
	TestTrue(TEXT("Disabling removes all sources of the selected feature"), ToggleNodes({Unrelated, FeatureA, FeatureB}, Matching, false) == TArray<FName>{Unrelated});
	TestTrue(TEXT("Missing mappings do not add a fabricated node"), ToggleNodes(Original, {}, true) == Original);

	Enabled.Add(AddedLater);
	TArray<FName> Restored = RestoreNodes(Enabled, Original, Matching);
	TestTrue(TEXT("Restore recovers the original matching node"), Restored.Contains(FeatureB) && !Restored.Contains(FeatureA));
	TestTrue(TEXT("Restore preserves unrelated purchases made during testing"), Restored.Contains(Unrelated) && Restored.Contains(AddedLater));
	TestTrue(TEXT("Restore does not duplicate nodes"), RestoreNodes(Restored, Original, Matching) == Restored);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSuitTestToolGrantTest, "Outlier.Editor.ShooterSuitTester.GrantChecks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSuitTestToolGrantTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false);
	if (!TestNotNull(TEXT("A transient PIE world is available"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		// 텅 빈 Actor와 ASC만 사용한다. 렌더 컴포넌트/PIE 에셋은 생성하지 않는다.
		World->DestroyWorld(false);
	};
	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("The test actor is spawned"), Owner))
	{
		return false;
	}
	UOutlierAbilitySystemComponent* ASC = NewObject<UOutlierAbilitySystemComponent>(Owner);
	Owner->AddInstanceComponent(ASC);
	// GAS는 OnRegister에서 AbilityActorInfo를 생성한다. 등록을 마친 뒤 Actor 정보를 초기화한다.
	ASC->RegisterComponent();
	if (!TestTrue(TEXT("The test ASC is registered before actor info initialization"), ASC->IsRegistered()))
	{
		return false;
	}
	ASC->InitializeForActor(Owner);
	const FGameplayTag Stealth = OutlierGameplayTags::Ability::Shooter::Stealth();
	TestTrue(TEXT("Existing NoGrantMode initially permits a locked ability"), ASC->IsShooterSuitAbilityGranted(Stealth, false));
	ASC->SetEditorShooterSuitGrantChecks(true);
	TestTrue(TEXT("The authority PIE setter enables strict grant checks"), ASC->AreEditorShooterSuitGrantChecksEnabled());
	const FGameplayTag Abilities[] =
	{
		Stealth,
		OutlierGameplayTags::Ability::Shooter::BulletReflection(),
		OutlierGameplayTags::Ability::Shooter::WeaponOvercharge()
	};
	for (const FGameplayTag& Ability : Abilities)
	{
		TestFalse(TEXT("An unchecked ability cannot bypass its grant in test mode"), ASC->IsShooterSuitAbilityGranted(Ability, false));
		TestTrue(TEXT("A real grant still permits the checked ability"), ASC->IsShooterSuitAbilityGranted(Ability, true));
	}
	TestTrue(TEXT("Quantum Leap keeps its base ability rule"), ASC->IsShooterSuitAbilityGranted(OutlierGameplayTags::Ability::Shooter::QuantumLeap(), false));
	ASC->SetEditorShooterSuitGrantChecks(false);
	TestTrue(TEXT("Restoring test mode restores the original NoGrantMode behavior"), ASC->IsShooterSuitAbilityGranted(Stealth, false));
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> NonPIE(World->WorldType, EWorldType::Game);
		ASC->SetEditorShooterSuitGrantChecks(true);
		TestFalse(TEXT("The editor setter cannot enable overrides outside PIE"), ASC->AreEditorShooterSuitGrantChecksEnabled());
	}
	ASC->ClearForActor(Owner);
	return true;
}

namespace
{
	struct FTesterInvulnerabilityWorld
	{
		UWorld* World = nullptr;
		AShooterCharacter* Shooter = nullptr;
		APartnerCharacter* Partner = nullptr;
		AOutlierPlayerState* ShooterState = nullptr;
		AOutlierPlayerState* PartnerState = nullptr;
		UClass* ShooterClass = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::PIE, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TesterInvulnerability")));
			if (!Test.TestNotNull(TEXT("PIE test world exists"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			ShooterClass = LoadClass<AShooterCharacter>(nullptr,
				TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
			if (!Test.TestNotNull(TEXT("Concrete Shooter class exists"), ShooterClass)) return false;
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform::Identity, Spawn);
			Partner = World->SpawnActor<APartnerCharacter>();
			ShooterState = World->SpawnActor<AOutlierPlayerState>();
			PartnerState = World->SpawnActor<AOutlierPlayerState>();
			if (!Test.TestTrue(TEXT("Both roles and PlayerStates exist"), Shooter && Partner && ShooterState && PartnerState)) return false;
			ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
			PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
			ShooterState->SetPairId(7);
			PartnerState->SetPairId(7);
			Shooter->SetPlayerState(ShooterState);
			Partner->SetPlayerState(PartnerState);
			// 실제 페어 참조를 연결하되 맵/장비를 준비하는 production BeginPlay는 실행하지 않는다.
			ShooterState->SetShooterCharacter(Shooter);
			ShooterState->SetPartnerCharacter(Partner);
			PartnerState->SetShooterCharacter(Shooter);
			PartnerState->SetPartnerCharacter(Partner);
			Shooter->GetOutlierAbilitySystemComponent()->InitializeForPawn(Shooter);
			Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Partner);
			return Shooter->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f)
				&& Partner->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
		}

		~FTesterInvulnerabilityWorld()
		{
			if (!World) return;
			if (Partner) Partner->GetPartnerVitalityComponent()->BeginOwnerTeardown();
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
		}
	};
}

struct FShooterSuitTesterInvulnerabilityTestAccess
{
	static void GrantPistol(SOutlierShooterSuitTestToolWidget& Widget, AShooterCharacter* Shooter)
	{
		Select(Widget, Shooter->GetPlayerState<AOutlierPlayerState>());
		Widget.SelectedTarget->Shooter = Shooter;
		Widget.SelectedTarget->ASC = Shooter->GetOutlierAbilitySystemComponent();
		Widget.SelectedTarget->Upgrades = Shooter->FindComponentByClass<UOutlierUpgradeComponent>();
		Widget.GrantPistol();
	}
	static void Select(SOutlierShooterSuitTestToolWidget& Widget, AOutlierPlayerState* State)
	{
		Widget.SelectedTarget = MakeShared<SOutlierShooterSuitTestToolWidget::FTarget>();
		Widget.SelectedTarget->PlayerState = State;
	}
	static void Toggle(SOutlierShooterSuitTestToolWidget& Widget, bool bEnabled)
	{
		Widget.SetInvulnerabilityState(bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	}
	static bool IsChecked(const SOutlierShooterSuitTestToolWidget& Widget)
	{
		return Widget.GetInvulnerabilityState() == ECheckBoxState::Checked;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSuitTesterPistolTest,
	"Outlier.Editor.ShooterSuitTester.PistolGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSuitTesterPistolTest::RunTest(const FString& Parameters)
{
	FTesterInvulnerabilityWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	TSharedRef<SOutlierShooterSuitTestToolWidget> Widget = SNew(SOutlierShooterSuitTestToolWidget);
	UShooterInventoryComponent* Inventory = Fixture.Shooter->GetInventoryComponent();
	if (!TestNotNull(TEXT("Shooter inventory"), Inventory)) return false;
	// 이 Fixture는 Pawn BeginPlay를 생략한다. 인벤토리의 실제 슬롯 초기화만 수행한다.
	Inventory->BeginPlay();
	FShooterSuitTesterInvulnerabilityTestAccess::GrantPistol(*Widget, Fixture.Shooter);
	AWeaponBase* Pistol = Inventory->GetWeaponInSlot(EWeaponSlot::Secondary);
	if (!TestNotNull(TEXT("Button grants a secondary weapon"), Pistol)) return false;
	TestEqual(TEXT("Granted weapon is a pistol"), Pistol->GetWeaponType(), EWeaponType::Pistol);
	const int32 SlotIndex = static_cast<int32>(EWeaponSlot::Secondary);
	const FOutlierLoadoutSnapshot& Snapshot = Fixture.ShooterState->GetLoadoutSnapshot();
	TestTrue(TEXT("Grant persists in PlayerState loadout"), Snapshot.SlotSnapshots.IsValidIndex(SlotIndex)
		&& Snapshot.SlotSnapshots[SlotIndex].WeaponClass == Pistol->GetClass());
	FShooterSuitTesterInvulnerabilityTestAccess::GrantPistol(*Widget, Fixture.Shooter);
	TestTrue(TEXT("Repeated button preserves the original pistol"), Inventory->GetWeaponInSlot(EWeaponSlot::Secondary) == Pistol);
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> NonPIE(Fixture.World->WorldType, EWorldType::Game);
		FShooterSuitTesterInvulnerabilityTestAccess::GrantPistol(*Widget, Fixture.Shooter);
		TestTrue(TEXT("Non-PIE call preserves inventory"), Inventory->GetWeaponInSlot(EWeaponSlot::Secondary) == Pistol);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSuitTesterInvulnerabilityWidgetTest,
	"Outlier.Editor.ShooterSuitTester.Invulnerability.WidgetSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSuitTesterInvulnerabilityWidgetTest::RunTest(const FString& Parameters)
{
	FTesterInvulnerabilityWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	using FAccess = FShooterSuitTesterInvulnerabilityTestAccess;
	{
		TSharedRef<SOutlierShooterSuitTestToolWidget> Widget = SNew(SOutlierShooterSuitTestToolWidget);
		FAccess::Select(*Widget, Fixture.ShooterState);
		TestFalse(TEXT("Initial checkbox is OFF"), FAccess::IsChecked(*Widget));
		FAccess::Toggle(*Widget, true);
		TestTrue(TEXT("UI handler enables actual pair damage protection"), OutlierDamage::IsEditorInvulnerable(Fixture.Partner));
	}
	{
		TSharedRef<SOutlierShooterSuitTestToolWidget> Reopened = SNew(SOutlierShooterSuitTestToolWidget);
		FAccess::Select(*Reopened, Fixture.ShooterState);
		TestTrue(TEXT("Reopened widget reads ON from PlayerState"), FAccess::IsChecked(*Reopened));
		FAccess::Toggle(*Reopened, false);
		TestFalse(TEXT("UI OFF also releases Partner protection"), OutlierDamage::IsEditorInvulnerable(Fixture.Partner));
		FAccess::Toggle(*Reopened, true);
	}
	FTesterInvulnerabilityWorld NextSession;
	if (!NextSession.Initialize(*this)) return false;
	TestFalse(TEXT("New PIE world starts OFF independently of previous world"), NextSession.ShooterState->IsEditorPairInvulnerable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSuitTesterInvulnerabilityDamageTest,
	"Outlier.Editor.ShooterSuitTester.Invulnerability.Damage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSuitTesterInvulnerabilityDamageTest::RunTest(const FString& Parameters)
{
	FTesterInvulnerabilityWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UOutlierAbilitySystemComponent* ShooterASC = Fixture.Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Fixture.Partner->GetOutlierAbilitySystemComponent();
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 30.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 30.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 20.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 20.0f);
	TestTrue(TEXT("One toggle enables pair protection"), Fixture.ShooterState->SetEditorPairInvulnerable(true));
	const FGameplayTag DamageTags[] = { OutlierGameplayTags::Damage::Ranged(),
		OutlierGameplayTags::Damage::Explosion(), OutlierGameplayTags::Damage::Environment() };
	for (bool bSuit : { false, true })
	{
		Fixture.ShooterState->SetAcquiredSuit(bSuit);
		Fixture.PartnerState->SetAcquiredSuit(bSuit);
		for (const FGameplayTag& Tag : DamageTags)
		{
			FOutlierDamageRequest Request;
			Request.DamageAmount = 100000.0f;
			Request.DamageTag = Tag;
			TestEqual(TEXT("Shooter rejects lethal requests before side effects"), OutlierDamage::Apply(Fixture.Shooter, Request), 0.0f);
			TestEqual(TEXT("Partner rejects lethal requests before GameOver/Reboot"), OutlierDamage::Apply(Fixture.Partner, Request), 0.0f);
			TestEqual(TEXT("Direct Shooter receiver is also protected"), Fixture.Shooter->ReceiveOutlierDamage(Request), 0.0f);
			TestEqual(TEXT("Direct Partner receiver is also protected"), static_cast<IOutlierDamageReceiver*>(Fixture.Partner)->ReceiveOutlierDamage(Request), 0.0f);
		}
	}
	for (UOutlierAbilitySystemComponent* ASC : { ShooterASC, PartnerASC })
	{
		TestFalse(TEXT("Direct ASC damage cannot bypass protection"), ASC->ApplyDamageToSelf(100000.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
		FGameplayEffectSpecHandle DirectDamage = ASC->MakeOutgoingSpec(UOutlierDamageGameplayEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (!TestTrue(TEXT("Direct damage GE is valid"), DirectDamage.IsValid())) return false;
		DirectDamage.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Damage(), 100000.0f);
		ASC->ApplyGameplayEffectSpecToSelf(*DirectDamage.Data.Get());
		TestEqual(TEXT("Direct GE preserves health"), ASC->GetNumericAttribute(UOutlierVitalAttributeSet::GetHealthAttribute()), 100.0f);
	}
	TestEqual(TEXT("Shooter shield is untouched"), Fixture.Shooter->GetCurShield(), 30.0f);
	TestEqual(TEXT("Partner shield pool is untouched"), Fixture.Shooter->GetCurPartnerShield(), 20.0f);
	TestFalse(TEXT("No Shooter death"), Fixture.Shooter->IsDead());
	TestFalse(TEXT("No Partner reboot"), Fixture.Partner->GetPartnerVitalityComponent()->IsRebooting());

	const FActiveGameplayEffectHandle OtherImmunity = ShooterASC->ApplyDamageImmuneStateToSelf();
	TestTrue(TEXT("Existing ability immunity can coexist"), OtherImmunity.IsValid());
	Fixture.ShooterState->SetEditorPairInvulnerable(false);
	TestFalse(TEXT("OFF preserves unrelated immunity"), ShooterASC->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
	TestNotNull(TEXT("Other immunity effect remains active"), ShooterASC->GetActiveGameplayEffect(OtherImmunity));
	ShooterASC->RemoveActiveEffectFromSelf(OtherImmunity);
	TestTrue(TEXT("OFF restores normal Shooter damage"), ShooterASC->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
	TestEqual(TEXT("Normal damage consumes partner shield first"), Fixture.Shooter->GetCurPartnerShield(), 10.0f);
	TestTrue(TEXT("OFF restores normal Partner damage"), PartnerASC->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
	TestEqual(TEXT("Partner health decreases after OFF"), Fixture.Partner->GetVitalAttributeSet()->GetHealth(), 90.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSuitTesterInvulnerabilityLifecycleTest,
	"Outlier.Editor.ShooterSuitTester.Invulnerability.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSuitTesterInvulnerabilityLifecycleTest::RunTest(const FString& Parameters)
{
	FTesterInvulnerabilityWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	Fixture.ShooterState->SetEditorPairInvulnerable(true);
	Fixture.Partner->SetPlayerState(nullptr);
	TestTrue(TEXT("Unpossessed Partner keeps pair protection"), OutlierDamage::IsEditorInvulnerable(Fixture.Partner));
	Fixture.Partner->SetPlayerState(Fixture.PartnerState);

	AShooterCharacter* OldShooter = Fixture.Shooter;
	Fixture.ShooterState->SetShooterCharacter(nullptr);
	Fixture.ShooterState->SetPartnerCharacter(nullptr);
	Fixture.ShooterState->FlushActivatedUpgradeNodes(25);
	TestTrue(TEXT("Preset progress reset retains the session setting"), Fixture.ShooterState->IsEditorPairInvulnerable());
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Fixture.Shooter = Fixture.World->SpawnActor<AShooterCharacter>(Fixture.ShooterClass, FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Replacement Shooter exists"), Fixture.Shooter)) return false;
	Fixture.Shooter->SetPlayerState(Fixture.ShooterState);
	Fixture.Shooter->GetOutlierAbilitySystemComponent()->InitializeForPawn(Fixture.Shooter);
	Fixture.ShooterState->SetShooterCharacter(Fixture.Shooter);
	Fixture.ShooterState->SetPartnerCharacter(Fixture.Partner);
	TestTrue(TEXT("Replacement Pawn inherits protection without reopening the tool"), OutlierDamage::IsEditorInvulnerable(Fixture.Shooter));
	TestTrue(TEXT("Partner follows the new Shooter link"), OutlierDamage::IsEditorInvulnerable(Fixture.Partner));
	TestFalse(TEXT("Old detached Pawn is no longer protected"), OutlierDamage::IsEditorInvulnerable(OldShooter));
	APartnerCharacter* OldPartner = Fixture.Partner;
	Fixture.Partner = Fixture.World->SpawnActor<APartnerCharacter>();
	if (!TestNotNull(TEXT("Replacement Partner exists"), Fixture.Partner)) return false;
	Fixture.Partner->SetPlayerState(Fixture.PartnerState);
	Fixture.Partner->GetOutlierAbilitySystemComponent()->InitializeForPawn(Fixture.Partner);
	Fixture.ShooterState->SetPartnerCharacter(Fixture.Partner);
	Fixture.PartnerState->SetShooterCharacter(Fixture.Shooter);
	Fixture.PartnerState->SetPartnerCharacter(Fixture.Partner);
	TestTrue(TEXT("Replacement Partner inherits the same toggle"), OutlierDamage::IsEditorInvulnerable(Fixture.Partner));
	TestFalse(TEXT("Old detached Partner loses protection"), OutlierDamage::IsEditorInvulnerable(OldPartner));

	UDataTable* Vitality = NewObject<UDataTable>();
	Vitality->RowStruct = FOutlierVitalityDataRow::StaticStruct();
	FOutlierVitalityDataRow VitalityRow;
	VitalityRow.MaxHealth = 100.0f;
	Vitality->AddRow(TEXT("Test"), VitalityRow);
	UDataTable* Survival = NewObject<UDataTable>();
	Survival->RowStruct = FPartnerSurvivalDataRow::StaticStruct();
	FPartnerSurvivalDataRow SurvivalRow;
	SurvivalRow.RebootTime = 0.25f;
	Survival->AddRow(TEXT("Test"), SurvivalRow);
	FDataTableRowHandle VitalityHandle;
	VitalityHandle.DataTable = Vitality;
	VitalityHandle.RowName = TEXT("Test");
	FDataTableRowHandle SurvivalHandle;
	SurvivalHandle.DataTable = Survival;
	SurvivalHandle.RowName = TEXT("Test");
	UPartnerVitalityComponent* Component = Fixture.Partner->GetPartnerVitalityComponent();
	if (!TestTrue(TEXT("Real reboot component initializes"), Component->InitializeFromDataTables(VitalityHandle, SurvivalHandle))) return false;
	Fixture.ShooterState->SetEditorPairInvulnerable(false);
	UOutlierAbilitySystemComponent* PartnerASC = Fixture.Partner->GetOutlierAbilitySystemComponent();
	TestTrue(TEXT("Lethal damage starts actual reboot"), PartnerASC->ApplyDamageToSelf(100.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
	TestTrue(TEXT("Reboot is active before enabling Tester protection"), Component->IsRebooting());
	Fixture.ShooterState->SetEditorPairInvulnerable(true);
	TestTrue(TEXT("ON does not cancel an already active reboot"), Component->IsRebooting());
	TestTrue(TEXT("Actual reboot completes"), Component->RemoveRebootEffect());
	TestFalse(TEXT("Protection still rejects damage after reboot"), PartnerASC->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));

	AOutlierPlayerState* OtherPair = Fixture.World->SpawnActor<AOutlierPlayerState>();
	OtherPair->SetPlayerRole(EOutlierPlayerRole::Shooter);
	OtherPair->SetPairId(8);
	TestFalse(TEXT("Another pair starts OFF"), OtherPair->IsEditorPairInvulnerable());
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> NonPIE(Fixture.World->WorldType, EWorldType::Game);
		TestTrue(TEXT("Shared debug protection also works in game worlds"), OutlierDamage::IsEditorInvulnerable(Fixture.Shooter));
		TestFalse(TEXT("Setter refuses non-PIE worlds"), Fixture.ShooterState->SetEditorPairInvulnerable(false));
	}
	{
		Fixture.ShooterState->SetRole(ROLE_SimulatedProxy);
		TestFalse(TEXT("Non-authority setter is rejected"), Fixture.ShooterState->SetEditorPairInvulnerable(false));
		Fixture.ShooterState->SetRole(ROLE_Authority);
	}
	Fixture.ShooterState->SetPairId(9);
	TestFalse(TEXT("Changing pairs clears old session protection"), Fixture.ShooterState->IsEditorPairInvulnerable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairInvulnerabilityDebugKeyTest,
	"Outlier.Editor.ShooterSuitTester.Invulnerability.DebugKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPairInvulnerabilityDebugKeyTest::RunTest(const FString& Parameters)
{
	FTesterInvulnerabilityWorld Fixture;
	if (!Fixture.Initialize(*this)) return false;
	TGuardValue<TEnumAsByte<EWorldType::Type>> GameWorld(Fixture.World->WorldType, EWorldType::Game);
	AGameStateBase* GameState = Fixture.World->SpawnActor<AGameStateBase>();
	AFirstPersonPlayerController* Controller = Fixture.World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestTrue(TEXT("Debug transport fixture exists"), GameState && Controller)) return false;
	Fixture.World->SetGameState(GameState);
	GameState->AddPlayerState(Fixture.ShooterState);
	GameState->AddPlayerState(Fixture.PartnerState);
	Controller->SetPlayerState(Fixture.ShooterState);
	Controller->SetAsLocalPlayerController();

	FInputKeyEventArgs Key;
	Key.Key = EKeys::F1;
	Key.Event = IE_Pressed;
	TestTrue(TEXT("Local controller consumes F1"), static_cast<APlayerController*>(Controller)->InputKey(Key));
	if (!TestTrue(TEXT("F1 enables server pair state outside PIE"), Fixture.ShooterState->IsDebugPairInvulnerable())) return false;
	Key.Event = IE_Repeat;
	static_cast<APlayerController*>(Controller)->InputKey(Key);
	TestTrue(TEXT("Holding F1 does not repeatedly toggle protection"), Fixture.ShooterState->IsDebugPairInvulnerable());
	TestFalse(TEXT("Shooter damage is rejected"), Fixture.Shooter->GetOutlierAbilitySystemComponent()
		->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierDamage::InstantKillTag()));
	TestFalse(TEXT("Partner damage is rejected"), Fixture.Partner->GetOutlierAbilitySystemComponent()
		->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierDamage::InstantKillTag()));

	// Partner 요청도 같은 Shooter 상태를 뒤집으며, Pawn을 소유하지 않아도 동작해야 한다.
	Controller->SetPlayerState(Fixture.PartnerState);
	Controller->ServerDebugTogglePairInvulnerability();
	TestFalse(TEXT("Partner request turns the same pair OFF"), Fixture.ShooterState->IsDebugPairInvulnerable());
	TestTrue(TEXT("Shooter damage resumes"), Fixture.Shooter->GetOutlierAbilitySystemComponent()
		->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
	TestTrue(TEXT("Partner damage resumes"), Fixture.Partner->GetOutlierAbilitySystemComponent()
		->ApplyDamageToSelf(10.0f, nullptr, nullptr, OutlierGameplayTags::Damage::Environment()));
	Fixture.PartnerState->SetPairId(8);
	Controller->ServerDebugTogglePairInvulnerability();
	TestFalse(TEXT("Unmatched requester cannot toggle another pair"), Fixture.ShooterState->IsDebugPairInvulnerable());
	Fixture.ShooterState->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot change debug state directly"), Fixture.ShooterState->SetDebugPairInvulnerable(true));
	Fixture.ShooterState->SetRole(ROLE_Authority);
	Controller->SetPlayerState(nullptr);
	return true;
}

#endif
