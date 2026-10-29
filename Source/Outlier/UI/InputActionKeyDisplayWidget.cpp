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
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "UI/InputKeyDisplayNameDataRow.h"
#include "UI/InputKeyTextureDataRow.h"
#include "Outlier.h"

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
	BorderImage = NewImage;
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
	// The WBP text override uses its own background (for example an ability icon),
	// rather than replacing it with the key texture from the table.
	UTexture2D* FrameTexture = bShowKeyTextWithImage && IsValid(BorderImage)
		? BorderImage.Get()
		: (IsValid(DisplayedKeyTexture) ? DisplayedKeyTexture.Get() : BorderImage.Get());
	if (bDisplayingImageKey)
	{
		Brush.SetResourceObject(DisplayedKeyTexture);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Margin = FMargin(0.0f);
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Brush.SetImageSize(GetKeyDisplaySize());
	}
	else if (FrameTexture)
	{
		Brush.SetResourceObject(FrameTexture);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FrameSliceMargin;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Brush.SetImageSize(GetKeyDisplaySize());
	}
	Border->SetBrush(Brush);
}

void UInputActionKeyDisplayWidget::EnsureWatchedKeyIsResolved()
{
	if (!bIsConstructed)
	{
		return;
	}

	// WatchedInputAction이 아직 없으면 기다릴 이유가 없다 - SetWatchedInputAction이
	// 호출될 때 다시 이 함수를 타게 된다.
	if (RefreshDisplayedKey() || !WatchedInputAction)
	{
		UnbindControlMappingsRebuiltDelegate();
		return;
	}

	// 액션은 있는데 아직 매핑을 못 찾았다 - Enhanced Input이 아직 IMC를
	// 등록/재구성하기 전일 가능성이 크므로, 재구성이 실제로 끝나는 시점에
	// 다시 시도하도록 예약해둔다.
	BindControlMappingsRebuiltDelegate();
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
