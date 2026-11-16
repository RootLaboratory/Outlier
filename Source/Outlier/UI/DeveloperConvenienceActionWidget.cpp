#include "UI/DeveloperConvenienceActionWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "DeveloperConvenienceActionWidget"

void UDeveloperConvenienceActionWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshLabels();
}

void UDeveloperConvenienceActionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshLabels();
	if (Button)
	{
		Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClicked);
	}
}

void UDeveloperConvenienceActionWidget::NativeDestruct()
{
	if (Button)
	{
		Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleClicked);
	}
	Super::NativeDestruct();
}

void UDeveloperConvenienceActionWidget::RefreshLabels()
{
	FText DefaultTitle;
	FText DefaultDescription;
	switch (Action)
	{
	case EDeveloperConvenienceAction::TeamRecovery:
		DefaultTitle = LOCTEXT("Recovery", "팀 즉시 회복");
		DefaultDescription = LOCTEXT("RecoveryDescription", "팀 체력·기본 실드 회복 / Partner Reboot 즉시 완료");
		break;
	case EDeveloperConvenienceAction::LevelSelection:
		DefaultTitle = LOCTEXT("Level", "레벨 선택");
		break;
	case EDeveloperConvenienceAction::ReturnToTitle:
		DefaultTitle = LOCTEXT("Title", "타이틀로 가기");
		break;
	}
	if (Title)
	{
		Title->SetText(TitleText.IsEmpty() ? DefaultTitle : TitleText);
	}
	if (Desc)
	{
		const FText Description = DescriptionText.IsEmpty() ? DefaultDescription : DescriptionText;
		Desc->SetText(Description);
		Desc->SetVisibility(Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UDeveloperConvenienceActionWidget::HandleClicked()
{
	if (GetIsEnabled())
	{
		OnActionClicked.Broadcast(Action);
	}
}

#undef LOCTEXT_NAMESPACE
