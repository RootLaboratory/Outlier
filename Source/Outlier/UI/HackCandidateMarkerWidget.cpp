#include "UI/HackCandidateMarkerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Drone/Partner/PartnerHackComponent.h"

void UHackCandidateMarkerWidget::InitializeMarker(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent)
{
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;
	HackComponent = InHackComponent;
}

void UHackCandidateMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Visible);

	if (!SelectButton && WidgetTree)
	{
		SelectButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SelectButton"));
		WidgetTree->RootWidget = SelectButton;
	}

	if (SelectButton)
	{
		SelectButton->SetVisibility(ESlateVisibility::Visible);
		SelectButton->OnClicked.RemoveDynamic(this, &UHackCandidateMarkerWidget::HandleClicked);
		SelectButton->OnClicked.AddDynamic(this, &UHackCandidateMarkerWidget::HandleClicked);
	}
}

void UHackCandidateMarkerWidget::HandleClicked()
{
	if (!HackComponent || !TargetActor)
	{
		return;
	}

	HackComponent->TrySelectHackTarget(TargetActor);
}
