#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/UpgradeMachineWidget.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace DeveloperConveniencePresetTests
{
struct FFixture
{
	UWorld* World = nullptr;
	AOutlierGameMode* Mode = nullptr;
	AFirstPersonPlayerController* Shooter = nullptr;
	AFirstPersonPlayerController* Partner = nullptr;
	AOutlierPlayerState* ShooterState = nullptr;
	AOutlierPlayerState* PartnerState = nullptr;
	UOutlierArenaSubsystem* Arena = nullptr;

	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return false;
		}
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		World->InitializeActorsForPlay(FURL());
		UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
			TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
		Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
		Shooter = World->SpawnActor<AFirstPersonPlayerController>();
		Partner = World->SpawnActor<AFirstPersonPlayerController>();
		ShooterState = World->SpawnActor<AOutlierPlayerState>();
		PartnerState = World->SpawnActor<AOutlierPlayerState>();
		Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
		if (!Test.TestNotNull(TEXT("Mode"), Mode) || !Shooter || !Partner || !ShooterState || !PartnerState || !Arena)
		{
			return false;
		}
		Shooter->PlayerState = ShooterState;
		Partner->PlayerState = PartnerState;
		ShooterState->SetOwner(Shooter);
		PartnerState->SetOwner(Partner);
		ShooterState->SetPairId(7);
		PartnerState->SetPairId(7);
		ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
		PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
		return true;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConveniencePresetTest,
	"Outlier.UI.DeveloperConvenience.PresetAuthorityAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConveniencePresetTest::RunTest(const FString& Parameters)
{
	DeveloperConveniencePresetTests::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	F.Mode->RefreshDeveloperConveniencePair(F.ShooterState, F.PartnerState);
	F.Mode->DeveloperConvenienceState.SessionId = FGuid::NewGuid();
	F.Mode->DeveloperConvenienceOperator = F.Shooter;
	const auto& Snapshot = F.Mode->DeveloperConvenienceState;
	FDeveloperConveniencePresetRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.MatchId = Snapshot.MatchId;
	Request.SessionId = Snapshot.SessionId;
	Request.GameplayGeneration = Snapshot.GameplayGeneration;
	Request.StageId = TEXT("Level1");
	TestEqual(TEXT("Waiting player cannot move the pair"), F.Mode->RequestDeveloperConveniencePreset(F.Partner, Request),
		EDeveloperConvenienceRequestResult::NotOperator);
	Request.GameplayGeneration += 1;
	TestEqual(TEXT("Previous page context cannot move a new generation"), F.Mode->RequestDeveloperConveniencePreset(F.Shooter, Request),
		EDeveloperConvenienceRequestResult::StaleContext);
	Request.GameplayGeneration = Snapshot.GameplayGeneration;
	for (FName Stage : { FName(NAME_None), FName(TEXT("ArbitraryMap")), FName(TEXT("CheckpointTest")) })
	{
		Request.StageId = Stage;
		TestEqual(TEXT("Unknown stages cannot use the fallback spawn"), F.Mode->RequestDeveloperConveniencePreset(F.Shooter, Request),
			EDeveloperConvenienceRequestResult::InvalidStage);
	}
	Request.StageId = TEXT("Level1");
	F.Mode->PresetUpgradeWidgetClass = nullptr;
	TestEqual(TEXT("Missing UI is rejected before reload"), F.Mode->RequestDeveloperConveniencePreset(F.Shooter, Request),
		EDeveloperConvenienceRequestResult::UIUnavailable);
	TestEqual(TEXT("Rejected request preserves the menu session"), F.Mode->DeveloperConvenienceState.SessionId, Request.SessionId);
	TestFalse(TEXT("Rejected request does not enter transition"), F.Mode->DeveloperConvenienceState.bTransitioning);
	TestFalse(TEXT("Rejected request does not reserve automatic UI"), F.Mode->PendingPresetUpgradeRequestId.IsValid());
	return true;
}

#endif
