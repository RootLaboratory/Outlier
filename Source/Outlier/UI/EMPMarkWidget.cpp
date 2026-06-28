#include "UI/EMPMarkWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Drone/Partner/PartnerEMPComponent.h"

void UEMPMarkWidget::InitializeMark(AActor* InTargetActor, UEMPableComponent* InEMPableComponent, UPartnerEMPComponent* InEMPComponent)
{
	TargetActor = InTargetActor;
	EMPableComponent = InEMPableComponent;
	EMPComponent = InEMPComponent;
}

void UEMPMarkWidget::NativeConstruct()
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
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SelectButton->IsFocusable = false;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		SelectButton->OnClicked.RemoveDynamic(this, &UEMPMarkWidget::HandleClicked);
		SelectButton->OnClicked.AddDynamic(this, &UEMPMarkWidget::HandleClicked);
	}
}

void UEMPMarkWidget::HandleClicked()
{
	if (!EMPComponent || !TargetActor)
	{
		return;
	}

	EMPComponent->TryMarkEMPTarget(TargetActor);
}
