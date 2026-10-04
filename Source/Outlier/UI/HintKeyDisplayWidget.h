#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "HintKeyDisplayWidget.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UBorder;
class UInputAction;
class ULocalPlayerSettingsSubsystem;
class UTextBlock;
class UTexture2D;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UHintKeyDisplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetWatchedInputAction(UInputAction* NewInputAction);

	UFUNCTION(BlueprintPure, Category = "UI|Input")
	UInputAction* GetWatchedInputAction() const { return WatchedInputAction; }

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetTextOverride(const FText& InOverrideText);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void ClearTextOverride();

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	bool RefreshDisplayedKey();

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetFrameImage(UTexture2D* NewImage);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> Frame;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UTexture2D> FrameImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UInputAction> WatchedInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input")
	FText MissingKeyText;

private:
	UFUNCTION()
	void HandleInputActionKeyChanged(UInputAction* ChangedInputAction, FKey NewKey);

	UFUNCTION()
	void HandleControlMappingsRebuilt();

	void EnsureWatchedKeyIsResolved();
	void BindSettingsSubsystem();
	void UnbindSettingsSubsystem();
	void RefreshFrameBrush();
	void BindControlMappingsRebuiltDelegate();
	void UnbindControlMappingsRebuiltDelegate();

	UPROPERTY(Transient)
	FText TextOverride;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystemForRebuild;

	bool bIsConstructed = false;
};
