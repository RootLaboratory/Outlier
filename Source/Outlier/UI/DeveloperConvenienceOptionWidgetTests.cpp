#if WITH_DEV_AUTOMATION_TESTS

#include "UI/DeveloperConvenienceOptionWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceOptionWidgetTest,
	"Outlier.UI.DeveloperConvenience.OptionWidgetLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceOptionWidgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Option widget world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	auto* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	ON_SCOPE_EXIT
	{
		Controller->Player = nullptr;
		Player->PlayerController = nullptr;
	};
	for (EDeveloperConvenienceOption Option : { EDeveloperConvenienceOption::Invincible,
		EDeveloperConvenienceOption::ConsumeCurrency, EDeveloperConvenienceOption::FastCooldown })
	{
		auto* Widget = CreateWidget<UDeveloperConvenienceOptionWidget>(Controller);
		if (!TestNotNull(TEXT("Reusable option widget"), Widget))
		{
			return false;
		}
		Widget->SetPlayerContext(FLocalPlayerContext(Player, World));
		Widget->Button = NewObject<UButton>(Widget);
		Widget->Title = NewObject<UTextBlock>(Widget);
		Widget->Desc = NewObject<UTextBlock>(Widget);
		Widget->ONOFF = NewObject<UTextBlock>(Widget);
		Widget->Option = Option;
		Widget->NativeConstruct();
		ON_SCOPE_EXIT { Widget->NativeDestruct(); };
		TestFalse(TEXT("No session cannot change an option"), Widget->Button->GetIsEnabled());
		TestFalse(TEXT("Option provides a default title"), Widget->Title->GetText().IsEmpty());
		TestFalse(TEXT("Option provides a default description"), Widget->Desc->GetText().IsEmpty());
		FDeveloperConvenienceSnapshot Snapshot;
		Snapshot.MatchId = FGuid::NewGuid();
		Snapshot.SessionId = FGuid::NewGuid();
		Snapshot.bIsOperator = true;
		Widget->HandleStateChanged(Snapshot);
		TestTrue(TEXT("Operator can click in an active session"), Widget->Button->GetIsEnabled());
		const bool bInitiallyOn = Option == EDeveloperConvenienceOption::ConsumeCurrency;
		TestEqual(TEXT("Initial text follows the selected server option"), Widget->ONOFF->GetText().ToString(),
			FString(bInitiallyOn ? TEXT("ON") : TEXT("OFF")));
		switch (Option)
		{
		case EDeveloperConvenienceOption::Invincible:
			Snapshot.Options.bInvincible = true;
			break;
		case EDeveloperConvenienceOption::ConsumeCurrency:
			Snapshot.Options.bConsumeCurrency = false;
			break;
		case EDeveloperConvenienceOption::FastCooldown:
			Snapshot.Options.bFastCooldown = true;
			break;
		}
		Controller->OnDeveloperConvenienceStateChanged.Broadcast(Snapshot);
		TestEqual(TEXT("Server notification updates the selected option"), Widget->ONOFF->GetText().ToString(),
			FString(bInitiallyOn ? TEXT("OFF") : TEXT("ON")));
		// 서버 GameMode가 없는 요청은 거절된다. 클릭만으로 확정 표시가 뒤집히면 안 된다.
		Widget->Button->OnClicked.Broadcast();
		TestEqual(TEXT("Rejected click does not optimistically change the label"), Widget->ONOFF->GetText().ToString(),
			FString(bInitiallyOn ? TEXT("OFF") : TEXT("ON")));
		Snapshot.bIsOperator = false;
		Controller->OnDeveloperConvenienceStateChanged.Broadcast(Snapshot);
		TestFalse(TEXT("Waiting peer only displays the shared state"), Widget->Button->GetIsEnabled());
		Snapshot.bIsOperator = true;
		Snapshot.bTransitioning = true;
		Controller->OnDeveloperConvenienceStateChanged.Broadcast(Snapshot);
		TestFalse(TEXT("Transition locks option changes"), Widget->Button->GetIsEnabled());
		Widget->NativeDestruct();
		Snapshot.Options = FDeveloperConvenienceOptions();
		Controller->OnDeveloperConvenienceStateChanged.Broadcast(Snapshot);
		TestEqual(TEXT("Removed widget no longer receives state notifications"), Widget->ONOFF->GetText().ToString(),
			FString(bInitiallyOn ? TEXT("OFF") : TEXT("ON")));
		Widget->NativeConstruct();
		TestEqual(TEXT("Reconstructed widget reads current controller state"), Widget->ONOFF->GetText().ToString(),
			FString(bInitiallyOn ? TEXT("ON") : TEXT("OFF")));
	}
	return true;
}

#endif
