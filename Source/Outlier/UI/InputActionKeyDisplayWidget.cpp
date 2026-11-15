#include "UI/InputActionKeyDisplayWidget.h"

#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/FontCache.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "UI/InputKeyDisplayNameDataRow.h"
#include "UI/InputKeyTextureDataRow.h"
#include "UI/InputKeyFrameLayout.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"
#include "Outlier.h"

/** Paint before the original widget tree, so the frame remains behind the key label. */
class SInputKeyFrame : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputKeyFrame) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UInputActionKeyDisplayWidget>, KeyWidget)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		KeyWidget = Args._KeyWidget;
		ChildSlot[Args._Content.Widget];
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		UInputActionKeyDisplayWidget* Widget = KeyWidget.Get();
		if (Widget) Widget->RefreshKeyTextAlignmentForPaint(Geometry);
		const bool bPainted = Widget && Widget->PaintScaledFrame(Geometry, Elements, LayerId, Style, bParentEnabled);
		return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
			LayerId + (bPainted ? 1 : 0), Style, bParentEnabled);
	}

private:
	TWeakObjectPtr<UInputActionKeyDisplayWidget> KeyWidget;
};

TSharedRef<SWidget> UInputActionKeyDisplayWidget::RebuildWidget()
{
	return SNew(SInputKeyFrame).KeyWidget(this)[Super::RebuildWidget()];
}

bool UInputActionKeyDisplayWidget::PaintScaledFrame(const FGeometry& Geometry,
	FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!bDrawScaledFrame || !Border || !ScaledFrameBrush.GetResourceObject()) return false;
	const FVector2D OriginalSize = Geometry.GetLocalSize();
	if (OriginalSize.X <= 0.0 || OriginalSize.Y <= 0.0) return false;
	const FVector2D Origin = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector2D AxisX = FVector2D(Geometry.LocalToAbsolute(FVector2D(1, 0))) - Origin;
	const FVector2D AxisY = FVector2D(Geometry.LocalToAbsolute(FVector2D(0, 1))) - Origin;
	const FVector2D PixelScale(AxisX.Size(), AxisY.Size());
	if (PixelScale.X <= UE_DOUBLE_SMALL_NUMBER || PixelScale.Y <= UE_DOUBLE_SMALL_NUMBER) return false;
	FGeometry PaintGeometry = Geometry;
	// Snap only the painted background. Child layout, text measurement and hit testing
	// keep their original geometry. Accumulated render scale includes DPI and ScaleBox.
	const bool bAxisAligned = FMath::IsNearlyZero(AxisX.Y, 0.0001) && FMath::IsNearlyZero(AxisY.X, 0.0001);
	if (bAxisAligned)
	{
		const FVector2D End = Geometry.LocalToAbsolute(OriginalSize);
		const FVector2D SnappedStart(FMath::RoundToDouble(Origin.X), FMath::RoundToDouble(Origin.Y));
		const FVector2D SnappedEnd(FMath::RoundToDouble(End.X), FMath::RoundToDouble(End.Y));
		const FVector2D LocalStart = Geometry.AbsoluteToLocal(SnappedStart);
		const FVector2D LocalEnd = Geometry.AbsoluteToLocal(SnappedEnd);
		PaintGeometry = Geometry.MakeChild(LocalEnd - LocalStart, FSlateLayoutTransform(LocalStart));
	}
	const FVector2D Size = PaintGeometry.GetLocalSize();
	if (Size.X <= 0.0 || Size.Y <= 0.0) return false;
	const FVector2D Corner = FInputKeyFrameLayout::GetPixelAlignedCornerSize(Size, PixelScale);
	const double X[] = {0.0, Corner.X, Size.X - Corner.X, Size.X};
	const double Y[] = {0.0, Corner.Y, Size.Y - Corner.Y, Size.Y};
	const float UV[] = {0.0f, FInputKeyFrameLayout::SourceMargin, 1.0f - FInputKeyFrameLayout::SourceMargin, 1.0f};
	const FLinearColor Tint = ScaledFrameBrush.GetTint(Style) * Style.GetColorAndOpacityTint() * Border->GetBrushColor();
	const ESlateDrawEffect Effects = bParentEnabled && GetIsEnabled()
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Column = 0; Column < 3; ++Column)
		{
			const FVector2D TileSize(X[Column + 1] - X[Column], Y[Row + 1] - Y[Row]);
			if (TileSize.X <= 0.0 || TileSize.Y <= 0.0) continue;
			FSlateBrush Tile = ScaledFrameBrush;
			Tile.SetUVRegion(FBox2f(FVector2f(UV[Column], UV[Row]), FVector2f(UV[Column + 1], UV[Row + 1])));
			FSlateDrawElement::MakeBox(Elements, LayerId,
				PaintGeometry.ToPaintGeometry(TileSize, FSlateLayoutTransform(FVector2D(X[Column], Y[Row]))), &Tile, Effects, Tint);
		}
	}
	return true;
}

