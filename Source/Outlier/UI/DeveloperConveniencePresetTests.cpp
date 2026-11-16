#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Camera/PlayerCameraManager.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresetUpgradeReadinessTest,
	"Outlier.UI.DeveloperConvenience.PresetPairReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresetUpgradeReadinessTest::RunTest(const FString& Parameters)
{
	DeveloperConveniencePresetTests::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	F.Mode->RefreshDeveloperConveniencePair(F.ShooterState, F.PartnerState);
	// 스트리밍 I/O만 격리하고 현재 페어 검증, ACK 수집, 세션 생성은 실제 함수를 실행한다.
	F.Arena->GameplayGeneration = 5;
	ULevelStreamingDynamic* Streaming = NewObject<ULevelStreamingDynamic>(F.World);
	ULevel* Level = NewObject<ULevel>(F.World);
	Level->bIsVisible = true;
	FindFProperty<FObjectPropertyBase>(ULevelStreaming::StaticClass(), TEXT("LoadedLevel"))
		->SetObjectPropertyValue_InContainer(Streaming, Level);
	F.Arena->GameplayStreamingLevels = { Streaming };
	F.Arena->GameplaySublevels = { TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Tests/Preset.Preset"))) };
	F.Arena->bGameplayLevelsResolved = true;
	F.Mode->ActiveMatchPairId = 7;
	F.Mode->GameState = F.World->SpawnActor<AGameStateBase>();
	F.Mode->GameState->PlayerArray = { F.ShooterState, F.PartnerState };
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	auto* ShooterPawn = F.World->SpawnActor<AShooterCharacter>(ShooterClass);
	auto* PartnerPawn = F.World->SpawnActor<APartnerCharacter>();
	if (!ShooterPawn || !PartnerPawn)
	{
		return false;
	}
	F.Shooter->SetPawn(ShooterPawn);
	F.Partner->SetPawn(PartnerPawn);
	for (auto* State : { F.ShooterState, F.PartnerState })
	{
		State->SetShooterCharacter(ShooterPawn);
		State->SetPartnerCharacter(PartnerPawn);
		State->SetAcquiredSuit(true);
	}
	F.Mode->PresetUpgradeWidgetClass = LoadClass<UUpgradeMachineWidget>(nullptr,
		TEXT("/Script/OutlierEditor.UpgradeMachineTestWidget"));
	if (!TestNotNull(TEXT("Concrete upgrade widget fixture"), F.Mode->PresetUpgradeWidgetClass.Get()))
	{
		return false;
	}
	TestTrue(TEXT("Fixture server pair is ready"), F.Mode->IsPairGameplayReady());
	F.Mode->TryPreparePresetUpgradeUI();
	TestFalse(TEXT("Ordinary readiness does not schedule automatic UI"), F.Mode->PendingPresetUpgradeRequestId.IsValid());
	F.Shooter->SetAsLocalPlayerController();
	F.Partner->SetAsLocalPlayerController();
	F.Mode->ArmPresetUpgradeUI(7, 4);
	TestTrue(TEXT("Server arm covers the local host"), F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestTrue(TEXT("Server arm covers the other owner"), F.Partner->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	F.Mode->ClearPendingPresetUpgradeUI();
	TestFalse(TEXT("Failure before any UI preparation uncovers the host"), F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestFalse(TEXT("Failure before any UI preparation uncovers the peer"), F.Partner->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	F.Mode->ArmPresetUpgradeUI(7, 5);
	const FGuid RequestId = F.Mode->PendingPresetUpgradeRequestId;
	// 수신기 준비 전에는 통보를 인정하지 않는다.
	F.Mode->OnClientPresetUpgradeUIReady(F.Shooter, RequestId, 5, true);
	TestTrue(TEXT("Unsolicited ACK ignored"), F.Mode->PresetUpgradeReadyControllers.IsEmpty());
	F.Mode->PresetUpgradePreparedControllers = { F.Shooter, F.Partner };
	F.Mode->OnClientPresetUpgradeUIReady(F.Shooter, FGuid::NewGuid(), 5, true);
	F.Mode->OnClientPresetUpgradeUIReady(F.Shooter, RequestId, 4, true);
	TestTrue(TEXT("Stale IDs and generations ignored"), F.Mode->PresetUpgradeReadyControllers.IsEmpty());
	F.Mode->OnClientPresetUpgradeUIReady(F.Shooter, RequestId, 5, true);
	F.Mode->OnClientPresetUpgradeUIReady(F.Shooter, RequestId, 5, true);
	TestTrue(TEXT("One peer cannot open UI for both"), F.Mode->PendingPresetUpgradeRequestId.IsValid());
	TestEqual(TEXT("Duplicate peer ACK is idempotent"), F.Mode->PresetUpgradeReadyControllers.Num(), 1);
	F.Mode->OnClientPresetUpgradeUIReady(F.Partner, RequestId, 5, true);
	TestFalse(TEXT("Both ready consumes the one-shot intent"), F.Mode->PendingPresetUpgradeRequestId.IsValid());
	const uint32 Session = F.ShooterState->GetStatAllocatorSessionId();
	TestTrue(TEXT("Common opener starts the upgrade session"), Session != 0);
	TestEqual(TEXT("Both players share the upgrade session"), Session, F.PartnerState->GetStatAllocatorSessionId());
	F.Mode->OnClientPresetUpgradeUIReady(F.Partner, RequestId, 5, true);
	TestEqual(TEXT("Late ACK cannot create a second upgrade session"), Session, F.ShooterState->GetStatAllocatorSessionId());
	F.Mode->ArmPresetUpgradeUI(7, 5);
	F.Mode->PresetUpgradePreparedControllers.Add(F.Shooter);
	F.Mode->OnClientPresetUpgradeUIReady(F.Shooter, F.Mode->PendingPresetUpgradeRequestId, 5, false);
	TestFalse(TEXT("Unavailable UI clears intent without completing upgrades"), F.Mode->PendingPresetUpgradeRequestId.IsValid());
	TestFalse(TEXT("Unavailable UI does not fake completion"), F.ShooterState->IsStatAllocatorUICompletedForGeneration(5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresetUpgradeClientReadinessTest,
	"Outlier.UI.DeveloperConvenience.PresetClientRequestLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresetUpgradeClientReadinessTest::RunTest(const FString& Parameters)
{
	DeveloperConveniencePresetTests::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	const FGuid CurrentRequest = FGuid::NewGuid();
	const FGuid OldRequest = FGuid::NewGuid();
	F.Shooter->SetAsLocalPlayerController();
	F.Shooter->PlayerCameraManager = F.World->SpawnActor<APlayerCameraManager>();
	if (!TestNotNull(TEXT("Transition camera"), F.Shooter->PlayerCameraManager.Get()))
	{
		return false;
	}
	// 실제 뷰포트 Overlay에 HUD 역할의 하위 슬롯과 전환 가림을 붙여 순서·제거까지 검증한다.
	ULocalPlayer* Local = NewObject<ULocalPlayer>(GEngine);
	UGameViewportClient* Viewport = NewObject<UGameViewportClient>(GEngine);
	const TSharedRef<SOverlay> ViewportOverlay = SNew(SOverlay);
	Viewport->SetViewportOverlayWidget(nullptr, ViewportOverlay);
	ViewportOverlay->AddSlot(1010)[SNew(SBorder)];
	Local->ViewportClient = Viewport;
	Local->PlayerController = F.Shooter;
	F.Shooter->Player = Local;
	F.Shooter->ClientBeginPresetTransition_Implementation(OldRequest, 4);
	F.Shooter->ClientBeginPresetTransition_Implementation(CurrentRequest, 5);
	TestFalse(TEXT("Transition suppresses the HUD independently of death"), F.Shooter->ShouldShowMainWidget());
	TestTrue(TEXT("Transition also fades audio outside the project subsystem"), F.Shooter->PlayerCameraManager->bFadeAudio);
	const TSharedPtr<SWidget> Cover = F.Shooter->PresetTransitionCover;
	TestTrue(TEXT("Transition cover is attached to the viewport"), Cover.IsValid() && Cover->GetParentWidget() == ViewportOverlay);
	TestEqual(TEXT("Repeated begin does not duplicate the cover"), ViewportOverlay->GetChildren()->Num(), 2);
	TestTrue(TEXT("Cover is above AddToViewport UI layers"), ViewportOverlay->GetChildren()->GetChildAt(1) == Cover);
	TestTrue(TEXT("Accepted transition starts blackout before UI preparation"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	F.Shooter->ClearPendingPresetUpgradeUI();
	TestTrue(TEXT("Reload preparation reset preserves transition blackout"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	F.Shooter->AddCameraBlackoutReason(ECameraBlackoutReason::LevelLoad);
	F.Shooter->RemoveCameraBlackoutReason(ECameraBlackoutReason::LevelLoad);
	TestTrue(TEXT("Possession releasing LevelLoad cannot reveal an unfinished preset"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestEqual(TEXT("Camera stays fully covered while UI is not ready"), F.Shooter->PlayerCameraManager->FadeAmount, 1.0f);
	F.Shooter->RestoreMainWidgetAfterDeath();
	TestFalse(TEXT("Respawn HUD restoration cannot expose an unfinished transition"), F.Shooter->ShouldShowMainWidget());
	TestTrue(TEXT("Reload and possession preserve the independent Slate cover"), F.Shooter->PresetTransitionCover == Cover);
	F.Shooter->ClientBeginPresetTransition_Implementation(OldRequest, 4);
	TestEqual(TEXT("Old transition cannot replace the newer blackout owner"), F.Shooter->PresetTransitionRequestId, CurrentRequest);
	F.Shooter->PendingPresetUpgradeRequestId = CurrentRequest;
	F.Shooter->PendingPresetUpgradeGeneration = 5;
	F.Shooter->PendingPresetUpgradePairId = 7;
	F.Shooter->ClientCancelPresetUpgradeUI_Implementation(OldRequest);
	TestTrue(TEXT("Stale cancellation cannot remove the Slate cover"), F.Shooter->PresetTransitionCover == Cover);
	TestTrue(TEXT("Old cancellation preserves current blackout"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestEqual(TEXT("Old cancellation cannot clear a newer pending request"),
		F.Shooter->PendingPresetUpgradeRequestId, CurrentRequest);
	FUILayerPushRequest Push;
	F.Shooter->ClientOpenPresetUpgradeUI_Implementation(OldRequest, 5, Push);
	F.Shooter->ClientOpenPresetUpgradeUI_Implementation(CurrentRequest, 4, Push);
	TestEqual(TEXT("Delayed Open cannot consume current pending context"),
		F.Shooter->PendingPresetUpgradeRequestId, CurrentRequest);
	TestFalse(TEXT("Unready Pawn/ASC cannot be marked as opened"), F.Shooter->LastOpenedPresetUpgradeRequestId.IsValid());
	F.Shooter->ClientOpenPresetUpgradeUI_Implementation(CurrentRequest, 5, Push);
	TestFalse(TEXT("Matching IDs alone cannot bypass local readiness"), F.Shooter->LastOpenedPresetUpgradeRequestId.IsValid());
	TestFalse(TEXT("Changed local context does not strand a transition blackout"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestFalse(TEXT("Failed UI preparation removes the Slate cover"), F.Shooter->PresetTransitionCover.IsValid());
	TestEqual(TEXT("Cover removal preserves the underlying viewport UI"), ViewportOverlay->GetChildren()->Num(), 1);
	TestTrue(TEXT("Failed UI preparation restores the HUD"), F.Shooter->ShouldShowMainWidget());
	F.Shooter->ClientBeginPresetTransition_Implementation(OldRequest, 4);
	TestFalse(TEXT("Completed generation cannot be restarted by a late begin"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	F.Shooter->ClientBeginPresetTransition_Implementation(CurrentRequest, 6);
	F.Shooter->PendingPresetUpgradeRequestId = CurrentRequest;
	F.Shooter->PendingPresetUpgradeGeneration = 6;
	F.Shooter->AddCameraBlackoutReason(ECameraBlackoutReason::GameOver);
	F.Shooter->GetWorldTimerManager().SetTimer(F.Shooter->PresetUpgradeUIReadyTimer,
		FTimerDelegate::CreateLambda([]() {}), 1.0f, true);
	F.Shooter->ClientCancelPresetUpgradeUI_Implementation(CurrentRequest);
	TestFalse(TEXT("Matching cancellation releases the pending context"), F.Shooter->PendingPresetUpgradeRequestId.IsValid());
	TestFalse(TEXT("Matching cancellation stops readiness polling"),
		F.Shooter->GetWorldTimerManager().IsTimerActive(F.Shooter->PresetUpgradeUIReadyTimer));
	TestEqual(TEXT("Matching cancellation clears the old generation"), F.Shooter->PendingPresetUpgradeGeneration, 0u);
	TestFalse(TEXT("Current cancellation releases transition blackout"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestTrue(TEXT("Transition cleanup preserves independent GameOver blackout"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::GameOver));
	TestEqual(TEXT("Independent GameOver reason keeps the camera covered"), F.Shooter->PlayerCameraManager->FadeAmount, 1.0f);
	TestFalse(TEXT("Transition cleanup restores audio even if a scene-only blackout remains"), F.Shooter->PlayerCameraManager->bFadeAudio);
	F.Shooter->ClientBeginPresetTransition_Implementation(FGuid::NewGuid(), 7);
	F.Shooter->ClientPrepareForArenaExit_Implementation();
	TestFalse(TEXT("Title/terminal recovery releases transition blackout"),
		F.Shooter->HasCameraBlackoutReason(ECameraBlackoutReason::PresetTransition));
	TestFalse(TEXT("Terminal cleanup actually restores the camera"), F.Shooter->PlayerCameraManager->bEnableFading);

	// 실제 음량 계산과 컴포넌트 복구를 검사한다. 사용자 설정은 전환 중 변경해도 유실되지 않아야 한다.
	UOutlierAudioSubsystem* Audio = NewObject<UOutlierAudioSubsystem>(F.World->GetGameInstance());
	Audio->MusicComponent = NewObject<UAudioComponent>(Audio);
	Audio->MusicBaseVolume = 0.8f;
	Audio->SetVolumeMultiplier(EOutlierAudioVolumeType::Master, 0.5f);
	Audio->SetVolumeMultiplier(EOutlierAudioVolumeType::BGM, 0.75f);
	Audio->SetPresetTransitionSuppressed(F.Shooter, true);
	TestTrue(TEXT("Transition audio suppression is active"), Audio->IsPresetTransitionSuppressed());
	TestEqual(TEXT("Existing BGM is muted immediately"), Audio->MusicComponent->VolumeMultiplier, 0.0f);
	TestEqual(TEXT("New SFX also resolve to silence"), Audio->GetCombinedVolumeMultiplier(EOutlierAudioVolumeType::SFX), 0.0f);
	Audio->SetVolumeMultiplier(EOutlierAudioVolumeType::Master, 0.25f);
	Audio->SetPresetTransitionSuppressed(F.Partner, true);
	Audio->SetPresetTransitionSuppressed(F.Shooter, false);
	TestTrue(TEXT("Other local transition owner retains audio suppression"), Audio->IsPresetTransitionSuppressed());
	Audio->SetPresetTransitionSuppressed(F.Partner, false);
	TestFalse(TEXT("Last owner restores audio"), Audio->IsPresetTransitionSuppressed());
	TestEqual(TEXT("User volume changes during blackout are retained"), Audio->GetVolumeMultiplier(EOutlierAudioVolumeType::Master), 0.25f);
	TestTrue(TEXT("BGM restores with current settings"), FMath::IsNearlyEqual(Audio->MusicComponent->VolumeMultiplier, 0.8f * 0.25f * 0.75f));
	return true;
}

#endif
