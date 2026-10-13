#include "UI/InputCombinationWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "InputAction.h"
#include "Outlier.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "UI/InputActionKeyDisplayWidget.h"

void UInputCombinationWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureInputPanel();
}

TSharedRef<SWidget> UInputCombinationWidget::RebuildWidget()
{
	EnsureInputPanel();
	return Super::RebuildWidget();
}

void UInputCombinationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bIsConstructed = true;
	BindInputEvents();
	RefreshExpression();
}

void UInputCombinationWidget::NativeDestruct()
{
	bIsConstructed = false;
	UnbindInputEvents();
	Super::NativeDestruct();
}

void UInputCombinationWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshExpression();
}

bool UInputCombinationWidget::EnsureInputPanel()
{
	if (!WidgetTree)
	{
		return false;
	}
	if (!InputItems) InputItems = WidgetTree->FindWidget<UHorizontalBox>(TEXT("InputItems"));
	if (!InputItems && !WidgetTree->RootWidget)
	{
		InputItems = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InputItems"));
	}
	if (InputItems && (!WidgetTree->RootWidget || WidgetTree->RootWidget == InputItems))
	{
		// Keep the whole row centered even when its desired width exceeds the available space.
		UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("InputContentScale"));
		Scale->SetStretch(EStretch::ScaleToFit);
		Scale->SetStretchDirection(EStretchDirection::DownOnly);
		UScaleBoxSlot* RowSlot = CastChecked<UScaleBoxSlot>(Scale->AddChild(InputItems));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
		RowSlot->SetVerticalAlignment(VAlign_Center);
		USizeBox* Host = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("InputContentHost"));
		USizeBoxSlot* HostSlot = CastChecked<USizeBoxSlot>(Host->AddChild(Scale));
		HostSlot->SetHorizontalAlignment(HAlign_Fill);
		HostSlot->SetVerticalAlignment(VAlign_Fill);
		WidgetTree->RootWidget = Host;
	}
	return InputItems != nullptr;
}

void UInputCombinationWidget::SetExpression(const TArray<FInputDisplayTerm>& NewExpression)
{
	Expression = NewExpression;
	if (bIsConstructed)
	{
		BindInputEvents();
		RefreshExpression();
	}
}

void UInputCombinationWidget::SetDisplayDefinition(const FInputDisplayDefinition& Definition)
{
	KeySpacing = Definition.KeySpacing;
	GroupSpacing = Definition.GroupSpacing;
	SymbolPadding = Definition.SymbolPadding;
	SymbolSize = Definition.SymbolSize;
	SetExpression(Definition.Expression);
}

void UInputCombinationWidget::SetKeyFrameTexture(UTexture2D* Texture)
{
	DefaultKeyFrameTexture = Texture;
	if (bIsConstructed) RefreshExpression();
}

void UInputCombinationWidget::SetKeyTextColor(FLinearColor NewColor)
{
	KeyTextColor = NewColor;
	if (bIsConstructed) RefreshExpression();
}

void UInputCombinationWidget::ClearExpression()
{
	Expression.Reset();
	UnbindInputEvents();
	if (InputItems)
	{
		InputItems->ClearChildren();
	}
}