#if WITH_EDITOR
void UInputActionKeyDisplayWidget::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	const FName Name = Event.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UInputActionKeyDisplayWidget, FrameTexture) && FrameTexture)
	{
		NonFrameTexture = nullptr;
		BorderImage = nullptr;
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UInputActionKeyDisplayWidget, NonFrameTexture) && NonFrameTexture)
	{
		FrameTexture = nullptr;
		BorderImage = nullptr;
	}
	Super::PostEditChangeProperty(Event);
}
#endif

void UInputActionKeyDisplayWidget::SetDisplayedInputAction_Implementation(UInputAction* InputAction)
{
	SetWatchedInputAction(InputAction);
}

void UInputActionKeyDisplayWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshKeyTextColor();
	RefreshBorderBrush();
	RefreshKeyLayout();
}

void UInputActionKeyDisplayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bIsConstructed = true;
	if (KeyText)
	{
		KeyText->SetAutoWrapText(false);
		KeyText->SetWrapTextAt(0.f);
		KeyText->SetJustification(ETextJustify::Center);
	}
	RefreshKeyTextColor();

	if (WidgetTree)
	{
		UScaleBox* KeyScaleBox = WidgetTree->FindWidget<UScaleBox>(TEXT("ScaleBox"));
		if (!KeyScaleBox)
		{
			KeyScaleBox = WidgetTree->FindWidget<UScaleBox>(TEXT("ScaleBox_0"));
		}
		if (KeyScaleBox)
		{
			KeyScaleBox->SetStretch(EStretch::ScaleToFit);
			KeyScaleBox->SetStretchDirection(EStretchDirection::DownOnly);
		}
	}

	if (MissingKeyText.IsEmpty())
	{
		MissingKeyText = FText::FromString(TEXT("-"));
	}

	RefreshBorderBrush();
	if (!bUseExplicitKey)
	{
		BindSettingsSubsystem();
	}
	EnsureWatchedKeyIsResolved();
}

void UInputActionKeyDisplayWidget::NativeDestruct()
{
	bIsConstructed = false;
	UnbindControlMappingsRebuiltDelegate();
	UnbindSettingsSubsystem();
	SetIsMissingKey(false);

	Super::NativeDestruct();
}

void UInputActionKeyDisplayWidget::SetWatchedInputAction(UInputAction* NewInputAction)
{
	bUseExplicitKey = false;
	ExplicitKey = FKey();
	WatchedInputAction = NewInputAction;

	// NativeConstruct 이전 호출이면 값만 저장한다.
	// 실제 delegate 연결은 NativeConstruct에서 담당한다.
	if (bIsConstructed)
	{
		BindSettingsSubsystem();
		EnsureWatchedKeyIsResolved();
	}
}

void UInputActionKeyDisplayWidget::SetShowKeyTextWithImage(bool bNewShowKeyTextWithImage)
{
	bShowKeyTextWithImage = bNewShowKeyTextWithImage;
	if (IsDesignTime() && bUseExplicitKey)
	{
		RefreshDisplayedKey();
	}
	else
	{
		EnsureWatchedKeyIsResolved();
	}
}

void UInputActionKeyDisplayWidget::SetDisplayedKey(FKey NewKey)
{
	bUseExplicitKey = true;
	ExplicitKey = NewKey;
	WatchedInputAction = nullptr;
	TextOverride = FText::GetEmpty();
	UnbindSettingsSubsystem();
	UnbindControlMappingsRebuiltDelegate();
	if (IsDesignTime())
	{
		// Designer widgets do not run NativeConstruct; explicit keys need no player context.
		RefreshDisplayedKey();
	}
	else
	{
		EnsureWatchedKeyIsResolved();
	}
}

