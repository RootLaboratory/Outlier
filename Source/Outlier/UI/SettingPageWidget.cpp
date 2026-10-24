#include "UI/SettingPageWidget.h"

#include "Components/ScrollBar.h"
#include "Components/ScrollBox.h"
#include "Engine/LocalPlayer.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBarTrack.h"
#include "Layout/Children.h"

namespace
{
TSharedPtr<SScrollBarTrack> FindScrollBarTrack(const TSharedRef<SWidget>& Widget)
{
	if (Widget->GetType() == FName(TEXT("SScrollBarTrack")))
	{
		return StaticCastSharedRef<SScrollBarTrack>(Widget);
	}
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SScrollBarTrack> Track = FindScrollBarTrack(Children->GetChildAt(Index))) return Track;
	}
	return nullptr;
}
}

void USettingPageWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (IsDesignTime()) UpdatePageScrollBar();
}

float USettingPageWidget::GetFixedThumbFraction()
{
	if (!PageScrollBar) return 1.0f;
	TSharedPtr<SScrollBarTrack> Track = CachedScrollBarTrack.Pin();
	if (!Track)
	{
		Track = FindScrollBarTrack(PageScrollBar->TakeWidget());
		CachedScrollBarTrack = Track;
	}
	if (!Track) return 1.0f;
	const bool bVertical = PageScrollBar->GetOrientation() == Orient_Vertical;
	const FVector2D TrackSize = Track->GetCachedGeometry().GetLocalSize();
	float TrackLength = bVertical ? TrackSize.Y : TrackSize.X;
	if (TrackLength <= 0.0f && IsDesignTime()) TrackLength = PageScrollBarPreviewTrackLength;
	const float MinThumb = Track->GetMinThumbSize();
	if (TrackLength <= MinThumb) return 1.0f;
	// Slate adds its minimum thumb length to fraction * (track length - minimum).
	const float ThumbLength = FMath::Clamp(PageScrollBarThumbLength, MinThumb, TrackLength);
	return FMath::Clamp((ThumbLength - MinThumb) / (TrackLength - MinThumb), UE_SMALL_NUMBER, 1.0f);
}

void USettingPageWidget::UpdatePageScrollBar()
{
	if (!PageScrollBox || !PageScrollBar) return;
	if (PageScrollBarThumbLength > 0.0f)
	{
		const float ThumbFraction = GetFixedThumbFraction();
		const float EndOffset = PageScrollBox->GetScrollOffsetOfEnd();
		const float Progress = EndOffset > UE_SMALL_NUMBER
			? FMath::Clamp(PageScrollBox->GetScrollOffset() / EndOffset, 0.0f, 1.0f) : 0.0f;
		PageScrollBar->SetState(Progress * (1.0f - ThumbFraction), ThumbFraction);
	}
	else
	{
		PageScrollBar->SetState(PageScrollBox->GetViewOffsetFraction(), PageScrollBox->GetViewFraction());
	}
}

void USettingPageWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!PageScrollBox || !PageScrollBar) return;

	PageScrollBox->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	const TSharedRef<SScrollBar> SlateScrollBar = StaticCastSharedRef<SScrollBar>(PageScrollBar->TakeWidget());
	SlateScrollBar->SetOnUserScrolled(FOnUserScrolled::CreateUObject(
		this, &USettingPageWidget::HandlePageScrollBarScrolled));
}

void USettingPageWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Polled so wheel, focus scrolling, scroll animation and list rebuilds all stay in sync.
	UpdatePageScrollBar();
}

void USettingPageWidget::HandlePageScrollBarScrolled(float OffsetFraction)
{
	if (!PageScrollBox) return;
	if (PageScrollBarThumbLength > 0.0f && PageScrollBar)
	{
		const float TravelFraction = 1.0f - GetFixedThumbFraction();
		const float Progress = TravelFraction > UE_SMALL_NUMBER
			? FMath::Clamp(OffsetFraction / TravelFraction, 0.0f, 1.0f) : 0.0f;
		PageScrollBox->SetScrollOffset(Progress * PageScrollBox->GetScrollOffsetOfEnd());
		return;
	}
	const float ViewSize = PageScrollBox->GetCachedGeometry().GetLocalSize().Y;
	const float EndOffset = PageScrollBox->GetScrollOffsetOfEnd();
	PageScrollBox->SetScrollOffset(FMath::Min(OffsetFraction * (EndOffset + ViewSize), EndOffset));
}

void USettingPageWidget::ActivatePage()
{
	RebuildKeyboardFocusTargets();
	CurrentFocusedIndex = KeyboardFocusTargets.IsEmpty() ? INDEX_NONE : 0;
}

void USettingPageWidget::DeactivatePage() {}
bool USettingPageWidget::HandlePageKeyDown(const FKeyEvent& KeyEvent) { return false; }
void USettingPageWidget::RebuildKeyboardFocusTargets() { KeyboardFocusTargets.Reset(); }

bool USettingPageWidget::HandlePageInput(ESettingPageInput Input)
{
	if (Input == ESettingPageInput::Up) return MoveKeyboardFocus(-1);
	if (Input == ESettingPageInput::Down) return MoveKeyboardFocus(1);
	return false;
}

void USettingPageWidget::HandlePagePointerDown(const FPointerEvent& PointerEvent)
{
	RebuildKeyboardFocusTargets();
	for (int32 Index = 0; Index < KeyboardFocusTargets.Num(); ++Index)
	{
		UWidget* Target = KeyboardFocusTargets[Index];
		if (Target && Target->IsVisible() && Target->GetIsEnabled()
			&& Target->GetCachedGeometry().IsUnderLocation(PointerEvent.GetScreenSpacePosition()))
		{
			CurrentFocusedIndex = Index;
			return;
		}
	}
}

void USettingPageWidget::FocusPage(bool bLastTarget)
{
	RebuildKeyboardFocusTargets();
	if (bLastTarget) CurrentFocusedIndex = KeyboardFocusTargets.Num() - 1;
	else if (!KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)) CurrentFocusedIndex = 0;
	FocusCurrentKeyboardTarget();
}

void USettingPageWidget::FocusCurrentKeyboardTarget()
{
	if (!KeyboardFocusTargets.IsValidIndex(CurrentFocusedIndex)) return;
	UWidget* Target = KeyboardFocusTargets[CurrentFocusedIndex];
	if (Target && Target->IsVisible() && Target->GetIsEnabled())
	{
		if (APlayerController* Player = GetOwningPlayer()) Target->SetUserFocus(Player);
	}
}

bool USettingPageWidget::MoveKeyboardFocus(int32 Direction)
{
	RebuildKeyboardFocusTargets();
	const int32 Next = CurrentFocusedIndex + Direction;
	if (!KeyboardFocusTargets.IsValidIndex(Next)) return false;
	CurrentFocusedIndex = Next;
	FocusCurrentKeyboardTarget();
	return true;
}

ULocalPlayerSettingsSubsystem* USettingPageWidget::GetSettingsSubsystem() const
{
	const ULocalPlayer* Player = GetOwningLocalPlayer();
	return Player ? Player->GetSubsystem<ULocalPlayerSettingsSubsystem>() : nullptr;
}
