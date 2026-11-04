#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "OutlierPlayerState.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerSprintInputModeTest,
	"Outlier.Partner.SprintInputMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerSprintInputModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerSprintInputTest")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	APartnerPlayerController* Controller = World->SpawnActor<APartnerPlayerController>();
	if (!TestNotNull(TEXT("Partner exists"), Partner)
		|| !TestNotNull(TEXT("Controller exists"), Controller)) return false;
	// 입력 상태만 검증한다. Possess의 데이터/무기 초기화와 사용자 세이브는 실행하지 않는다.
	Partner->Controller = Controller;
	Controller->SetPawn(Partner);
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = LocalPlayer;
	Partner->AccelerateInputKeys = { EKeys::LeftShift };
	TestTrue(TEXT("Fixture is locally controlled"), Partner->IsLocallyControlled());
	const auto KeyEvent = [&](FKey Key, EInputEvent Event)
	{
		Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, Event, uint64(0)));
	};
	const auto Press = [&]()
	{
		// 실제 Controller 키 경로가 재입력을 허용한 뒤 액션의 Started를 전달한다.
		KeyEvent(EKeys::LeftShift, IE_Pressed);
		Partner->ToggleAccelerate();
	};
	const auto Release = [&]()
	{
		KeyEvent(EKeys::LeftShift, IE_Released);
		Partner->ReleaseAccelerate();
	};

	Partner->ResetAccelerateInput();
	Partner->ToggleAccelerate();
	TestFalse(TEXT("Synthetic Started after reset cannot enable boost"), Partner->bIsAccelerate != 0);
	Partner->ReleaseAccelerate();
	Partner->ToggleAccelerate();
	TestFalse(TEXT("Completed after Flush does not rearm boost"), Partner->bIsAccelerate != 0);
	KeyEvent(EKeys::SpaceBar, IE_Pressed);
	TestTrue(TEXT("Unrelated physical key cannot rearm boost"), Partner->bAccelerateNeedsFreshPress);

	Press();
	TestTrue(TEXT("Toggle fresh Press enables boost"), Partner->bIsAccelerate != 0);
	Partner->ToggleAccelerate();
	TestTrue(TEXT("Duplicate Started cannot toggle twice"), Partner->bIsAccelerate != 0);
	Release();
	TestTrue(TEXT("Toggle Release preserves boost"), Partner->bIsAccelerate != 0);
	Press();
	TestFalse(TEXT("Toggle next Press disables boost"), Partner->bIsAccelerate != 0);
	Release();

	Partner->HandleAccelerateModeChanged(EOutlierPlayerRole::Partner, EOutlierSprintInputMode::Hold);
	TestTrue(TEXT("Mode change requires a fresh physical Press"), Partner->bAccelerateNeedsFreshPress);
	Press();
	TestTrue(TEXT("Hold Press enables boost"), Partner->bIsAccelerate != 0);
	Partner->HandleAccelerateModeChanged(EOutlierPlayerRole::Shooter, EOutlierSprintInputMode::Toggle);
	TestEqual(TEXT("Other role preference leaves Partner mode unchanged"),
		Partner->AccelerateInputMode, EOutlierSprintInputMode::Hold);
	TestTrue(TEXT("Other role preference preserves current boost"), Partner->bIsAccelerate != 0);
	Release();
	TestFalse(TEXT("Hold Release disables boost"), Partner->bIsAccelerate != 0);
	Press();
	Partner->HandleAccelerateModeChanged(EOutlierPlayerRole::Partner, EOutlierSprintInputMode::Toggle);
	TestFalse(TEXT("Changing mode clears current boost"), Partner->bIsAccelerate != 0);
	Partner->ReleaseAccelerate();
	Partner->ToggleAccelerate();
	TestFalse(TEXT("Changing mode cannot reuse the previous hold"), Partner->bIsAccelerate != 0);
	Press();
	TestTrue(TEXT("Fresh Press uses the new mode"), Partner->bIsAccelerate != 0);
	Partner->ReleaseAccelerate();
	Partner->HandleAccelerateContextChanged(EOutlierPlayerRole::None);
	TestFalse(TEXT("Role context boundary clears boost"), Partner->bIsAccelerate != 0);
	TestTrue(TEXT("Role context boundary requires fresh Press"), Partner->bAccelerateNeedsFreshPress);

	ULocalPlayerSettingsSubsystem* Settings = NewObject<ULocalPlayerSettingsSubsystem>(LocalPlayer);
	// 직접 콜백 호출과 달리 실제 설정 알림의 연결/해제 수명도 함께 검사한다.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Settings->OnSprintInputModeChanged.AddUniqueDynamic(Partner, &APartnerCharacter::HandleAccelerateModeChanged);
	}
	TestEqual(TEXT("Repeated subscription retains exactly one Partner listener"),
		Settings->OnSprintInputModeChanged.GetAllObjects().Num(), 1);
	for (EOutlierSprintInputMode Mode : { EOutlierSprintInputMode::Toggle, EOutlierSprintInputMode::Hold })
	{
		Settings->OnSprintInputModeChanged.Broadcast(EOutlierPlayerRole::Partner, Mode);
		TestEqual(TEXT("Settings notification applies the current role mode"), Partner->AccelerateInputMode, Mode);
		Release();
		Press();
		TestTrue(TEXT("Controller Press enables boost before focus loss"), Partner->bIsAccelerate != 0);
		Controller->FlushPressedKeys();
		TestFalse(TEXT("Controller Flush clears boost in both input modes"), Partner->bIsAccelerate != 0);
		Partner->ReleaseAccelerate();
		Partner->ToggleAccelerate();
		TestFalse(TEXT("Synthetic Release and Started after Flush cannot resume boost"), Partner->bIsAccelerate != 0);
		KeyEvent(EKeys::LeftShift, IE_Repeat);
		Partner->ToggleAccelerate();
		TestFalse(TEXT("Key repeat after Flush cannot count as a fresh Press"), Partner->bIsAccelerate != 0);
		Release();
		Press();
		TestTrue(TEXT("Real Release and Press after Flush restore boost"), Partner->bIsAccelerate != 0);
		Release();
		TestEqual(TEXT("Physical Release follows the current notified mode"),
			Partner->bIsAccelerate != 0, Mode == EOutlierSprintInputMode::Toggle);
		Partner->ResetAccelerateInput();
	}
	Settings->OnSprintInputModeChanged.RemoveAll(Partner);
	Settings->OnSprintInputModeChanged.Broadcast(EOutlierPlayerRole::Partner, EOutlierSprintInputMode::Toggle);
	TestEqual(TEXT("Detached settings notification cannot change Partner mode"),
		Partner->AccelerateInputMode, EOutlierSprintInputMode::Hold);
	Settings->OnSprintInputModeChanged.AddUniqueDynamic(Partner, &APartnerCharacter::HandleAccelerateModeChanged);
	Settings->OnSprintInputModeChanged.Broadcast(EOutlierPlayerRole::Partner, EOutlierSprintInputMode::Toggle);
	TestEqual(TEXT("Rebound settings notification applies once"), Partner->AccelerateInputMode, EOutlierSprintInputMode::Toggle);
	Settings->OnSprintInputModeChanged.RemoveAll(Partner);

	const uint32 OldEpoch = Partner->AccelerateInputEpoch;
	Partner->ResetAccelerateInput();
	Partner->ServerSetAccelerate_Implementation(true, 100, OldEpoch);
	TestFalse(TEXT("Old lifecycle ON cannot revive boost"), Partner->bIsAccelerate != 0);
	const uint32 CurrentEpoch = Partner->AccelerateInputEpoch;
	Partner->ServerSetAccelerate_Implementation(true, 1, CurrentEpoch);
	TestTrue(TEXT("Current epoch ON is accepted"), Partner->bIsAccelerate != 0);
	Partner->ServerSetAccelerate_Implementation(false, 2, CurrentEpoch);
	Partner->ServerSetAccelerate_Implementation(true, 1, CurrentEpoch);
	TestFalse(TEXT("Older ON cannot override a newer OFF"), Partner->bIsAccelerate != 0);
	Partner->ServerSetAccelerate_Implementation(true, 3, CurrentEpoch);
	Partner->OutlierAbilitySystemComponent->AddLooseGameplayTag(OutlierGameplayTags::State::Dead());
	TestFalse(TEXT("Dead fixture blocks new input"), Partner->CanAcceptInput());
	Partner->ServerSetAccelerate_Implementation(false, 4, CurrentEpoch);
	TestFalse(TEXT("OFF cleanup is accepted while gameplay input is blocked"), Partner->bIsAccelerate != 0);
	Partner->ServerSetAccelerate_Implementation(true, 5, CurrentEpoch);
	TestFalse(TEXT("Server still rejects ON while blocked"), Partner->bIsAccelerate != 0);
	Partner->OutlierAbilitySystemComponent->RemoveLooseGameplayTag(OutlierGameplayTags::State::Dead());

	Controller->SetPawn(nullptr);
	Controller->Player = nullptr;
	Partner->Controller = nullptr;
	return true;
}

#endif