void UInputActionKeyDisplayWidget::SetTextOverride(const FText& InOverrideText)
{
	TextOverride = InOverrideText;
	EnsureWatchedKeyIsResolved();
}

void UInputActionKeyDisplayWidget::ClearTextOverride()
{
	TextOverride = FText::GetEmpty();
	EnsureWatchedKeyIsResolved();
}

bool UInputActionKeyDisplayWidget::RefreshDisplayedKey()
{
	if (!KeyText)
	{
		return false;
	}

	if (!TextOverride.IsEmpty())
	{
		ShowKeyText(TextOverride);
		return true;
	}

	if (bUseExplicitKey)
	{
		return ShowResolvedKey(ExplicitKey);
	}

	if (!WatchedInputAction)
	{
		ShowKeyText(MissingKeyText);
		return false;
	}

	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	if (!InputSubsystem)
	{
		ShowKeyText(MissingKeyText);
		return false;
	}

	// QueryKeysMappedToAction은 이 플레이어에게 현재 실제로 적용된(리바인드
	// 반영된) 키를 돌려준다. FrontendPlayerController든
	// FirstPersonPlayerController든 상관없이 동일하게 동작하므로, 타이틀/
	// 로비 KeyHint와 인게임 InteractKeyWidget 양쪽에서 그대로 재사용된다.
	const TArray<FKey> MappedKeys = InputSubsystem->QueryKeysMappedToAction(WatchedInputAction);
	if (MappedKeys.IsValidIndex(0) && MappedKeys[0].IsValid())
	{
		return ShowResolvedKey(MappedKeys[0]);
	}

	ShowKeyText(MissingKeyText);
	return false;
}

bool UInputActionKeyDisplayWidget::ShowResolvedKey(FKey Key)
{
	if (!Key.IsValid())
	{
		ShowKeyText(MissingKeyText);
		return false;
	}
	const FInputKeyTextureDataRow* Row = FindKeyTextureRow(Key);
	if (Row && IsValid(Row->Texture) && !Row->bShowKeyText && !bShowKeyTextWithImage)
	{
		ShowKeyImage(*Row);
	}
	else
	{
		if ((Key.IsMouseButton() || Key.GetMenuCategory() == EKeys::NAME_MouseCategory)
			&& (!Row || !IsValid(Row->Texture)) && !WarnedMissingMouseKeys.Contains(Key.GetFName()))
		{
			WarnedMissingMouseKeys.Add(Key.GetFName());
			UE_LOG(LogOutlier, Warning, TEXT("[InputBindKey] No texture for mouse key '%s' in '%s'; using the common frame and key name."),
				*Key.ToString(), *GetNameSafe(MouseKeyTextureTable));
		}
		ShowKeyText(GetKeyDisplayName(Key), Key);
	}
	return true;
}

FText UInputActionKeyDisplayWidget::GetKeyDisplayName(FKey Key) const
{
	if (Key.IsValid() && KeyDisplayNameTable && KeyDisplayNameTable->GetRowStruct() == FInputKeyDisplayNameDataRow::StaticStruct())
	{
		const FInputKeyDisplayNameDataRow* Row = KeyDisplayNameTable->FindRow<FInputKeyDisplayNameDataRow>(
			Key.GetFName(), TEXT("InputBindKeyDisplayName"), false);
		if (Row && !Row->DisplayName.IsEmpty())
		{
			return Row->DisplayName;
		}
	}
	return Key.GetDisplayName(false);
}

const FInputKeyTextureDataRow* UInputActionKeyDisplayWidget::FindKeyTextureRow(FKey Key) const
{
	if (!Key.IsValid() || !MouseKeyTextureTable || MouseKeyTextureTable->GetRowStruct() != FInputKeyTextureDataRow::StaticStruct())
	{
		return nullptr;
	}
	return MouseKeyTextureTable->FindRow<FInputKeyTextureDataRow>(
		Key.GetFName(), TEXT("InputBindKeyTexture"), false);
}