bool UInputCombinationWidget::RefreshExpression()
{
	if (!EnsureInputPanel())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[InputCombination] '%s' needs a HorizontalBox named InputItems."), *GetName());
		return false;
	}
	InputItems->ClearChildren();
	if (Expression.IsEmpty())
	{
		return true;
	}
	if (!KeyDisplayWidgetClass || KeyDisplayWidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[InputCombination] '%s' needs a concrete KeyDisplayWidgetClass."), *GetName());
		return false;
	}

	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	bool bAllResolved = true;
	bool bHasGroup = false;
	for (const FInputDisplayTerm& Term : Expression)
	{
		if (!IsValid(Term.InputAction))
		{
			bAllResolved = false;
			continue;
		}

		TArray<FKey> Keys;
		if (InputSubsystem)
		{
			for (const FKey& Key : InputSubsystem->QueryKeysMappedToAction(Term.InputAction))
			{
				if (Key.IsValid())
				{
					Keys.AddUnique(Key);
					if (Term.KeySelection == EInputDisplayKeySelection::FirstMapped)
					{
						break;
					}
				}
			}
		}
		if (Keys.IsEmpty())
		{
			// Keep an unresolved operand visible as the leaf widget's missing-key marker.
			// A later mapping rebuild replaces it with the complete group.
			bAllResolved = false;
			Keys.Add(FKey());
		}

		UHorizontalBox* Group = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			UInputActionKeyDisplayWidget* KeyWidget = CreateWidget<UInputActionKeyDisplayWidget>(this, KeyDisplayWidgetClass);
			if (!KeyWidget)
			{
				bAllResolved = false;
				continue;
			}
			if (DefaultKeyFrameTexture) KeyWidget->SetBorderImage(DefaultKeyFrameTexture);
			KeyWidget->SetKeyTextColor(KeyTextColor);
			KeyWidget->SetDisplayHeight(KeyDisplayHeight);
			KeyWidget->SetDisplayedKey(Keys[Index]);
			UHorizontalBoxSlot* KeySlot = Group->AddChildToHorizontalBox(KeyWidget);
			KeySlot->SetVerticalAlignment(VAlign_Center);
			KeySlot->SetPadding(FMargin(Group->GetChildrenCount() > 1 ? FMath::Max(0.0f, KeySpacing) : 0.0f, 0.0f, 0.0f, 0.0f));
		}
		if (Group->GetChildrenCount() == 0)
		{
			continue;
		}

		UWidget* Separator = bHasGroup ? CreateSeparator(Term) : nullptr;
		if (Separator)
		{
			UHorizontalBoxSlot* SymbolSlot = InputItems->AddChildToHorizontalBox(Separator);
			SymbolSlot->SetVerticalAlignment(VAlign_Center);
			SymbolSlot->SetPadding(SymbolPadding);
		}
		FMargin GroupPadding = Term.Padding;
		if (bHasGroup && !Separator)
		{
			GroupPadding.Left += FMath::Max(0.0f, GroupSpacing);
		}
		UHorizontalBoxSlot* GroupSlot = InputItems->AddChildToHorizontalBox(Group);
		GroupSlot->SetVerticalAlignment(VAlign_Center);
		GroupSlot->SetPadding(GroupPadding);
		bHasGroup = true;
	}
	return bAllResolved;
}

UWidget* UInputCombinationWidget::CreateSeparator(const FInputDisplayTerm& Term)
{
	if (Term.SeparatorBefore == EInputDisplaySeparator::None)
	{
		return nullptr;
	}
	UTexture2D* Texture = Term.SeparatorBefore == EInputDisplaySeparator::Plus ? PlusTexture.Get() : Term.SymbolTexture.Get();
	if (IsValid(Texture))
	{
		UImage* Image = WidgetTree->ConstructWidget<UImage>();
		Image->SetBrushFromTexture(Texture, true);
		UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>();
		Scale->SetStretch(EStretch::ScaleToFit);
		Scale->SetStretchDirection(EStretchDirection::Both);
		UScaleBoxSlot* ImageSlot = CastChecked<UScaleBoxSlot>(Scale->AddChild(Image));
		ImageSlot->SetHorizontalAlignment(HAlign_Center);
		ImageSlot->SetVerticalAlignment(VAlign_Center);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(FMath::Max(1.0, SymbolSize.X));
		Size->SetHeightOverride(FMath::Max(1.0, SymbolSize.Y));
		Size->AddChild(Scale);
		return Size;
	}
	const FText Text = Term.SeparatorBefore == EInputDisplaySeparator::Plus ? FText::FromString(TEXT("+")) : Term.SymbolText;
	if (Text.IsEmpty())
	{
		return nullptr;
	}
	UTextBlock* Symbol = WidgetTree->ConstructWidget<UTextBlock>();
	Symbol->SetText(Text);
	return Symbol;
}

void UInputCombinationWidget::BindInputEvents()
{
	UnbindInputEvents();
	if (Expression.IsEmpty())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (!LocalPlayer)
	{
		return;
	}
	BoundInputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (BoundInputSubsystem)
	{
		// Stay subscribed after success: context changes can add/remove keys in an AllMapped group.
		BoundInputSubsystem->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &UInputCombinationWidget::HandleControlMappingsRebuilt);
	}
	BoundSettingsSubsystem = LocalPlayer->GetSubsystem<ULocalPlayerSettingsSubsystem>();
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputActionKeyChanged.AddUniqueDynamic(this, &UInputCombinationWidget::HandleInputActionKeyChanged);
	}
}

void UInputCombinationWidget::UnbindInputEvents()
{
	if (BoundInputSubsystem)
	{
		BoundInputSubsystem->ControlMappingsRebuiltDelegate.RemoveAll(this);
		BoundInputSubsystem = nullptr;
	}
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputActionKeyChanged.RemoveAll(this);
		BoundSettingsSubsystem = nullptr;
	}
}

void UInputCombinationWidget::HandleInputActionKeyChanged(UInputAction* ChangedInputAction, FKey NewKey)
{
	(void)NewKey;
	if (Expression.ContainsByPredicate([ChangedInputAction](const FInputDisplayTerm& Term) { return Term.InputAction == ChangedInputAction; }))
	{
		RefreshExpression();
	}
}

void UInputCombinationWidget::HandleControlMappingsRebuilt()
{
	RefreshExpression();
}
