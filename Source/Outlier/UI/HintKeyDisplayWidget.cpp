#include "UI/HintKeyDisplayWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"

namespace
{
FText GetHintCompactKeyDisplayName(const FKey& Key)
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
		{EKeys::MiddleMouseButton, TEXT("MMB")},
		{EKeys::ThumbMouseButton, TEXT("M4")},
		{EKeys::ThumbMouseButton2, TEXT("M5")},
		{EKeys::MouseScrollUp, TEXT("Wheel ↑")},
		{EKeys::MouseScrollDown, TEXT("Wheel ↓")},
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

void UHintKeyDisplayWidget::NativeConstruct()
{
	Super::NativeConstruct();

	bIsConstructed = true;

	if (Key)
	{
		Key->SetAutoWrapText(false);
		Key->SetWrapTextAt(0.f);
		Key->SetJustification(ETextJustify::Center);
	}

	if (MissingKeyText.IsEmpty())
	{
		MissingKeyText = FText::FromString(TEXT("-"));
	}

	RefreshFrameBrush();
	BindSettingsSubsystem();
	EnsureWatchedKeyIsResolved();
}

void UHintKeyDisplayWidget::NativeDestruct()
{
	bIsConstructed = false;
	UnbindControlMappingsRebuiltDelegate();
	UnbindSettingsSubsystem();

	Super::NativeDestruct();
}

void UHintKeyDisplayWidget::SetWatchedInputAction(UInputAction* NewInputAction)
{
	WatchedInputAction = NewInputAction;

	if (bIsConstructed)
	{
		EnsureWatchedKeyIsResolved();
	}
}

void UHintKeyDisplayWidget::SetTextOverride(const FText& InOverrideText)
{
	TextOverride = InOverrideText;
	EnsureWatchedKeyIsResolved();
}

void UHintKeyDisplayWidget::ClearTextOverride()
{
	TextOverride = FText::GetEmpty();
	EnsureWatchedKeyIsResolved();
}

bool UHintKeyDisplayWidget::RefreshDisplayedKey()
{
	if (!Key)
	{
		return false;
	}

	if (!TextOverride.IsEmpty())
	{
		Key->SetText(TextOverride);
		return true;
	}

	if (!WatchedInputAction)
	{
		Key->SetText(MissingKeyText);
		return false;
	}

	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	if (!InputSubsystem)
	{
		Key->SetText(MissingKeyText);
		return false;
	}

	const TArray<FKey> MappedKeys =
		InputSubsystem->QueryKeysMappedToAction(WatchedInputAction);
	if (MappedKeys.IsValidIndex(0) && MappedKeys[0].IsValid())
	{
		Key->SetText(GetHintCompactKeyDisplayName(MappedKeys[0]));
		return true;
	}

	Key->SetText(MissingKeyText);
	return false;
}

void UHintKeyDisplayWidget::SetFrameImage(UTexture2D* NewImage)
{
	FrameImage = NewImage;
	RefreshFrameBrush();
}

void UHintKeyDisplayWidget::RefreshFrameBrush()
{
	if (!Frame || !FrameImage)
	{
		return;
	}

	Frame->SetBrushFromTexture(FrameImage);
}

void UHintKeyDisplayWidget::EnsureWatchedKeyIsResolved()
{
	if (!bIsConstructed)
	{
		return;
	}

	if (RefreshDisplayedKey() || !WatchedInputAction)
	{
		UnbindControlMappingsRebuiltDelegate();
		return;
	}

	BindControlMappingsRebuiltDelegate();
}

void UHintKeyDisplayWidget::HandleInputActionKeyChanged(
	UInputAction* ChangedInputAction,
	FKey NewKey)
{
	(void)NewKey;

	if (ChangedInputAction == WatchedInputAction)
	{
		EnsureWatchedKeyIsResolved();
	}
}

void UHintKeyDisplayWidget::HandleControlMappingsRebuilt()
{
	EnsureWatchedKeyIsResolved();
}

void UHintKeyDisplayWidget::BindSettingsSubsystem()
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
			&UHintKeyDisplayWidget::HandleInputActionKeyChanged);
	}
}

void UHintKeyDisplayWidget::UnbindSettingsSubsystem()
{
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnInputActionKeyChanged.RemoveAll(this);
	}

	BoundSettingsSubsystem = nullptr;
}

void UHintKeyDisplayWidget::BindControlMappingsRebuiltDelegate()
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
		return;
	}

	UnbindControlMappingsRebuiltDelegate();

	InputSubsystem->ControlMappingsRebuiltDelegate.AddUniqueDynamic(
		this,
		&UHintKeyDisplayWidget::HandleControlMappingsRebuilt);
	BoundInputSubsystemForRebuild = InputSubsystem;
}

void UHintKeyDisplayWidget::UnbindControlMappingsRebuiltDelegate()
{
	if (BoundInputSubsystemForRebuild)
	{
		BoundInputSubsystemForRebuild->ControlMappingsRebuiltDelegate.RemoveAll(this);
		BoundInputSubsystemForRebuild = nullptr;
	}
}