void UInputActionKeyDisplayWidget::ShowKeyText(const FText& Text, FKey Key)
{
	bDisplayingImageKey = false;
	// Showing a label must not discard the key's mapped frame texture.
	const FInputKeyTextureDataRow* Row = FindKeyTextureRow(Key);
	DisplayedKeyTexture = Row && IsValid(Row->Texture) ? Row->Texture.Get() : nullptr;
	ImageDisplaySize = FVector2D::ZeroVector;
	if (KeyText)
	{
		KeyText->SetText(Text);
		KeyText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	RefreshKeyTextColor();
	RefreshBorderBrush();
	RefreshKeyLayout();
}

void UInputActionKeyDisplayWidget::ShowKeyImage(const FInputKeyTextureDataRow& Row)
{
	bDisplayingImageKey = true;
	DisplayedKeyTexture = Row.Texture;
	ImageDisplaySize = Row.DisplaySize;
	if (KeyText)
	{
		KeyText->SetText(FText::GetEmpty());
		KeyText->SetVisibility(ESlateVisibility::Collapsed);
	}
	RefreshBorderBrush();
	RefreshKeyLayout();
}

void UInputActionKeyDisplayWidget::SetBorderImage(UTexture2D* NewImage)
{
	SetNonFrameTexture(NewImage);
}

UTexture2D* UInputActionKeyDisplayWidget::GetBorderImage() const
{
	return IsValid(FrameTexture) ? FrameTexture.Get() : (IsValid(NonFrameTexture) ? NonFrameTexture.Get() : BorderImage.Get());
}

void UInputActionKeyDisplayWidget::SetFrameTexture(UTexture2D* Texture)
{
	FrameTexture = Texture;
	NonFrameTexture = nullptr;
	BorderImage = nullptr;
	RefreshBorderBrush();
	RefreshKeyLayout();
}

void UInputActionKeyDisplayWidget::SetNonFrameTexture(UTexture2D* Texture)
{
	NonFrameTexture = Texture;
	FrameTexture = nullptr;
	BorderImage = nullptr;
	RefreshBorderBrush();
	RefreshKeyLayout();
}

void UInputActionKeyDisplayWidget::SetKeyTextColor(FLinearColor NewColor)
{
	KeyTextColor = NewColor;
	RefreshKeyTextColor();
}

void UInputActionKeyDisplayWidget::RefreshKeyTextColor()
{
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(FSlateColor(KeyTextColor));
	}
}

void UInputActionKeyDisplayWidget::SetDisplayHeight(float NewHeight)
{
	DisplayHeight = FMath::Max(1.0f, NewHeight);
	RefreshBorderBrush();
	RefreshKeyLayout();
}

void UInputActionKeyDisplayWidget::SetUseCaplineAlignment(bool bEnabled)
{
	bUseCaplineAlignment = bEnabled;
	RefreshKeyTextAlignment();
}

