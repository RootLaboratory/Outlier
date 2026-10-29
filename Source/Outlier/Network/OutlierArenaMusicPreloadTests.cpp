#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierArenaSettings.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierArenaMusicPreloadTest,
	"Outlier.Audio.Music.LevelPreload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierArenaMusicPreloadTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Start = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Bgm.inbattle.Start"));
	const FGameplayTag Loop = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Bgm.inbattle.Loop"));
	const TSoftObjectPtr<UWorld> Original(FSoftObjectPath(TEXT("/Game/Maps/TestAudio.TestAudio")));
	const TSoftObjectPtr<UWorld> PIE(FSoftObjectPath(TEXT("/Game/Maps/UEDPIE_2_TestAudio.UEDPIE_2_TestAudio")));
	TStrongObjectPtr<UOutlierArenaSettings> Settings(NewObject<UOutlierArenaSettings>());
	Settings->BgmPreloadContexts.Reset();
	Settings->BgmPreloadContexts.FindOrAdd(Original).AddTag(Start);
	Settings->BgmPreloadContexts.FindOrAdd(PIE).AddTag(Loop);
	Settings->BgmPreloadContexts.FindOrAdd(PIE).AddTag(Start);
	Settings->BgmPreloadContexts.FindOrAdd(TSoftObjectPtr<UWorld>()).AddTag(Start);
	Settings->BgmPreloadContexts.FindOrAdd(TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Empty.Empty"))));
	TMap<FString, TArray<FGameplayTag>> Mappings;
	Settings->BuildBgmPreloadMappings(Mappings);
	TestEqual(TEXT("PIE and original map settings merge; empty settings are omitted"), Mappings.Num(), 1);
	const TArray<FGameplayTag>* Contexts = Mappings.Find(TEXT("/Game/Maps/TestAudio"));
	if (!TestNotNull(TEXT("Original package is the lookup key"), Contexts)) return false;
	TestEqual(TEXT("Repeated tags are deduplicated"), Contexts->Num(), 2);
	TestTrue(TEXT("Explicit transition contexts are preserved"), Contexts->Contains(Start) && Contexts->Contains(Loop));

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UWorld> World(NewObject<UWorld>());
	World->SetGameInstance(GameInstance.Get());
	TStrongObjectPtr<UOutlierAudioSubsystem> Audio(NewObject<UOutlierAudioSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UOutlierArenaSubsystem> Arena(NewObject<UOutlierArenaSubsystem>(World.Get()));
	Arena->CachedAudioSubsystem = Audio.Get();
	Arena->BgmPreloadMappings = Mappings;
	ULevelStreamingDynamic* FirstInstance = NewObject<ULevelStreamingDynamic>(World.Get());
	ULevelStreamingDynamic* SecondInstance = NewObject<ULevelStreamingDynamic>(World.Get());
	FirstInstance->SetWorldAsset(Original);
	FirstInstance->SetShouldBeLoaded(true);
	SecondInstance->SetWorldAsset(PIE);
	SecondInstance->SetShouldBeLoaded(false);
	World->AddStreamingLevel(FirstInstance);
	World->AddStreamingLevel(SecondInstance);
	Arena->PreloadInitiallyRequestedLevelMusic();
	TestEqual(TEXT("Early inventory observes only existing load requests"), Arena->LevelMusicPreloadRequests.Num(), 1);
	TestFalse(TEXT("Music inventory does not load an unrequested level"), SecondInstance->ShouldBeLoaded());
	TestFalse(TEXT("Music inventory does not wait for loaded level content"), FirstInstance->IsLevelLoaded());
	Arena->SetLevelMusicPreload(SecondInstance, PIE, true);
	TestEqual(TEXT("Each instance owns a distinct request"), Arena->LevelMusicPreloadRequests.Num(), 2);
	const uint64 FirstRequest = Arena->LevelMusicPreloadRequests.FindChecked(FirstInstance);
	const uint64 SecondRequest = Arena->LevelMusicPreloadRequests.FindChecked(SecondInstance);
	TestTrue(TEXT("Same map instances have distinct lifetimes"), FirstRequest != SecondRequest);
	Arena->SetLevelMusicPreload(FirstInstance, Original, true);
	TestEqual(TEXT("Repeated load does not acquire twice"), Arena->LevelMusicPreloadRequests.FindChecked(FirstInstance), FirstRequest);
	Arena->SetLevelMusicPreload(FirstInstance, Original, false);
	TestTrue(TEXT("Unload cancels the corresponding request"), Audio->GetMusicPreloadState(FirstRequest) == EOutlierMusicPreloadState::Released);
	TestTrue(TEXT("Another instance is still waiting for its metadata"), Audio->GetMusicPreloadState(SecondRequest) == EOutlierMusicPreloadState::Waiting);
	Arena->SetLevelMusicPreload(FirstInstance, Original, true);
	TestTrue(TEXT("Reload acquires a new lifetime"), Arena->LevelMusicPreloadRequests.FindChecked(FirstInstance) != FirstRequest);
	Arena->ClearLevelMusicPreloads();
	TestTrue(TEXT("Arena teardown removes instance ownership"), Arena->LevelMusicPreloadRequests.IsEmpty());
	TestTrue(TEXT("Arena teardown releases remaining audio requests"), Audio->GetMusicPreloadState(SecondRequest) == EOutlierMusicPreloadState::Released);
	return true;
}

#endif
