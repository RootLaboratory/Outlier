#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "HackCandidateMarkerWidget.generated.h"

class UButton;
class UHackableInfoWidget;
class UHackableComponent;
class UPartnerHackComponent;

UCLASS()
class OUTLIER_API UHackCandidateMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeMarker(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent);
	void SetHackableInfoWidgetClass(TSubclassOf<UHackableInfoWidget> InHackableInfoWidgetClass);
	void EnsureHackableInfoWidget();
	void SetHackHoldProgress(float InProgress);
	void StartHackHold(float InHoldDuration);
	void CancelHackHold();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SelectButton;

private:
	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();

	void ShowHackableInfoWidget();
	void HideHackableInfoWidget();
	void TickHackHold();
	bool CalculateInfoWidgetLayout(FVector2D& OutPosition, FVector2D& OutSize) const;

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY()
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Hack")
	TSubclassOf<UHackableInfoWidget> HackableInfoWidgetClass;

	UPROPERTY()
	TObjectPtr<UHackableInfoWidget> HackableInfoWidget;

	float HackHoldDuration = 0.0f;
	float HackHoldStartTime = 0.0f;
	bool bIsHoldingHack = false;
	FTimerHandle HackHoldTimerHandle;
};
