#include "UI/UILayerKeyHintWidget.h"

#include "Components/TextBlock.h"
#include "EnhancedActionKeyMapping.h"
#include "FrontendPlayerController.h"
#include "InputMappingContext.h"

void UUILayerKeyHintWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (MissingKeyText.IsEmpty())
	{
		MissingKeyText = FText::FromString(TEXT("-"));
	}

	RefreshKeyTexts();
}

void UUILayerKeyHintWidget::RefreshKeyTexts()
{
	const AFrontendPlayerController* FrontendPlayerController =
		Cast<AFrontendPlayerController>(GetOwningPlayer());

	if (ConfirmedKeyText)
	{
		ConfirmedKeyText->SetText(
			ConfirmedHintTextOverride.IsEmpty() && FrontendPlayerController
				? ResolveActionKeyText(FrontendPlayerController->GetWidgetConfirmedAction())
				: (ConfirmedHintTextOverride.IsEmpty() ? MissingKeyText : ConfirmedHintTextOverride));
	}

	if (EscapeKeyText)
	{
		EscapeKeyText->SetText(
			EscapeHintTextOverride.IsEmpty() && FrontendPlayerController
				? ResolveActionKeyText(FrontendPlayerController->GetWidgetEscapeAction())
				: (EscapeHintTextOverride.IsEmpty() ? MissingKeyText : EscapeHintTextOverride));
	}
}

void UUILayerKeyHintWidget::SetConfirmedHintText(const FText& InHintText)
{
	ConfirmedHintTextOverride = InHintText;
	RefreshKeyTexts();
}

void UUILayerKeyHintWidget::SetEscapeHintText(const FText& InHintText)
{
	EscapeHintTextOverride = InHintText;
	RefreshKeyTexts();
}

void UUILayerKeyHintWidget::ClearHintTextOverrides()
{
	ConfirmedHintTextOverride = FText::GetEmpty();
	EscapeHintTextOverride = FText::GetEmpty();
	RefreshKeyTexts();
}

FText UUILayerKeyHintWidget::ResolveActionKeyText(const UInputAction* InputAction) const
{
	const AFrontendPlayerController* FrontendPlayerController =
		Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (!FrontendPlayerController || !InputAction)
	{
		return MissingKeyText;
	}

	for (const UInputMappingContext* MappingContext :
		FrontendPlayerController->GetDefaultMappingContexts())
	{
		if (!MappingContext)
		{
			continue;
		}

		for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
		{
			if (Mapping.Action == InputAction && Mapping.Key.IsValid())
			{
				return Mapping.Key.GetDisplayName(false);
			}
		}
	}

	return MissingKeyText;
}
