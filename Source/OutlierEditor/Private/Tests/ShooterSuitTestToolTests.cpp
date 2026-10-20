#if WITH_DEV_AUTOMATION_TESTS

#include "OutlierShooterSuitTestToolWidget.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

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

#endif
