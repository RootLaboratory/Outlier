#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Drone/Partner/HackType.h"
#include "GameplayTagContainer.h"
#include "HackMiniGameWidget.generated.h"

class UHackableComponent;
class UHackingMiniGameBase;
class UCanvasPanel;
class UPartnerHackComponent;
class UProgressBar;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackMiniGameWidgetFinished, const FHackResultContext&, ResultContext);

UCLASS(Blueprintable)
class OUTLIER_API UHackMiniGameWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void InitializeHackMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent);

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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> TimeProgressBar;

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
	void ClearActiveMiniGame();

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

	float MiniGameTimeLimit = 0.0f;
};
