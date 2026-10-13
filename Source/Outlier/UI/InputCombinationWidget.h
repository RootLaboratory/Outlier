#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "UI/InputDisplayTypes.h"
#include "InputCombinationWidget.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UHorizontalBox;
class UInputAction;
class UInputActionKeyDisplayWidget;
class ULocalPlayerSettingsSubsystem;
class UTexture2D;
class UWidget;

/**
 * Reuses the BindKey WBP to render a runtime input expression horizontally.
 * Assign KeyDisplayWidgetClass to the existing InputActionKeyDisplayWidget WBP.
 * A native instance supplies its own HorizontalBox; a WBP can supply InputItems.
 * Examples (each entry is a term, with its preceding separator):
 * Move(AllMapped); Sprint(FirstMapped), + Move(AllMapped);
 * Crouch(FirstMapped), + Sprint(FirstMapped), + Move(AllMapped).
 */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UInputCombinationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetDisplayDefinition(const FInputDisplayDefinition& Definition);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetExpression(const TArray<FInputDisplayTerm>& NewExpression);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void ClearExpression();

	/** Re-query active mappings. False means a missing mapping or invalid widget configuration. */
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	bool RefreshExpression();

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetKeyFrameTexture(UTexture2D* Texture);

	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetKeyTextColor(FLinearColor NewColor);

	virtual void SynchronizeProperties() override;

	/** Runtime data supplied by the resolver; WBP defaults do not author expressions. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Input")
	TArray<FInputDisplayTerm> Expression;

	/** Concrete WBP subclass carrying the shared key texture table, frame and text styling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TSubclassOf<UInputActionKeyDisplayWidget> KeyDisplayWidgetClass;

	/** One frame injected into every generated text key. Dedicated image keys keep their own texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UTexture2D> DefaultKeyFrameTexture;

	/** Color injected into every generated key display, overriding the leaf WBP's default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetKeyTextColor, Category = "UI|Input")
	FLinearColor KeyTextColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float KeyDisplayHeight = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UTexture2D> PlusTexture;

	/** Texture symbols are scaled to fit this area while preserving their aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout")
	FVector2D SymbolSize = FVector2D(16.0, 16.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "0.0"))
	float KeySpacing = 8.0f;

	/** Gap between adjacent IA groups when no visible symbol separates them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "0.0"))
	float GroupSpacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout")
	FMargin SymbolPadding = FMargin(8.0f, 0.0f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Optional for native use. A custom WBP root must contain a HorizontalBox named InputItems. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> InputItems;

private:
	bool EnsureInputPanel();
	UWidget* CreateSeparator(const FInputDisplayTerm& Term);
	void BindInputEvents();
	void UnbindInputEvents();

	UFUNCTION()
	void HandleInputActionKeyChanged(UInputAction* ChangedInputAction, FKey NewKey);

	UFUNCTION()
	void HandleControlMappingsRebuilt();

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	bool bIsConstructed = false;
};
