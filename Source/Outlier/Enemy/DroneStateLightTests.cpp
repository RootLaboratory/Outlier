#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/VECDrone.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "TimerManager.h"
#include <limits>

struct FDroneStateLightTestAccess
{
	static void Initialize(AVECDrone& Drone)
	{
		Drone.InitializeStateSpotLight();
		Drone.RefreshAbilitySystemActorInfo();
		Drone.GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
		Drone.BindGasVitalityObservers();
		Drone.bStateLightReady = true;
		Drone.RefreshStateSpotLight();
	}
	static USpotLightComponent& Light(AVECDrone& Drone) { return *Drone.StateSpotLight; }
	static void Refresh(AVECDrone& Drone) { Drone.RefreshStateSpotLight(); }
	static void ReceiveState(AVECDrone& Drone, EEnemyCombatState State)
	{
		// 클라이언트가 Actor 상태를 받은 뒤 실행하는 실제 경로를 사용한다.
		Drone.CombatState = State;
		Drone.PostNetReceive();
	}
	static bool IsPulsing(AVECDrone& Drone)
	{
		return Drone.GetWorldTimerManager().IsTimerActive(Drone.StateLightPulseTimerHandle);
	}
	static double StartTime(const AVECDrone& Drone) { return Drone.StateLightPulseStartTime; }
	static void SampleAt(AVECDrone& Drone, double Elapsed)
	{
		Drone.StateLightPulseStartTime = Drone.GetWorld()->GetTimeSeconds() - Elapsed;
		Drone.UpdateStateLightPulse();
	}
	static void Apply(AVECDrone& Drone, const FDroneStateLightSettings& Settings)
	{
		Drone.ApplyStateLightSettings(Settings);
	}
	static void SetLease(AVECDrone& Drone, int32 Generation, int32 Lease, EEnemyPoolState State)
	{
		Drone.PoolGameplayGeneration = Generation;
		Drone.PoolLeaseSerial = Lease;
		Drone.PoolState = State;
		Drone.PostNetReceive();
	}
	static void SetEnabled(AVECDrone& Drone, bool bEnabled)
	{
		Drone.bUseStateSpotLight = bEnabled;
		Drone.RefreshStateSpotLight();
	}
	static void EndPresentation(AVECDrone& Drone)
	{
		Drone.EndPlay(EEndPlayReason::RemovedFromWorld);
	}
};

namespace
{
struct FDroneStateLightWorld
{
	UWorld* World = nullptr;

	FDroneStateLightWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DroneStateLightTest")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}
	~FDroneStateLightWorld()
	{
		if (!World) return;
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	AVECDrone* Spawn()
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVECDrone* Drone = World
			? World->SpawnActor<AVECDrone>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters) : nullptr;
		if (Drone)
		{
			// 네이티브 드론은 조명을 생성하지 않는다. BP의 기존 컴포넌트에 해당하는 조명을 준비한다.
			USpotLightComponent* ExistingLight = NewObject<USpotLightComponent>(Drone, TEXT("SpotLight"));
			Drone->AddInstanceComponent(ExistingLight);
			ExistingLight->SetupAttachment(Drone->GetRootComponent());
			ExistingLight->SetRelativeLocation(FVector(10.0f, 20.0f, -30.0f));
			ExistingLight->RegisterComponent();
			FDroneStateLightTestAccess::Initialize(*Drone);
		}
		return Drone;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneStateLightTransitionTest,
	"Outlier.Enemy.StateLight.TransitionsAndPulse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneStateLightTransitionTest::RunTest(const FString& Parameters)
{
	FDroneStateLightWorld Fixture;
	AVECDrone* Drone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	USpotLightComponent& Light = FDroneStateLightTestAccess::Light(*Drone);
	TestEqual(TEXT("Existing BP light is reused"), Light.GetFName(), FName(TEXT("SpotLight")));
	TestTrue(TEXT("Existing attachment is preserved"), Light.GetAttachParent() == Drone->GetRootComponent());
	TestTrue(TEXT("Existing location is preserved"), Light.GetRelativeLocation().Equals(FVector(10.0f, 20.0f, -30.0f)));
	TInlineComponentArray<USpotLightComponent*> SpotLights;
	Drone->GetComponents(SpotLights);
	TestEqual(TEXT("No extra state light is created"), SpotLights.Num(), 1);
	TestTrue(TEXT("NonCombat light is visible"), Light.IsVisible());
	TestTrue(TEXT("NonCombat is white"), Light.GetLightColor().Equals(FLinearColor::White));
	TestEqual(TEXT("NonCombat uses fixed intensity"), Light.Intensity, 1000.0f);
	TestFalse(TEXT("Fixed light has no pulse timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));

	// EnterAlert/Commit은 서버 로컬 통지, ReceiveState는 복제 수신 이후의 적용 경로를 검증한다.
	Drone->EnterAlert(FVector::ZeroVector);
	TestTrue(TEXT("Server local Alert starts a timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	TestTrue(TEXT("Alert is orange"), Light.GetLightColor().Equals(FLinearColor(1.0f, 0.25f, 0.0f), 0.01f));
	TestEqual(TEXT("Alert starts at minimum"), Light.Intensity, 0.0f);
	const double StartTime = FDroneStateLightTestAccess::StartTime(*Drone);
	FDroneStateLightTestAccess::ReceiveState(*Drone, EEnemyCombatState::Alert);
	TestEqual(TEXT("Repeated receive preserves phase"), FDroneStateLightTestAccess::StartTime(*Drone), StartTime);
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		// TimerManager는 엔진 프레임당 한 번만 실행되므로 수동 World Tick마다 프레임도 진행한다.
		++GFrameCounter;
		Fixture.World->Tick(LEVELTICK_All, 0.05f);
	}
	TestTrue(TEXT("Timer advances brightness without manual updates"), Light.Intensity > 0.0f);
	TestEqual(TEXT("Timer callbacks preserve start time"), FDroneStateLightTestAccess::StartTime(*Drone), StartTime);
	FDroneStateLightTestAccess::SampleAt(*Drone, 0.25);
	TestTrue(TEXT("Quarter period is midpoint"), FMath::IsNearlyEqual(Light.Intensity, 500.0f, 0.1f));
	FDroneStateLightTestAccess::SampleAt(*Drone, 0.5);
	TestTrue(TEXT("Half period is maximum"), FMath::IsNearlyEqual(Light.Intensity, 1000.0f, 0.1f));
	FDroneStateLightTestAccess::SampleAt(*Drone, 1.0);
	TestTrue(TEXT("Full period returns to minimum"), FMath::IsNearlyZero(Light.Intensity, 0.1f));
	TestTrue(TEXT("Server clears Alert"), Drone->CommitAlertToNonCombat());
	TestFalse(TEXT("Leaving Alert clears timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	TestEqual(TEXT("Server restores fixed brightness"), Light.Intensity, 1000.0f);
	FDroneStateLightTestAccess::ReceiveState(*Drone, EEnemyCombatState::Combat);
	TestTrue(TEXT("Received Combat is red"), Light.GetLightColor().Equals(FLinearColor::Red));
	TestFalse(TEXT("Combat has no timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneStateLightLifetimeTest,
	"Outlier.Enemy.StateLight.StunDeathAndLease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneStateLightLifetimeTest::RunTest(const FString& Parameters)
{
	FDroneStateLightWorld Fixture;
	AVECDrone* Drone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	USpotLightComponent& Light = FDroneStateLightTestAccess::Light(*Drone);
	Drone->EnterAlert(FVector::ZeroVector);
	UOutlierAbilitySystemComponent* ASC = Drone->GetOutlierAbilitySystemComponent();
	ASC->AddLooseGameplayTag(OutlierGameplayTags::State::Stunned());
	TestFalse(TEXT("Stun hides light"), Light.IsVisible());
	TestFalse(TEXT("Stun clears timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	ASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::Stunned());
	TestTrue(TEXT("Stun removal restores Alert"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	FDroneStateLightTestAccess::SetLease(*Drone, 1, 1, EEnemyPoolState::CombatActive);
	FDroneStateLightTestAccess::SampleAt(*Drone, 0.5);
	FDroneStateLightTestAccess::SetLease(*Drone, 1, 2, EEnemyPoolState::CombatActive);
	TestEqual(TEXT("Same-state new lease restarts at minimum"), Light.Intensity, 0.0f);
	FDroneStateLightTestAccess::SetLease(*Drone, 1, 2, EEnemyPoolState::Idle);
	TestFalse(TEXT("Idle hides light"), Light.IsVisible());
	TestFalse(TEXT("Idle clears timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	FDroneStateLightTestAccess::SetLease(*Drone, 2, 3, EEnemyPoolState::CombatActive);
	TestTrue(TEXT("New generation restores light"), Light.IsVisible());
	FDroneStateLightTestAccess::SetEnabled(*Drone, false);
	TestFalse(TEXT("Disabled feature hides light"), Light.IsVisible());
	TestFalse(TEXT("Disabled feature clears timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	FDroneStateLightTestAccess::SetEnabled(*Drone, true);
	// CombatState가 그대로여도 GAS 사망 통지만으로 고정 조명이 꺼져야 한다.
	FDroneStateLightTestAccess::ReceiveState(*Drone, EEnemyCombatState::Combat);
	ASC->AddLooseGameplayTag(OutlierGameplayTags::State::Dead());
	TestFalse(TEXT("Dead tag hides a fixed light"), Light.IsVisible());
	ASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::Dead());
	FDroneStateLightTestAccess::ReceiveState(*Drone, EEnemyCombatState::Alert);
	FDroneStateLightTestAccess::EndPresentation(*Drone);
	FDroneStateLightTestAccess::Refresh(*Drone);
	TestFalse(TEXT("EndPlay cannot re-enable light"), Light.IsVisible());
	TestFalse(TEXT("EndPlay clears timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneStateLightSettingsTest,
	"Outlier.Enemy.StateLight.InvalidSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneStateLightSettingsTest::RunTest(const FString& Parameters)
{
	FDroneStateLightWorld Fixture;
	AVECDrone* Drone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	USpotLightComponent& Light = FDroneStateLightTestAccess::Light(*Drone);
	FDroneStateLightSettings Settings;
	Settings.bPulseEnabled = true;
	Settings.MinIntensity = 900.0f;
	Settings.MaxIntensity = 100.0f;
	// 대괄호를 정규식 문자 집합으로 해석하지 않도록 로그 문자열 자체를 비교한다.
	AddExpectedErrorPlain(TEXT("[DroneStateLight] Invalid settings corrected"), EAutomationExpectedErrorFlags::Contains, 6);
	FDroneStateLightTestAccess::Apply(*Drone, Settings);
	TestEqual(TEXT("Reversed range starts at lower bound"), Light.Intensity, 100.0f);
	TestEqual(TEXT("Settings source is unchanged"), Settings.MinIntensity, 900.0f);
	Settings.MinIntensity = -10.0f;
	Settings.MaxIntensity = 200.0f;
	FDroneStateLightTestAccess::Apply(*Drone, Settings);
	TestEqual(TEXT("Negative minimum clamps to zero"), Light.Intensity, 0.0f);
	Settings.MinIntensity = std::numeric_limits<float>::quiet_NaN();
	Settings.MaxIntensity = std::numeric_limits<float>::infinity();
	FDroneStateLightTestAccess::Apply(*Drone, Settings);
	TestEqual(TEXT("Nonfinite brightness clamps to zero"), Light.Intensity, 0.0f);
	TestFalse(TEXT("Equal bounds need no timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	Settings.MinIntensity = 0.0f;
	Settings.MaxIntensity = 200.0f;
	Settings.PulsePeriod = 0.0f;
	FDroneStateLightTestAccess::Apply(*Drone, Settings);
	TestEqual(TEXT("Invalid period uses fixed maximum"), Light.Intensity, 200.0f);
	TestFalse(TEXT("Invalid period creates no timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	Settings.PulsePeriod = std::numeric_limits<float>::quiet_NaN();
	FDroneStateLightTestAccess::Apply(*Drone, Settings);
	TestEqual(TEXT("Nonfinite period uses fixed maximum"), Light.Intensity, 200.0f);
	TestFalse(TEXT("Nonfinite period creates no timer"), FDroneStateLightTestAccess::IsPulsing(*Drone));
	Settings.bPulseEnabled = false;
	Settings.MaxIntensity = -1.0f;
	FDroneStateLightTestAccess::Apply(*Drone, Settings);
	TestEqual(TEXT("Negative fixed maximum clamps to zero"), Light.Intensity, 0.0f);
	return true;
}

#endif
