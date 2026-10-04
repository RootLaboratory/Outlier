#include "UI/UpgradeDescWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MediaTexture.h"
#include "PopupRetainerBox.h"

void UUpgradeDescWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PopupRetainer)
	{
		PopupRetainer->OnClosed.AddUniqueDynamic(this, &UUpgradeDescWidget::HandlePopupClosed);
		PopupRetainer->ResetPopup();
	}

	if (Button && !bDefaultDisabledButtonBrushCached)
	{
		DefaultDisabledButtonBrush = Button->GetStyle().Disabled;
		bDefaultDisabledButtonBrushCached = true;
	}
	if (Button)
	{
		Button->OnClicked.AddUniqueDynamic(this, &UUpgradeDescWidget::HandlePurchaseClicked);
	}
	if (Background && !bDefaultBackgroundBrushCached)
	{
		DefaultBackgroundBrush = Background->GetBrush();
		bDefaultBackgroundBrushCached = true;
	}
	if (DefaultPopupDesignSize.X <= 0.0f || DefaultPopupDesignSize.Y <= 0.0f)
	{
		DefaultPopupDesignSize = GetPopupDesignSize();
	}
	if (PopupRetainer && Background)
	{
		if (USizeBox* RootSizeBox = Cast<USizeBox>(PopupRetainer->GetContent()))
		{
			if (DefaultPopupDesignSize.X > 0.0f && DefaultPopupDesignSize.Y > 0.0f)
			{
				RootSizeBox->SetMinDesiredWidth(0.0f);
				RootSizeBox->SetMinDesiredHeight(0.0f);
				RootSizeBox->SetWidthOverride(DefaultPopupDesignSize.X);
				RootSizeBox->SetHeightOverride(DefaultPopupDesignSize.Y);
			}
		}
	}
	InitializeMediaImage();

	RefreshTextBlocks();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UUpgradeDescWidget::HandlePurchaseClicked()
{
	if (CurrentNodeState == EOutlierUpgradeNodeState::Unlocked && bCanAfford)
	{
		OnPurchaseRequested.Broadcast();
	}
}

void UUpgradeDescWidget::InjectNodeData(
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	UpdateNodeData(NAME_None, InNodeData, InNodeState, InCurrentNodeCount, bInCanAfford);
}

void UUpgradeDescWidget::UpdateNodeData(
	FName InNodeRowName,
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	CurrentNodeRowName = InNodeRowName;
	CurrentNodeData = InNodeData;
	CurrentNodeState = InNodeState;
	CurrentNodeCount = InCurrentNodeCount;
	bCanAfford = bInCanAfford;
	bClearWhenClosed = false;

	RefreshTextBlocks();
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();

	if (PopupRetainer)
	{
		PopupRetainer->RequestRender();
	}
}

void UUpgradeDescWidget::ShowNodeData(
	FName InNodeRowName,
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	UpdateNodeData(InNodeRowName, InNodeData, InNodeState, InCurrentNodeCount, bInCanAfford);
	SetVisibility(ESlateVisibility::Visible);
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();
	PlayPopUp(true);
}

void UUpgradeDescWidget::HideNodeData(FName InNodeRowName)
{
	if (!InNodeRowName.IsNone() && !CurrentNodeRowName.IsNone() && InNodeRowName != CurrentNodeRowName)
	{
		return;
	}

	bClearWhenClosed = true;
	PlayPopUp(false);
}

FVector2D UUpgradeDescWidget::GetPopupDesignSize() const
{
	if (Background)
	{
		if (const UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(Background->Slot))
		{
			const FVector2D Size = BackgroundSlot->GetSize();
			if (Size.X > 0.0f && Size.Y > 0.0f)
			{
				return Size;
			}
		}
		return Background->GetBrush().ImageSize;
	}
	return FVector2D::ZeroVector;
}

void UUpgradeDescWidget::ClearNodeData()
{
	SetVisibility(ESlateVisibility::Collapsed);
	CurrentNodeRowName = NAME_None;
	CurrentNodeData = FOutlierUpgradeNodeRow();
	CurrentNodeState = EOutlierUpgradeNodeState::Locked;
	CurrentNodeCount = 0;
	bCanAfford = false;
	bClearWhenClosed = false;

	if (PopupRetainer)
	{
		PopupRetainer->ResetPopup();
	}
}

void UUpgradeDescWidget::PlayPopUp(bool bOpen)
{
	if (bOpen)
	{
		SetVisibility(ESlateVisibility::Visible);
	}

	if (PopupRetainer)
	{
		bOpen ? PopupRetainer->PlayOpen() : PopupRetainer->PlayClose();
		return;
	}

	if (!bOpen)
	{
		HandlePopupClosed();
	}
}

void UUpgradeDescWidget::HandlePopupClosed()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (bClearWhenClosed)
	{
		CurrentNodeRowName = NAME_None;
		CurrentNodeData = FOutlierUpgradeNodeRow();
		CurrentNodeState = EOutlierUpgradeNodeState::Locked;
		CurrentNodeCount = 0;
		bCanAfford = false;
		bClearWhenClosed = false;
	}
}