FVector2D UInputActionKeyDisplayWidget::GetKeyDisplaySize() const
{
	const double Height = FMath::Max(1.0f, DisplayHeight);
	if (bDisplayingImageKey && DisplayedKeyTexture)
	{
		int32 TextureWidth = DisplayedKeyTexture->GetSizeX();
		int32 TextureHeight = DisplayedKeyTexture->GetSizeY();
#if WITH_EDITORONLY_DATA
		// 에디터 프리뷰, 쿠킹, headless 검사는 render data가 생기기 전에 실행될 수 있다.
		if (TextureWidth <= 0 || TextureHeight <= 0)
		{
			TextureWidth = DisplayedKeyTexture->Source.GetSizeX();
			TextureHeight = DisplayedKeyTexture->Source.GetSizeY();
		}
#endif
		const double Aspect = static_cast<double>(FMath::Max(1, TextureWidth)) / FMath::Max(1, TextureHeight);
		if (ImageDisplaySize.X > 0.0 && ImageDisplaySize.Y > 0.0) return ImageDisplaySize;
		if (ImageDisplaySize.Y > 0.0) return FVector2D(ImageDisplaySize.Y * Aspect, ImageDisplaySize.Y);
		if (ImageDisplaySize.X > 0.0) return FVector2D(ImageDisplaySize.X, ImageDisplaySize.X / Aspect);
		return FVector2D(Height * Aspect, Height);
	}
	// 폰트 크기는 높이에서 정하고, 프레임 너비는 그 폰트로 측정한 라벨에 맞춘다.
	// nine-slice 프레임이 너비를 따라 늘어나므로 모든 키의 글자 크기가 같아진다.
	const double MinWidth = FMath::Max(static_cast<double>(FMath::Max(1.0f, MinTextKeyWidth)), Height);
	const double MaxWidth = FMath::Max(MinWidth, static_cast<double>(MaxTextKeyWidth));
	double Width = MinWidth;
	FSlateFontInfo FittedFont;
	if (KeyText && GetFittedKeyFont(FittedFont))
	{
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FMargin TextPadding = GetTextBorderPadding();
		Width = FMath::CeilToDouble(FontMeasure->Measure(KeyText->GetText(), FittedFont).X) + TextPadding.Left + TextPadding.Right;
		if (Width > Height)
		{
			// 짧은 라벨은 정사각형을 유지하고, 긴 라벨만 프레임과 글자 사이를 띄운다.
			Width += Height * FMath::Max(0.0f, LongLabelSidePaddingRatio) * 2.0;
		}
	}
	return FVector2D(FMath::Clamp(Width, MinWidth, MaxWidth), Height);
}

FMargin UInputActionKeyDisplayWidget::GetTextBorderPadding() const
{
	// 이미지 키는 Border padding을 0으로 바꾸므로 최초 WBP 값을 기준으로 한다.
	if (bHasDefaultBorderPadding)
	{
		return DefaultBorderPadding;
	}
	return Border ? Border->GetPadding() : FMargin(0.0f);
}

bool UInputActionKeyDisplayWidget::GetFittedKeyFont(FSlateFontInfo& OutFont) const
{
	if (!KeyText || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return false;
	}
	OutFont = bHasDefaultKeyFont ? DefaultKeyFont : KeyText->GetFont();
	if (!OutFont.HasValidFont())
	{
		return false;
	}

	// 줄 높이는 폰트 크기에 비례하므로 기준 크기에서 한 번 측정해 역산한다.
	constexpr float ReferenceFontSize = 100.0f;
	FSlateFontInfo ReferenceFont = OutFont;
	ReferenceFont.Size = ReferenceFontSize;
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float ReferenceLineHeight = FontMeasure->GetMaxCharacterHeight(ReferenceFont);
	if (ReferenceLineHeight <= 0.0f)
	{
		return false;
	}

	const FMargin TextPadding = GetTextBorderPadding();
	const float InnerHeight = FMath::Max(1.0f, DisplayHeight - TextPadding.Top - TextPadding.Bottom);
	const float TargetLineHeight = InnerHeight * FMath::Clamp(TextHeightRatio, 0.1f, 1.0f);
	OutFont.Size = FMath::Max(1.0f, TargetLineHeight * ReferenceFontSize / ReferenceLineHeight);
	return true;
}

void UInputActionKeyDisplayWidget::RefreshKeyTextAlignmentForPaint(const FGeometry& Geometry)
{
	if (!KeyText || !bUseCaplineAlignment || bDisplayingImageKey) return;
	// ScaleBox lays out the label at its fitted scale. Measure H at that same font
	// scale: hinting at small DPI sizes is not a linear rescale of the 1080p glyph.
	const FVector2D Desired = KeyText->GetDesiredSize();
	const FMargin TextPadding = GetTextBorderPadding();
	const FVector2D Inner = Geometry.GetLocalSize() - FVector2D(TextPadding.Left + TextPadding.Right, TextPadding.Top + TextPadding.Bottom);
	float FitScale = 1.f;
	if (Desired.X > 0 && Desired.Y > 0)
	{
		FitScale = FMath::Clamp(FMath::Min(Inner.X / Desired.X, Inner.Y / Desired.Y), 0.001, 1.0);
	}
	RefreshKeyTextAlignment(Geometry.GetAccumulatedLayoutTransform().GetScale() * FitScale);
}

