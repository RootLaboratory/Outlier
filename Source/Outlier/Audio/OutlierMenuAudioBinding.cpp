#include "Audio/OutlierMenuAudioBinding.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "UI/UpgradeNodeWidget.h"
void UOutlierMenuAudioBinding::Bind(UButton* InButton, UOutlierAudioSubsystem* InAudio)
{
	Button = InButton;
	bSuppressClick = InButton->GetTypedOuter<UUpgradeNodeWidget>() != nullptr;
	Audio = InAudio;
	InButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Clicked);
	InButton->OnHovered.AddUniqueDynamic(this, &ThisClass::Hovered);
}
void UOutlierMenuAudioBinding::Clicked()
{
	if (!bSuppressClick && Audio.IsValid() && Button.IsValid() && Audio->LastMenuAudioFrame != GFrameCounter)
		OutlierAudioFeedback::Local(Button.Get(), TEXT("Audio.Context.UI.Menu.Click"));
}
void UOutlierMenuAudioBinding::Hovered()
{
	// UButton.OnHovered is pointer hover, not focus/navigation selection.
	if (Button.IsValid()) OutlierAudioFeedback::Local(Button.Get(), TEXT("Audio.Context.UI.Menu.Hover"));
}
void UOutlierAudioSubsystem::BindMenuButtons(UUserWidget* Widget)
{
	if (!Widget || !Widget->WidgetTree) return;
	MenuAudioBindings.RemoveAll([](const auto& Binding) { return !Binding || !Binding->IsAlive(); });
	TArray<UWidget*> Widgets;
	Widget->WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Child : Widgets)
	{
		if (UUserWidget* Nested = Cast<UUserWidget>(Child)) BindMenuButtons(Nested);
		UButton* Button = Cast<UButton>(Child);
		if (!Button) continue;
		// Upgrade buttons own Active/Fail feedback, never generic Click.

		if (MenuAudioBindings.ContainsByPredicate([Button](const auto& Binding) { return Binding->Matches(Button); })) continue;
		UOutlierMenuAudioBinding* Binding = NewObject<UOutlierMenuAudioBinding>(this);
		Binding->Bind(Button, this);
		MenuAudioBindings.Add(Binding);
	}
}
