#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Sound/SoundCue.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMusicPreloadTest,
	"Outlier.Audio.Music.Preload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMusicPreloadTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UOutlierAudioSubsystem> Audio(NewObject<UOutlierAudioSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UWorld> World(NewObject<UWorld>());
	TStrongObjectPtr<UWorld> OtherWorld(NewObject<UWorld>());
	World->SetGameInstance(GameInstance.Get());
	OtherWorld->SetGameInstance(GameInstance.Get());
	const auto Tag = [](const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name)); };
	const FGameplayTag Type = Tag(TEXT("Audio.Type.BGM"));
	const FGameplayTag Start = Tag(TEXT("Audio.Context.Bgm.inbattle.Start"));
	const FGameplayTag Loop = Tag(TEXT("Audio.Context.Bgm.inbattle.Loop"));
	const FGameplayTag End = Tag(TEXT("Audio.Context.Bgm.inbattle.End"));
	USoundCue* First = NewObject<USoundCue>();
	USoundCue* Second = NewObject<USoundCue>();
	USoundCue* Unrelated = NewObject<USoundCue>();
	const FSoftObjectPath FirstPath(First);
	const FSoftObjectPath SecondPath(Second);
	auto& Entries = Audio->CatalogEntriesByType.Add(Type);
	const auto AddEntry = [&Entries](FGameplayTag Context, USoundBase* Sound, float Weight = 1.0f)
	{
		UOutlierAudioSubsystem::FRuntimeCatalogEntry Entry;
		Entry.RequiredContext = Context;
		Entry.Sound = Sound;
		Entry.Weight = Weight;
		Entries.Add(Entry);
	};
	AddEntry(Start, First);
	AddEntry(Start, Second, 3.0f);
	AddEntry(Start, First); // 같은 음원을 가진 Variant도 선로드 참조는 하나로 합쳐져야 한다.
	AddEntry(Start, Unrelated, 0.0f);
	AddEntry(Start, nullptr);
	AddEntry(Loop, First);
	AddEntry(Type, Unrelated); // 부모 Context는 정확히 일치하는 요청에 섞이지 않아야 한다.
	AddEntry(FGameplayTag(), Unrelated);

	TestEqual(TEXT("Null world is rejected"), Audio->RequestMusicPreload(nullptr, { Start }), uint64(0));
	TestEqual(TEXT("Empty request is rejected"), Audio->RequestMusicPreload(World.Get(), {}), uint64(0));
	World->bIsTearingDown = true;
	TestEqual(TEXT("Tearing down world cannot acquire new residency"), Audio->RequestMusicPreload(World.Get(), { Start }), uint64(0));
	World->bIsTearingDown = false;
	const uint64 FirstRequest = Audio->RequestMusicPreload(World.Get(), { Start, Start });
	TestTrue(TEXT("Metadata discovery is awaited"),
		Audio->GetMusicPreloadState(FirstRequest) == EOutlierMusicPreloadState::Waiting);
	Audio->bBankDiscoveryComplete = true;
	Audio->ResumeMusicPreloads();
	TestTrue(TEXT("Loaded variants are immediately ready"),
		Audio->GetMusicPreloadState(FirstRequest) == EOutlierMusicPreloadState::Ready);
	TestEqual(TEXT("All distinct valid variants are retained"), Audio->RetainedMusicPreloadSounds.Num(), 2);
	TestEqual(TEXT("Repeated Context/Variant acquires one reference"), Audio->MusicPreloadSounds[FirstPath].Users, 1);
	TestEqual(TEXT("Preload does not choose a music Context"), Audio->DesiredMusicContext, FGameplayTag());
	TestNull(TEXT("Preload does not start playback"), Audio->MusicComponent.Get());

	const uint64 SecondRequest = Audio->RequestMusicPreload(OtherWorld.Get(), { Loop });
	TestEqual(TEXT("Different requests share an existing sound"), Audio->MusicPreloadSounds[FirstPath].Users, 2);
	CollectGarbage(RF_NoFlags);
	TestNotNull(TEXT("Retained sound survives garbage collection"), FirstPath.ResolveObject());
	TestNotNull(TEXT("Every selected variant survives garbage collection"), SecondPath.ResolveObject());
	Audio->ReleaseMusicPreload(FirstRequest);
	Audio->ReleaseMusicPreload(FirstRequest);
	TestTrue(TEXT("Repeated release is harmless"),
		Audio->GetMusicPreloadState(FirstRequest) == EOutlierMusicPreloadState::Released);
	TestEqual(TEXT("Shared sound remains for the other world"), Audio->RetainedMusicPreloadSounds.Num(), 1);
	TestEqual(TEXT("Last remaining request owns one reference"), Audio->MusicPreloadSounds[FirstPath].Users, 1);

	// 선로드 해제가 재생 컴포넌트의 소유권까지 끊지 않는지 확인한다.
	Audio->MusicComponent = NewObject<UAudioComponent>(Audio.Get());
	Audio->MusicComponent->SetSound(First);
	Audio->ReleaseMusicPreloadsForWorld(World.Get());
	TestTrue(TEXT("Other world request is preserved"),
		Audio->GetMusicPreloadState(SecondRequest) == EOutlierMusicPreloadState::Ready);
	UWorld* CleanupWorld = OtherWorld.Get();
	CleanupWorld->MarkAsGarbage();
	Audio->ReleaseMusicPreloadsForWorld(CleanupWorld);
	TestTrue(TEXT("World cleanup releases even a garbage-marked weak world"),
		Audio->GetMusicPreloadState(SecondRequest) == EOutlierMusicPreloadState::Released);
	TestEqual(TEXT("Last release removes preload residency"), Audio->RetainedMusicPreloadSounds.Num(), 0);
	TestTrue(TEXT("Playback retains its own sound"), Audio->MusicComponent->GetSound() == First);

	AddExpectedError(TEXT("has missing Context/Variant entries"), EAutomationExpectedErrorFlags::Contains, 2);
	const uint64 Missing = Audio->RequestMusicPreload(World.Get(), { End });
	TestTrue(TEXT("Missing exact Context fails without waiting forever"),
		Audio->GetMusicPreloadState(Missing) == EOutlierMusicPreloadState::Failed);
	const uint64 Partial = Audio->RequestMusicPreload(World.Get(), { Start, End });
	TestTrue(TEXT("Partially valid requests do not claim all sounds ready"),
		Audio->GetMusicPreloadState(Partial) == EOutlierMusicPreloadState::Failed);
	TestEqual(TEXT("Valid sounds in a partial request remain retained"), Audio->RetainedMusicPreloadSounds.Num(), 2);
	Audio->ClearMusicPreloads();

	// 디스크 타이밍에 의존하지 않도록 진행 중 요청을 구성하고, 실제 완료/해제 경로를 호출한다.
	const auto AddPending = [&Audio, &World](const FSoftObjectPath& Path)
	{
		const uint64 RequestId = ++Audio->NextMusicPreloadRequestId;
		auto& Request = Audio->MusicPreloadRequests.Add(RequestId);
		Request.World = World.Get();
		Request.State = EOutlierMusicPreloadState::Loading;
		Request.SoundPaths.Add(Path);
		auto& Sound = Audio->MusicPreloadSounds.Add(Path);
		Sound.Users = 1;
		Sound.Serial = ++Audio->NextMusicPreloadSoundSerial;
		return RequestId;
	};
	const uint64 PendingOwner = AddPending(FirstPath);
	const uint64 SharedPending = Audio->RequestMusicPreload(World.Get(), { Loop });
	TestEqual(TEXT("In-flight sound is shared without a second acquire"), Audio->MusicPreloadSounds[FirstPath].Users, 2);
	Audio->ReleaseMusicPreload(PendingOwner);
	TestTrue(TEXT("One cancellation preserves another in-flight request"),
		Audio->GetMusicPreloadState(SharedPending) == EOutlierMusicPreloadState::Loading);
	Audio->HandleMusicPreloadSoundLoaded(FirstPath, Audio->MusicPreloadSounds[FirstPath].Serial);
	TestTrue(TEXT("Shared in-flight completion reaches the remaining requester"),
		Audio->GetMusicPreloadState(SharedPending) == EOutlierMusicPreloadState::Ready);
	Audio->ReleaseMusicPreload(SharedPending);

	const uint64 Cancelled = AddPending(FirstPath);
	const uint64 OldSerial = Audio->MusicPreloadSounds[FirstPath].Serial;
	Audio->ReleaseMusicPreload(Cancelled);
	const uint64 Replacement = AddPending(FirstPath);
	Audio->HandleMusicPreloadSoundLoaded(FirstPath, OldSerial);
	TestTrue(TEXT("Old callback cannot complete the replacement request"),
		Audio->GetMusicPreloadState(Replacement) == EOutlierMusicPreloadState::Loading);
	Audio->HandleMusicPreloadSoundLoaded(FirstPath, Audio->MusicPreloadSounds[FirstPath].Serial);
	TestTrue(TEXT("Current callback completes its request"),
		Audio->GetMusicPreloadState(Replacement) == EOutlierMusicPreloadState::Ready);
	Audio->ClearMusicPreloads();
	Audio->HandleMusicPreloadSoundLoaded(FirstPath, OldSerial);
	TestTrue(TEXT("Cleared callbacks cannot restore residency"), Audio->RetainedMusicPreloadSounds.IsEmpty());
	TestTrue(TEXT("Cleared request reports released"),
		Audio->GetMusicPreloadState(Replacement) == EOutlierMusicPreloadState::Released);

	const FSoftObjectPath NotSound(World.Get());
	const uint64 FailedLoad = AddPending(NotSound);
	AddExpectedError(TEXT("Failed to preload BGM"), EAutomationExpectedErrorFlags::Contains, 1);
	Audio->HandleMusicPreloadSoundLoaded(NotSound, Audio->MusicPreloadSounds[NotSound].Serial);
	TestTrue(TEXT("Failed sound resolution terminates the request"),
		Audio->GetMusicPreloadState(FailedLoad) == EOutlierMusicPreloadState::Failed);
	Audio->ClearMusicPreloads();
	return true;
}

#endif
