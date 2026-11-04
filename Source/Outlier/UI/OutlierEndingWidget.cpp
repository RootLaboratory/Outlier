#include "UI/OutlierEndingWidget.h"

#include "Components/Button.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

UOutlierEndingWidget::UOutlierEndingWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	EndingAudioTag = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Bgm.EndingCredit"));
}

void UOutlierEndingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (ensureMsgf(EndingButton, TEXT("EndingButton must be bound on %s"), *GetName()))
	{
		EndingButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleEndingClicked);
		EndingButton->OnHovered.AddUniqueDynamic(this, &ThisClass::HandleEndingHovered);
		EndingButton->OnUnhovered.AddUniqueDynamic(this, &ThisClass::HandleEndingUnhovered);
		OriginalEndingStyle = EndingButton->GetStyle();
	}
}

void UOutlierEndingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetEndingSelected(true);
}

void UOutlierEndingWidget::SetEndingSelected(bool bSelected)
{
	if (!EndingButton) return;
	FButtonStyle Style = OriginalEndingStyle;
	if (bSelected) Style.SetNormal(Style.Hovered);
	EndingButton->SetStyle(Style);
}

void UOutlierEndingWidget::HandleEndingHovered()
{
	SetEndingSelected(true);
	if (GetOwningPlayer()) SetUserFocus(GetOwningPlayer());
}

void UOutlierEndingWidget::HandleEndingUnhovered()
{
	SetEndingSelected(false);
}

bool UOutlierEndingWidget::SelectEndingFromKeyboard()
{
	SetEndingSelected(true);
	if (GetOwningPlayer()) SetUserFocus(GetOwningPlayer());
	OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Hover"));
	return true;
}

bool UOutlierEndingWidget::HandleUILayerUp_Implementation() { return SelectEndingFromKeyboard(); }
bool UOutlierEndingWidget::HandleUILayerDown_Implementation() { return SelectEndingFromKeyboard(); }
bool UOutlierEndingWidget::HandleUILayerLeft_Implementation() { return SelectEndingFromKeyboard(); }
bool UOutlierEndingWidget::HandleUILayerRight_Implementation() { return SelectEndingFromKeyboard(); }

FReply UOutlierEndingWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* Layers = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
	if (Layers && Layers->RouteWidgetPreviewKeyDown(this, InKeyEvent))
	{
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

bool UOutlierEndingWidget::HandleUILayerConfirmed_Implementation()
{
	HandleEndingClicked();
	return true;
}

void UOutlierEndingWidget::HandleEndingClicked()
{
	if (AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		if (Controller->HasRequestedExplicitLeave() || !EndingButton || !EndingButton->GetIsEnabled()) return;
		OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Click"));
		Controller->RequestLeaveGame(false);
	}
}