void UUpgradeDescWidget::RefreshTextBlocks()
{
	if (Name)
	{
		Name->SetText(CurrentNodeData.DisplayName);
	}

	if (Desc)
	{
		Desc->SetText(CurrentNodeData.Desc);
	}

	if (Cost)
	{
		Cost->SetText(BuildCostNeedText());
		Cost->SetVisibility(CurrentNodeState == EOutlierUpgradeNodeState::Activated
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	RefreshCostTextStyle();
	RefreshButtonStyle();
	RefreshBackground();
}

void UUpgradeDescWidget::InitializeMediaImage()
{
	if (!MediaImage || !MediaMaterial)
	{
		return;
	}

	MediaImage->SetBrushFromMaterial(MediaMaterial);
	MediaImageMaterial = MediaImage->GetDynamicMaterial();
	if (MediaImageMaterial && MediaTexture && !MediaTextureParameterName.IsNone())
	{
		MediaImageMaterial->SetTextureParameterValue(MediaTextureParameterName, MediaTexture);
	}
}

void UUpgradeDescWidget::RefreshBackground()
{
	const bool bPurchased = CurrentNodeState == EOutlierUpgradeNodeState::Activated;
	if (Background)
	{
		if (DefaultPopupDesignSize.X > 0.0f && DefaultPopupDesignSize.Y > 0.0f)
		{
			FVector2D PopupSize = DefaultPopupDesignSize;
			if (DefaultBackgroundTexture && DefaultBackgroundTexture->GetSizeX() > 0)
			{
				PopupSize.Y = PopupSize.X * DefaultBackgroundTexture->GetSizeY() / DefaultBackgroundTexture->GetSizeX();
			}
			if (bPurchased)
			{
				if (PurchasedBackgroundTexture && PurchasedBackgroundTexture->GetSizeX() > 0)
				{
					const float PurchasedHeight = PopupSize.X * PurchasedBackgroundTexture->GetSizeY() / PurchasedBackgroundTexture->GetSizeX();
					PopupSize.Y = FMath::Clamp(PurchasedHeight, 1.0f, PopupSize.Y);
				}
			}

			if (UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(Background->Slot))
			{
				BackgroundSlot->SetSize(PopupSize);
			}
			if (PopupRetainer)
			{
				if (USizeBox* RootSizeBox = Cast<USizeBox>(PopupRetainer->GetContent()))
				{
					RootSizeBox->SetWidthOverride(PopupSize.X);
					RootSizeBox->SetHeightOverride(PopupSize.Y);
				}
			}
		}

		UTexture2D* Texture = bPurchased ? PurchasedBackgroundTexture.Get() : DefaultBackgroundTexture.Get();
		FSlateBrush Brush = bDefaultBackgroundBrushCached ? DefaultBackgroundBrush : Background->GetBrush();
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
		}
		Background->SetBrush(Brush);
	}

	if (MediaImage)
	{
		MediaImage->SetVisibility(bPurchased ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UUpgradeDescWidget::RefreshCostTextStyle()
{
	const FSlateColor& CostColor = CurrentNodeState == EOutlierUpgradeNodeState::Activated || bCanAfford
		? DefaultCostTextColor
		: InsufficientCostTextColor;

	if (Cost)
	{
		Cost->SetColorAndOpacity(CostColor);
	}
}

void UUpgradeDescWidget::RefreshButtonStyle()
{
	if (!Button)
	{
		return;
	}

	const bool bPurchased = CurrentNodeState == EOutlierUpgradeNodeState::Activated;
	const bool bPurchasable = CurrentNodeState == EOutlierUpgradeNodeState::Unlocked && bCanAfford;
	UTexture2D* DisabledTexture = nullptr;
	if (bPurchased)
	{
		DisabledTexture = PurchasedDisabledTexture.Get();
	}
	else if (CurrentNodeState == EOutlierUpgradeNodeState::Locked)
	{
		DisabledTexture = PrerequisiteDisabledTexture.Get();
	}
	else if (!bCanAfford)
	{
		DisabledTexture = InsufficientDisabledTexture.Get();
	}

	FButtonStyle Style = Button->GetStyle();
	Style.Disabled = DefaultDisabledButtonBrush;
	if (DisabledTexture)
	{
		Style.Disabled.SetResourceObject(DisabledTexture);
		Style.Disabled.DrawAs = ESlateBrushDrawType::Image;
		Style.Disabled.ImageType = ESlateBrushImageType::FullColor;
		Style.Disabled.ImageSize = Style.Normal.ImageSize.X > 0.0f && Style.Normal.ImageSize.Y > 0.0f
			? Style.Normal.ImageSize
			: FVector2D(DisabledTexture->GetSizeX(), DisabledTexture->GetSizeY());
		Style.Disabled.TintColor = FSlateColor(FLinearColor::White);
	}
	Button->SetStyle(Style);
	Button->SetIsEnabled(bPurchasable);
}

FText UUpgradeDescWidget::BuildCostNeedText() const
{
	FFormatOrderedArguments Arguments;
	Arguments.Add(FText::AsNumber(CurrentNodeData.Cost));
	Arguments.Add(FText::AsNumber(CurrentNodeCount));
	return FText::Format(CostNeedTextFormat, Arguments);
}
