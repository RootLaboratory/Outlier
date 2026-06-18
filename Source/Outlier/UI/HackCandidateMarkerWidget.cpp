#include "UI/HackCandidateMarkerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Drone/Partner/PartnerCharacter.h"

void UHackCandidateMarkerWidget::InitializeMarker(AActor* InTargetActor, UHackableComponent* InHackableComponent)
{
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;
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
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(PC->GetPawn());
	if (!PartnerCharacter || !TargetActor)
	{
		return;
	}

	PartnerCharacter->RequestHackTarget(TargetActor);
}
