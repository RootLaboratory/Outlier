#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Drone/Partner/HackType.h"
#include "GameplayTagContainer.h"
#include "Styling/SlateBrush.h"
#include "HackMiniGameWidget.generated.h"

class UHackableComponent;
class UHackingMiniGameBase;
class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UPartnerHackComponent;
class UProgressBar;
class UGameViewportClient;
class SWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackMiniGameWidgetFinished, const FHackResultContext&, ResultContext);

UCLASS(Blueprintable)
class OUTLIER_API UHackMiniGameWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void InitializeHackMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent,
		UPartnerHackComponent* InHackComponent, float InClickSuccessRangeMultiplier = 1.0f, float InSpinningGapMultiplier = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void SetMiniGameTimeLimit(float InTimeLimit);

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	bool StartHacking();

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void CancelHacking();

	UPROPERTY(BlueprintAssignable, Category = "Hack|MiniGame")
	FOnHackMiniGameWidgetFinished OnHackMiniGameFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MiniGameRoot;

	/** Brush and tint used by the software cursor during the minigame. Kept hidden in the widget tree. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FakeCursorImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> TimeProgressBar;

	/** Elapsed time / time limit (0-1), matching the EMP progress material. */
	UPROPERTY(EditDefaultsOnly, Category = "Hack|MiniGame|Progress")
	FName ProgressParameterName = TEXT("Progress");

	UPROPERTY(BlueprintReadOnly, Category = "Hack|MiniGame")
	float ElapsedTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|MiniGame")
	uint8 bIsTimeLimited : 1 = true;

	UPROPERTY(EditDefaultsOnly, Category = "Hack|MiniGame")
	TMap<FGameplayTag, TSubclassOf<UHackingMiniGameBase>> MiniGameWidgetClassesByTag;

	UPROPERTY(EditDefaultsOnly, Category = "Hack|MiniGame")
	TSubclassOf<UHackingMiniGameBase> DefaultMiniGameWidgetClass;

private:
	TSubclassOf<UHackingMiniGameBase> ResolveMiniGameWidgetClass() const;
	bool ResolveIsTimeLimited() const;
	void RefreshTimeProgressBar();
	void EnsureProgressMaterial();
	void UpdateTimeProgress(float Progress);
	void ClearActiveMiniGame();
	void ApplyMiniGameCursor();
	void RestoreCursor();

	UFUNCTION()
	void HandleActiveMiniGameFinished(EHackResult Result);

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY()
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY()
	TObjectPtr<UHackingMiniGameBase> ActiveMiniGameWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ProgressMaterial;

	float MiniGameTimeLimit = 0.0f;
	float ClickSuccessRangeMultiplier = 1.0f;
	float SpinningGapMultiplier = 1.0f;

	UPROPERTY(Transient)
	FSlateBrush MiniGameCursorBrush;

	TWeakObjectPtr<UGameViewportClient> CursorViewport;
	TSharedPtr<SWidget> PreviousCursorWidget;
	TSharedPtr<SWidget> MiniGameCursorWidget;
};
