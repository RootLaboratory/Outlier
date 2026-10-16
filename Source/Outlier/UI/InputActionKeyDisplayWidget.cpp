#include "UI/InputActionKeyDisplayWidget.h"

#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "UI/InputKeyTextureDataRow.h"
#include "Outlier.h"

namespace
{
FText GetCompactKeyDisplayName(const FKey& Key)
{
	static const TPair<FKey, const TCHAR*> Aliases[] = {
		{EKeys::LeftShift, TEXT("LShift")},
		{EKeys::RightShift, TEXT("RShift")},
		{EKeys::LeftControl, TEXT("LCtrl")},
		{EKeys::RightControl, TEXT("RCtrl")},
		{EKeys::LeftAlt, TEXT("LAlt")},
		{EKeys::RightAlt, TEXT("RAlt")},
		{EKeys::LeftCommand, TEXT("LWin")},
		{EKeys::RightCommand, TEXT("RWin")},
		{EKeys::BackSpace, TEXT("Bksp")},
		{EKeys::Up, TEXT("↑")},
		{EKeys::Down, TEXT("↓")},
		{EKeys::Left, TEXT("←")},
		{EKeys::Right, TEXT("→")},
		{EKeys::NumPadZero, TEXT("N0")},
		{EKeys::NumPadOne, TEXT("N1")},
		{EKeys::NumPadTwo, TEXT("N2")},
		{EKeys::NumPadThree, TEXT("N3")},
		{EKeys::NumPadFour, TEXT("N4")},
		{EKeys::NumPadFive, TEXT("N5")},
		{EKeys::NumPadSix, TEXT("N6")},
		{EKeys::NumPadSeven, TEXT("N7")},
		{EKeys::NumPadEight, TEXT("N8")},
		{EKeys::NumPadNine, TEXT("N9")},
		{EKeys::Add, TEXT("N+")},
		{EKeys::Subtract, TEXT("N-")},
		{EKeys::Multiply, TEXT("N*")},
		{EKeys::Divide, TEXT("N/")},
		{EKeys::Decimal, TEXT("N.")},
		{EKeys::NumLock, TEXT("Num")},
		{EKeys::ScrollLock, TEXT("ScrLk")},
	};

	for (const TPair<FKey, const TCHAR*>& Alias : Aliases)
	{
		if (Key == Alias.Key)
		{
			return FText::FromString(Alias.Value);
		}
	}

	return Key.GetDisplayName(false);
}
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

void UInputActionKeyDisplayWidget::SetDisplayedKey(FKey NewKey)
{
	bUseExplicitKey = true;
	ExplicitKey = NewKey;
	WatchedInputAction = nullptr;
	TextOverride = FText::GetEmpty();
	UnbindSettingsSubsystem();
	UnbindControlMappingsRebuiltDelegate();
	EnsureWatchedKeyIsResolved();
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
	if (Row && IsValid(Row->Texture) && !Row->bShowKeyText)
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
		ShowKeyText(GetCompactKeyDisplayName(Key), Key);
	}
	return true;
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
	(void)Key;
	bDisplayingImageKey = false;
	DisplayedKeyTexture = nullptr;
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
	return FVector2D(FMath::Max(1.0f, MinTextKeyWidth), Height);
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
	if (!WidgetTree) return;
	USizeBox* KeySizeBox = WidgetTree->FindWidget<USizeBox>(TEXT("SizeBox"));
	if (!KeySizeBox) KeySizeBox = WidgetTree->FindWidget<USizeBox>(TEXT("SizeBox_0"));
	if (!KeySizeBox) return;
	const FVector2D Size = GetKeyDisplaySize();
	KeySizeBox->SetHeightOverride(Size.Y);
	if (bDisplayingImageKey)
	{
		KeySizeBox->ClearMinDesiredWidth();
		KeySizeBox->ClearMaxDesiredWidth();
		KeySizeBox->SetWidthOverride(Size.X);
	}
	else
	{
		KeySizeBox->ClearWidthOverride();
		KeySizeBox->SetMinDesiredWidth(Size.X);
		KeySizeBox->SetMaxDesiredWidth(FMath::Max(static_cast<float>(Size.X), MaxTextKeyWidth));
	}
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
	if (bDisplayingImageKey)
	{
		Brush.SetResourceObject(DisplayedKeyTexture);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Margin = FMargin(0.0f);
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Brush.SetImageSize(GetKeyDisplaySize());
	}
	else if (BorderImage)
	{
		Brush.SetResourceObject(BorderImage);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FrameSliceMargin;
		Brush.Tiling = ESlateBrushTileType::NoTile;
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