void UInputActionKeyDisplayWidget::RefreshKeyTextAlignment(float FontScale)
{
	if (!KeyText) return;
	if (!bHasDefaultKeyTextTranslation)
	{
		DefaultKeyTextTranslation = KeyText->GetRenderTransform().Translation;
		bHasDefaultKeyTextTranslation = true;
	}
	FVector2D Translation = DefaultKeyTextTranslation;
	if (bUseCaplineAlignment && !bDisplayingImageKey && FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetRenderer())
	{
		// Use the same flat capital for every label, including Shift/Space. Per-label
		// ink bounds would move the baseline when switching to Q or lowercase letters.
		const FSlateFontInfo Font = KeyText->GetFont();
		FontScale = FMath::Max(FontScale, 0.001f);
		FCharacterList& Characters = FSlateApplication::Get().GetRenderer()->GetFontCache()->GetCharacterList(Font, FontScale);
		const FCharacterEntry& Cap = Characters.GetCharacter(TEXT('H'), EFontFallback::FF_NoFallback);
		if (Cap.Valid && Cap.VSize > 0 && FMath::IsNearlyEqual(Cap.BitmapRenderScale, 1.f))
		{
			const double LineHeight = Characters.GetMaxHeight();
			const double Baseline = LineHeight + Characters.GetBaseline();
			// Atlas padding is symmetric, so it cancels in the reference glyph center.
			const double CapCenter = Baseline - Cap.VerticalOffset + Cap.VSize * 0.5;
			const double ShadowY = KeyText->GetShadowOffset().Y * FontScale;
			Translation.Y += ((LineHeight + FMath::Abs(ShadowY)) * 0.5
				- (CapCenter + FMath::Max(0.0, -ShadowY))) / FontScale;
		}
	}
	// Render translation leaves desired size and the frame untouched. The existing
	// ScaleBox/DPI transforms scale the correction with the label, including long keys.
	if (!KeyText->GetRenderTransform().Translation.Equals(Translation, 0.0001))
	{
		KeyText->SetRenderTranslation(Translation);
	}
}

void UInputActionKeyDisplayWidget::RefreshKeyLayout()
{
	if (Border)
	{
		if (!bHasDefaultBorderPadding)
		{
			DefaultBorderPadding = Border->GetPadding();
			bHasDefaultBorderPadding = true;
		}
		Border->SetPadding(bDisplayingImageKey ? FMargin(0.0f) : DefaultBorderPadding);
	}
	if (KeyText)
	{
		if (!bHasDefaultKeyFont)
		{
			DefaultKeyFont = KeyText->GetFont();
			bHasDefaultKeyFont = true;
		}
		FSlateFontInfo FittedFont;
		if (!bDisplayingImageKey && GetFittedKeyFont(FittedFont))
		{
			KeyText->SetFont(FittedFont);
		}
	}
	RefreshKeyTextAlignment();
	if (!WidgetTree) return;
	USizeBox* KeySizeBox = WidgetTree->FindWidget<USizeBox>(TEXT("SizeBox"));
	if (!KeySizeBox) KeySizeBox = WidgetTree->FindWidget<USizeBox>(TEXT("SizeBox_0"));
	if (!KeySizeBox) return;
	const FVector2D Size = GetKeyDisplaySize();
	KeySizeBox->SetHeightOverride(Size.Y);
	KeySizeBox->ClearMinDesiredWidth();
	KeySizeBox->ClearMaxDesiredWidth();
	KeySizeBox->SetWidthOverride(Size.X);
}

