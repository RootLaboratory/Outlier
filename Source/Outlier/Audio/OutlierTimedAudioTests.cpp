#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierTimedAudioWindowTest,
	"Outlier.Audio.TimedEmitter.PlaybackWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierTimedAudioWindowTest::RunTest(const FString& Parameters)
{
	FOutlierAudioPlaybackWindow Window;
	Window.ServerStartTime = 100.0;
	Window.SoundDuration = 4.029208f;
	float Time = 0.0f;
	for (const float Duration : {1.0f, 2.0f, 4.029208f})
	{
		Window.Duration = Duration;
		TestTrue(TEXT("Valid duration starts"), Window.GetPlaybackTime(100.0, Time));
		TestTrue(TEXT("Starts at the sound tail"), FMath::IsNearlyEqual(Time, Window.SoundDuration - Duration));
		TestTrue(TEXT("Delayed load joins current time"), Window.GetPlaybackTime(100.25, Time));
		TestTrue(TEXT("Delay advances sound and curve together"),
			FMath::IsNearlyEqual(Time, Window.SoundDuration - Duration + 0.25f));
		TestFalse(TEXT("Completed window cannot replay"), Window.GetPlaybackTime(100.0 + Duration, Time));
	}
	for (const float Invalid : {0.0f, -1.0f, 5.0f,
		std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		Window.Duration = Invalid;
		TestFalse(TEXT("Invalid or unsupported duration rejected"), Window.GetPlaybackTime(100.0, Time));
	}
	Window.Duration = 2.0f;
	Window.SoundDuration = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Invalid sound length rejected"), Window.GetPlaybackTime(100.0, Time));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierTimedAudioLifecycleTest,
	"Outlier.Audio.TimedEmitter.StaleRequests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierTimedAudioLifecycleTest::RunTest(const FString& Parameters)
{
	if (!GEngine) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
	};
	AActor* Emitter = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Emitter"), Emitter)) return false;
	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UOutlierAudioSubsystem> Audio(NewObject<UOutlierAudioSubsystem>(GameInstance.Get()));
	UOutlierAudioSubsystem::FPendingPlay Pending;
	Pending.AttachedEmitter = Emitter;
	Pending.AudioInstanceId = -1;
	Pending.Playback.ServerStartTime = 100.0;
	Pending.Playback.Duration = 2.0f;
	Pending.Playback.SoundDuration = 4.029208f;
	const UOutlierAudioSubsystem::FLoopPlaybackKey Key { Emitter, FGameplayTag() };
	auto& Current = Audio->LocalEmitterLoops.Add(Key);
	Current.bTimedSound = true;
	Current.InstanceId = -1;
	float Time = 0.0f;
	TestTrue(TEXT("Current pending request can seek"), Audio->ResolveTimedEmitterStartTime(Pending, 100.5, Time));
	// 원본 길이를 float로 저장한 뒤 계산한 값과 리터럴의 반올림 차이만 허용한다.
	TestTrue(TEXT("Async completion uses current position"), FMath::IsNearlyEqual(Time, 2.529208f, 1.e-6f));
	Current.InstanceId = -2;
	TestFalse(TEXT("New attempt invalidates old completion"), Audio->ResolveTimedEmitterStartTime(Pending, 100.5, Time));
	Pending.AudioInstanceId = -2;
	TestTrue(TEXT("New attempt can play"), Audio->ResolveTimedEmitterStartTime(Pending, 100.5, Time));
	TestFalse(TEXT("Expired completion is ignored"), Audio->ResolveTimedEmitterStartTime(Pending, 102.0, Time));
	Audio->LocalEmitterLoops.Remove(Key);
	TestFalse(TEXT("Cancellation invalidates pending completion"), Audio->ResolveTimedEmitterStartTime(Pending, 100.5, Time));
	return true;
}

#endif
