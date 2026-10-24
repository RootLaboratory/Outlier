#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingPageWidget.generated.h"

class ULocalPlayerSettingsSubsystem;
class UScrollBar;
class UScrollBox;
class SScrollBarTrack;

UENUM(BlueprintType)
enum class ESettingPageInput : uint8
{
	Confirm, Up, Down, Left, Right, Escape
};

// Only the containing SettingWidget receives layer input. Pages consume injected input.
UCLASS(Abstract, Blueprintable)
class OUTLIER_API USettingPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void ActivatePage();
	virtual void DeactivatePage();
	virtual bool HandlePageInput(ESettingPageInput Input);
	virtual bool HandlePageKeyDown(const FKeyEvent& KeyEvent);
	virtual void HandlePagePointerDown(const FPointerEvent& PointerEvent);
	virtual bool IsCapturingKeyboardInput() const { return false; }
	void FocusPage(bool bLastTarget = false);

	DECLARE_MULTICAST_DELEGATE(FOnKeyboardCaptureRequested);
	FOnKeyboardCaptureRequested OnKeyboardCaptureRequested;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void RebuildKeyboardFocusTargets();
	virtual void FocusCurrentKeyboardTarget();
	bool MoveKeyboardFocus(int32 Direction);
	ULocalPlayerSettingsSubsystem* GetSettingsSubsystem() const;

	// Optional pair: PageScrollBox's own bar is hidden and PageScrollBar, placed anywhere in the layout, drives it.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Scroll")
	TObjectPtr<UScrollBox> PageScrollBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Setting|Scroll")
	TObjectPtr<UScrollBar> PageScrollBar;

	// UI units. Zero keeps the standard content-proportional thumb.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Scroll", meta = (ClampMin = "0.0"))
	float PageScrollBarThumbLength = 0.0f;

	// Used only in the designer before Slate has an arranged track geometry.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Setting|Scroll", meta = (ClampMin = "1.0"))
	float PageScrollBarPreviewTrackLength = 600.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> KeyboardFocusTargets;
	int32 CurrentFocusedIndex = INDEX_NONE;

private:
	void UpdatePageScrollBar();
	float GetFixedThumbFraction();
	TWeakPtr<SScrollBarTrack> CachedScrollBarTrack;
	void HandlePageScrollBarScrolled(float OffsetFraction);
};