void UInputActionKeyDisplayWidget::RefreshBorderBrush()
{
	if (!Border)
	{
		return;
	}

	if (!bHasDefaultBorderBrush)
	{
		DefaultBorderBrush = Border->Background;
		bHasDefaultBorderBrush = true;
	}
	FSlateBrush Brush = DefaultBorderBrush;
	bDrawScaledFrame = false;
	ScaledFrameBrush = FSlateBrush();
	// The WBP text override uses its own background (for example an ability icon),
	// rather than replacing it with the key texture from the table.
	UTexture2D* BackgroundTexture = GetBorderImage();
	UTexture2D* ResolvedTexture = bShowKeyTextWithImage && IsValid(BackgroundTexture)
		? BackgroundTexture
		: (IsValid(DisplayedKeyTexture) ? DisplayedKeyTexture.Get() : BackgroundTexture);
	if (bDisplayingImageKey)
	{
		Brush.SetResourceObject(DisplayedKeyTexture);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Margin = FMargin(0.0f);
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Brush.SetImageSize(GetKeyDisplaySize());
	}
	else if (ResolvedTexture)
	{
		Brush.SetResourceObject(ResolvedTexture);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FrameSliceMargin;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Brush.SetImageSize(GetKeyDisplaySize());
		if (IsValid(FrameTexture) && ResolvedTexture == FrameTexture)
		{
			ScaledFrameBrush = Brush;
			ScaledFrameBrush.DrawAs = ESlateBrushDrawType::Image;
			ScaledFrameBrush.Margin = FMargin(0.0f);
			bDrawScaledFrame = true;
			Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		}
	}
	Border->SetBrush(Brush);
	InvalidateLayoutAndVolatility();
}

void UInputActionKeyDisplayWidget::EnsureWatchedKeyIsResolved()
{
	if (!bIsConstructed)
	{
		return;
	}

	SetIsMissingKey(!RefreshDisplayedKey());
	// 고정 키·고정 문구·액션 없음 상태에서는 매핑을 감시하지 않는다.
	if (bUseExplicitKey || !WatchedInputAction || !TextOverride.IsEmpty())
	{
		UnbindControlMappingsRebuiltDelegate();
		return;
	}

	// 조회 성공 후에도 구독을 유지하여 이후 활성 매핑 변경을 반영한다.
	BindControlMappingsRebuiltDelegate();
}

void UInputActionKeyDisplayWidget::SetIsMissingKey(bool bNewIsMissingKey)
{
	if (bIsMissingKey == bNewIsMissingKey)
	{
		return;
	}
	bIsMissingKey = bNewIsMissingKey;
	OnMissingKeyChanged.Broadcast(bIsMissingKey);
}

void UInputActionKeyDisplayWidget::HandleInputActionKeyChanged(
	UInputAction* ChangedInputAction,
	FKey NewKey)
{
	(void)NewKey;

	if (ChangedInputAction == WatchedInputAction)
	{
		EnsureWatchedKeyIsResolved();
	}
}

void UInputActionKeyDisplayWidget::HandleControlMappingsRebuilt()
{
	EnsureWatchedKeyIsResolved();
}

void UInputActionKeyDisplayWidget::BindSettingsSubsystem()
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerSettingsSubsystem* SettingsSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerSettingsSubsystem>()
		: nullptr;

	if (BoundSettingsSubsystem == SettingsSubsystem)
	{
		return;
	}

	UnbindSettingsSubsystem();
	BoundSettingsSubsystem = SettingsSubsystem;

	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputActionKeyChanged.AddUniqueDynamic(
			this,
			&UInputActionKeyDisplayWidget::HandleInputActionKeyChanged);
	}
}

void UInputActionKeyDisplayWidget::UnbindSettingsSubsystem()
{
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputActionKeyChanged.RemoveAll(this);
	}

	BoundSettingsSubsystem = nullptr;
}

void UInputActionKeyDisplayWidget::BindControlMappingsRebuiltDelegate()
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	if (!InputSubsystem)
	{
		UnbindControlMappingsRebuiltDelegate();
		return;
	}

	if (BoundInputSubsystemForRebuild == InputSubsystem)
	{
		// 이미 현재 LocalPlayer의 subsystem에 구독 중.
		return;
	}

	// Owning local player가 바뀐 경우 이전 subsystem의 delegate를 먼저
	// 끊고 새 subsystem에 연결한다.
	UnbindControlMappingsRebuiltDelegate();

	InputSubsystem->ControlMappingsRebuiltDelegate.AddUniqueDynamic(
		this,
		&UInputActionKeyDisplayWidget::HandleControlMappingsRebuilt);
	BoundInputSubsystemForRebuild = InputSubsystem;
}

void UInputActionKeyDisplayWidget::UnbindControlMappingsRebuiltDelegate()
{
	if (BoundInputSubsystemForRebuild)
	{
		BoundInputSubsystemForRebuild->ControlMappingsRebuiltDelegate.RemoveAll(this);
		BoundInputSubsystemForRebuild = nullptr;
	}
}
