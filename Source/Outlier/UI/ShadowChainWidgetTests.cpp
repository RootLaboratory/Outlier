#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ShadowChainWidget.h"
#include "Components/Image.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/ScopeExit.h"
#include "Shooter/ShooterCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "InputKeyEventArgs.h"
#include "OutlierPlayerState.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowChainDisplayTest,
	"Outlier.UI.ShadowChain.DisplayTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShadowChainDisplayTest::RunTest(const FString& Parameters)
{
	FShadowChainDisplayState Display;
	FOutlierShadowChainPresentationState State;
	Display.Update(State, 0.0f, 0.0f, 0.15f);
	TestFalse(TEXT("Uninitialized data is hidden"), Display.bVisible);
	State.bInitialized = true;
	State.bUnlocked = true;
	State.RequiredKills = 5;
	Display.Update(State, 0.0f, 0.0f, 0.15f);
	TestTrue(TEXT("Acquired zero stacks retains silhouette"), Display.bVisible);
	TestEqual(TEXT("Zero stacks has no bright region"), Display.Progress, 0.0f);
	State.CurrentStacks = 1;
	Display.Update(State, 0.0f, 0.075f, 0.15f);
	TestTrue(TEXT("Stack fill moves smoothly rather than snapping"), Display.Progress > 0.0f && Display.Progress < 0.2f);
	const float HalfFill = Display.Progress;
	State.CurrentStacks = 2;
	Display.Update(State, 0.0f, 0.0f, 0.15f);
	TestEqual(TEXT("A rapid kill retargets from the current height"), Display.Progress, HalfFill);
	Display.Update(State, 0.0f, 0.15f, 0.15f);
	TestEqual(TEXT("Latest target completes without queued old animations"), Display.Progress, 0.4f);
	TestFalse(TEXT("Idle stacks need no frame updates"), Display.NeedsUpdate());

	State.bPassiveActive = true;
	State.DurationSeconds = 1.5f;
	State.ActivationSerial = 1;
	State.CurrentStacks = 0;
	Display.Update(State, 1.5f, 0.0f, 0.15f);
	TestTrue(TEXT("Successful passive activation selects stealth color"), Display.bPassive);
	TestEqual(TEXT("Activation begins at the current height"), Display.Progress, 0.4f);
	Display.Update(State, 1.35f, 0.15f, 0.15f);
	TestEqual(TEXT("Fast refill reaches full height"), Display.Progress, 1.0f);
	Display.Update(State, 1.25f, 0.1f, 0.15f);
	TestTrue(TEXT("Refill rejoins the actual server ratio"), FMath::IsNearlyEqual(Display.Progress, 1.25f / 1.5f));
	State.CurrentStacks = 2;
	Display.Update(State, 1.2f, 0.0f, 0.15f);
	TestTrue(TEXT("Hidden stack change does not refill active timer"), FMath::IsNearlyEqual(Display.Progress, 0.8f));
	Display.Update(State, 1.2f, 0.0f, 0.15f);
	TestTrue(TEXT("Duplicate active state does not restart refill"), FMath::IsNearlyEqual(Display.Progress, 0.8f));

	++State.ActivationSerial;
	Display.Update(State, 1.5f, 0.0f, 0.15f);
	TestTrue(TEXT("Refresh starts from current timer height"), FMath::IsNearlyEqual(Display.Progress, 0.8f));
	Display.Update(State, 1.425f, 0.075f, 0.15f);
	TestTrue(TEXT("Refresh smoothly fills while the tag stays active"), Display.Progress > 0.8f && Display.Progress < 1.0f);
	Display.Update(State, 1.35f, 0.075f, 0.15f);
	TestEqual(TEXT("Refreshed fill reaches full"), Display.Progress, 1.0f);
	Display.Update(State, 0.0f, 0.0f, 0.15f);
	TestFalse(TEXT("Expiry changes color even if end replication is delayed"), Display.bPassive);
	Display.Update(State, 0.0f, 0.15f, 0.15f);
	TestEqual(TEXT("Expiry returns to accumulated stacks"), Display.Progress, 0.4f);

	Display.Update(State, 0.5f, 0.0f, 0.15f, true);
	TestTrue(TEXT("Late attachment uses actual remaining ratio immediately"), FMath::IsNearlyEqual(Display.Progress, 1.0f / 3.0f));
	State.bPassiveActive = false;
	State.CurrentStacks = 1;
	State.RequiredKills = 3;
	Display.Update(State, 0.0f, 0.15f, 0.15f);
	TestTrue(TEXT("Three-kill tuning uses one third, not a hardcoded fifth"), FMath::IsNearlyEqual(Display.Progress, 1.0f / 3.0f));
	State.bUnlocked = false;
	Display.Update(State, 0.0f, 0.0f, 0.15f);
	TestFalse(TEXT("Losing acquisition hides and stops animation"), Display.bVisible || Display.NeedsUpdate());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowChainSnapshotContractTest,
	"Outlier.UI.ShadowChain.SnapshotContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShadowChainSnapshotContractTest::RunTest(const FString& Parameters)
{
	FOutlierShadowChainPresentationState State;
	State.bInitialized = State.bUnlocked = State.bPassiveActive = true;
	State.StartServerTime = 100.0f;
	State.DurationSeconds = 1.5f;
	TestEqual(TEXT("A late packet retains only real remaining time"), State.GetRemaining(101.0f), 0.5f);
	TestEqual(TEXT("Expired packets do not restart the timer"), State.GetRemaining(103.0f), 0.0f);
	TestTrue(TEXT("Clock correction cannot exceed the effect duration"), State.GetRemaining(99.0f) <= State.DurationSeconds);
	State.bPassiveActive = false;
	TestEqual(TEXT("Non-passive stealth has no passive remaining time"), State.GetRemaining(100.0f), 0.0f);

	const FProperty* Snapshot = FindFProperty<FProperty>(UOutlierAbilitySystemComponent::StaticClass(), TEXT("ShadowChainPresentation"));
	if (!TestNotNull(TEXT("Snapshot is a reflected replicated property"), Snapshot)) return false;
	TestTrue(TEXT("Changes use RepNotify"), Snapshot->HasAllPropertyFlags(CPF_Net | CPF_RepNotify));
	TArray<FLifetimeProperty> Lifetime;
	GetDefault<UOutlierAbilitySystemComponent>()->GetLifetimeReplicatedProps(Lifetime);
	const FLifetimeProperty* Entry = Lifetime.FindByPredicate([Snapshot](const FLifetimeProperty& Property) { return Property.RepIndex == Snapshot->RepIndex; });
	if (!TestNotNull(TEXT("Snapshot participates in replication"), Entry)) return false;
	TestEqual(TEXT("Stacks are private to the owner connection"), Entry->Condition, COND_OwnerOnly);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowChainDebugKeyTest,
	"Outlier.UI.ShadowChain.DebugHelperWithoutKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShadowChainDebugKeyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("StealthDebugKey")));
	if (!TestNotNull(TEXT("Debug key game world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	AFirstPersonPlayerController* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	if (!TestTrue(TEXT("Server debug fixture"), Controller && State && Shooter)) return false;
	State->SetPlayerRole(EOutlierPlayerRole::Shooter);
	State->SetPairId(7);
	State->SetShooterCharacter(Shooter);
	Shooter->SetPlayerState(State);
	Controller->SetPlayerState(State);
	Controller->SetAsLocalPlayerController();
	UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
	ASC->InitializeForPawn(Shooter);
	if (!ASC->IsShooterSuitConfigured()) ASC->ConfigureShooterSuitAbilities(FOutlierShooterSuitConfig());
	UOutlierUpgradeComponent* Upgrades = Shooter->FindComponentByClass<UOutlierUpgradeComponent>();
	UOutlierUpgradeSetData* Set = LoadObject<UOutlierUpgradeSetData>(nullptr,
		TEXT("/Game/Datas/Character/Upgrade/DA_ShooterUpgradeData.DA_ShooterUpgradeData"));
	if (!TestNotNull(TEXT("Real Shooter upgrade data"), Set)) return false;
	// This isolated fixture has not dispatched BeginPlay, which initializes the BP's preset data.
	Upgrades->SetUpgradeSetData(nullptr);
	Upgrades->SetUpgradeSetData(Set);
	State->RestoreCheckpointProgress(7, EOutlierUpgradeRole::Shooter, {TEXT("Unrelated")});
	Upgrades->SyncFromPlayerState();
	// 기존 키 입력은 제거하고 Tester가 쓰는 서버 헬퍼는 그대로 유지한다.
	const uint32 BeforeKeys = State->GetUpgradeRestoreRevision(EOutlierUpgradeRole::Shooter);
	for (const FKey& DebugKey : { EKeys::F1, EKeys::F2 })
	{
		for (EInputEvent Event : { IE_Pressed, IE_Repeat, IE_Released, IE_Pressed })
		{
			FInputKeyEventArgs Key;
			Key.Key = DebugKey;
			Key.Event = Event;
			static_cast<APlayerController*>(Controller)->InputKey(Key);
			TestFalse(TEXT("Each legacy key event leaves invincibility disabled"), State->IsDebugPairInvulnerable());
		}
	}
	TestEqual(TEXT("Legacy keys do not grant upgrades"), State->GetUpgradeRestoreRevision(EOutlierUpgradeRole::Shooter), BeforeKeys);
	TestFalse(TEXT("Legacy keys do not toggle invincibility"), State->IsDebugPairInvulnerable());
	Controller->ServerDebugGrantStealthUpgrades();
	const TArray<FName> Granted = State->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter);
	for (const TCHAR* Node : {TEXT("Shooter_Stealth_001"), TEXT("Shooter_Stealth_002"), TEXT("Shooter_Stealth_003"),
		TEXT("Shooter_Stealth_004A"), TEXT("Shooter_Stealth_004B"), TEXT("Shooter_Stealth_005A"), TEXT("Shooter_Stealth_005B")})
	{
		TestTrue(FString::Printf(TEXT("Real stealth node granted: %s"), Node), Granted.Contains(FName(Node)));
	}
	TestEqual(TEXT("Only the seven stealth upgrades were added"), Granted.Num(), 8);
	TestTrue(TEXT("Existing other upgrades are preserved"), Granted.Contains(TEXT("Unrelated")));
	TestEqual(TEXT("Currency is preserved"), State->GetNodeCount(), 7);
	TestTrue(TEXT("Normal projection unlocks the ShadowChain UI snapshot"), ASC->GetShadowChainPresentationState().bUnlocked);
	const uint32 Revision = State->GetUpgradeRestoreRevision(EOutlierUpgradeRole::Shooter);
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		Controller->ServerDebugGrantStealthUpgrades();
	}
	TestEqual(TEXT("Repeated presses never duplicate grants or replay restore"), State->GetUpgradeRestoreRevision(EOutlierUpgradeRole::Shooter), Revision);
	State->SetPlayerRole(EOutlierPlayerRole::Partner);
	State->RestoreCheckpointProgress(7, EOutlierUpgradeRole::Shooter, {});
	Controller->ServerDebugGrantStealthUpgrades();
	TestTrue(TEXT("Partner cannot grant Shooter upgrades"), State->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowChainWidgetAutoBindingTest,
	"Outlier.UI.ShadowChain.AutoBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShadowChainWidgetAutoBindingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("ShadowChainAutoBinding")));
	if (!TestNotNull(TEXT("Transient fixture world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	ON_SCOPE_EXIT { Controller->Player = nullptr; Player->PlayerController = nullptr; };
	UShadowChainWidget* Widget = CreateWidget<UShadowChainWidget>(Controller);
	if (!TestNotNull(TEXT("Widget inherits its owning player"), Widget)) return false;
	// This isolated world has no viewport/GameInstance to supply the local player's world.
	Widget->SetPlayerContext(FLocalPlayerContext(Player, World));
	Widget->ShadowChainImage = NewObject<UImage>(Widget);
	Widget->FillMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Material/Widget/M_UI_NodeFill.M_UI_NodeFill"));
	Widget->NativeConstruct();
	ON_SCOPE_EXIT { Widget->NativeDestruct(); };
	TestTrue(TEXT("HUD before possession keeps the controller subscription"), Widget->ObservedController == Controller);
	TestEqual(TEXT("No Pawn is hidden"), Widget->GetVisibility(), ESlateVisibility::Collapsed);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Concrete Shooter class"), ShooterClass)) return false;
	AShooterCharacter* First = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	AShooterCharacter* Second = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(500, 0, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("First Shooter"), First) || !TestNotNull(TEXT("Replacement Shooter"), Second)) return false;
	Controller->Possess(First);
	UOutlierAbilitySystemComponent* FirstASC = First->GetOutlierAbilitySystemComponent();
	TestTrue(TEXT("Late possession automatically binds the Shooter ASC"), Widget->ObservedASC == FirstASC);
	FirstASC->InitializeForPawn(First);
	if (!FirstASC->IsShooterSuitConfigured()) FirstASC->ConfigureShooterSuitAbilities(FOutlierShooterSuitConfig());
	TestTrue(TEXT("Fixture config initialized"), FirstASC->IsShooterSuitConfigured());
	FirstASC->AddLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowChain());
	TestEqual(TEXT("Acquisition shows the silhouette without BP calls"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	Widget->NativeDestruct();
	TestFalse(TEXT("Removed widget releases controller subscription"), Widget->ObservedController.IsValid());
	TestFalse(TEXT("Removed widget releases ASC subscription"), Widget->PresentationHandle.IsValid());
	Widget->NativeConstruct();
	TestTrue(TEXT("Existing possession binds on reconstruction"), Widget->ObservedASC == FirstASC);
	Controller->UnPossess();
	TestFalse(TEXT("Unpossess clears previous ASC"), Widget->ObservedASC.IsValid());
	TestTrue(TEXT("Unpossess preserves future Pawn notifications"), Widget->ObservedController == Controller);
	Controller->Possess(Second);
	TestTrue(TEXT("Respawn automatically follows the new Shooter"), Widget->ObservedShooter == Second);
	TestTrue(TEXT("Respawn binds only the new ASC"), Widget->ObservedASC == Second->GetOutlierAbilitySystemComponent());
	FirstASC->RemoveLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowChain());
	TestTrue(TEXT("Old Shooter changes cannot reconnect old ASC"), Widget->ObservedASC == Second->GetOutlierAbilitySystemComponent());
	Widget->HandleObservedEndPlay(Second, EEndPlayReason::Destroyed);
	TestFalse(TEXT("Ended Pawn is released"), Widget->ObservedASC.IsValid());
	TestTrue(TEXT("Pawn EndPlay retains controller subscription"), Widget->ObservedController == Controller);
	Widget->ClearObservedShooter();
	Controller->UnPossess();
	Controller->Possess(First);
	TestFalse(TEXT("Explicit clear remains an opt out"), Widget->ObservedASC.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowChainWidgetMaterialTest,
	"Outlier.UI.ShadowChain.MaterialIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShadowChainWidgetMaterialTest::RunTest(const FString& Parameters)
{
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Material/Widget/M_UI_NodeFill.M_UI_NodeFill"));
	if (!TestNotNull(TEXT("Reusable node fill material exists"), Material)) return false;
	UShadowChainWidget* First = NewObject<UShadowChainWidget>();
	UShadowChainWidget* Second = NewObject<UShadowChainWidget>();
	for (UShadowChainWidget* Widget : { First, Second })
	{
		Widget->ShadowChainImage = NewObject<UImage>(Widget);
		Widget->FillMaterial = Material;
		Widget->EnsureMaterial();
	}
	if (!TestNotNull(TEXT("First widget creates its own MID"), First->DynamicMaterial.Get())
		|| !TestNotNull(TEXT("Second widget creates its own MID"), Second->DynamicMaterial.Get())) return false;
	TestNotEqual(TEXT("Widgets never share mutable material state"), First->DynamicMaterial.Get(), Second->DynamicMaterial.Get());
	First->DynamicMaterial->SetScalarParameterValue(TEXT("Progress"), 0.4f);
	Second->DynamicMaterial->SetScalarParameterValue(TEXT("Progress"), 0.8f);
	First->EnsureMaterial();
	float Value = 0.0f;
	TestTrue(TEXT("Existing material exposes Progress"), First->DynamicMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Progress")), Value));
	TestEqual(TEXT("Reapplying settings preserves first widget progress"), Value, 0.4f);
	Second->DynamicMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Progress")), Value);
	TestEqual(TEXT("Second widget remains independent"), Value, 0.8f);
	First->ClearObservedShooter();
	TestEqual(TEXT("Explicit clear hides the widget"), First->GetVisibility(), ESlateVisibility::Collapsed);
	return true;
}
#endif

#endif
