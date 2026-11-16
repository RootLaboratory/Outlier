#include "UI/DeveloperConvenienceWidget.h"

#include "FirstPerson/FirstPersonPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

void UDeveloperConvenienceWidget::InitializeDeveloperConvenience(const FDeveloperConvenienceSnapshot& Snapshot)
{
	const bool bNewSession = !bContextActive || Context.SessionId != Snapshot.SessionId;
	Context = Snapshot;
	bContextActive = true;
	SetIsFocusable(true);
	if (bNewSession)
	{
		Page = EDeveloperConveniencePage::MainMenu;
		OnDeveloperConveniencePageChanged.Broadcast(Page);
	}
}

bool UDeveloperConvenienceWidget::SetDeveloperConveniencePage(EDeveloperConveniencePage NewPage)
{
	if (!bContextActive || !Context.bIsOperator || Context.bTransitioning
		|| (NewPage != EDeveloperConveniencePage::MainMenu && NewPage != EDeveloperConveniencePage::LevelSelection))
	{
		return false;
	}
	if (Page != NewPage)
	{
		Page = NewPage;
		OnDeveloperConveniencePageChanged.Broadcast(Page);
	}
	return true;
}

FReply UDeveloperConvenienceWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (auto* Controller = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		if (Controller->HandleDeveloperConvenienceKey(Event.GetKey(), Event.IsShiftDown(), Event.IsRepeat()))
		{
			return FReply::Handled();
		}
		if (auto* Layers = Controller->GetLocalPlayer()
			? Controller->GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
			Layers && Layers->RouteWidgetPreviewKeyDown(this, Event))
		{
			return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

bool UDeveloperConvenienceWidget::HandleUILayerEscape_Implementation()
{
	if (bContextActive && Context.bIsOperator)
	{
		if (Page == EDeveloperConveniencePage::LevelSelection)
		{
			SetDeveloperConveniencePage(EDeveloperConveniencePage::MainMenu);
			return true;
		}
		if (auto* Controller = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
		{
			Controller->RequestDeveloperConvenienceSession(false, Context);
		}
	}
	// 대기 화면도 입력을 소비해 기존 설정창/기본 Pop 동작으로 빠지지 않는다.
	return true;
}

void UDeveloperConvenienceWidget::NativeDestruct()
{
	if (bContextActive)
	{
		bContextActive = false;
		if (auto* Controller = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
		{
			Controller->ServerDeveloperConvenienceUIUnavailable(Context.SessionId);
		}
	}
	Super::NativeDestruct();
}
