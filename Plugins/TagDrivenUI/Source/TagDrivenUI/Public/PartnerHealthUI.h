// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "PartnerHealthUI.generated.h"

class UProgressBar;
class UTextBlock;

/** Partner 전용 단일 HP 바. */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerHealthUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void HealthChanged(float InHealthRatio);

	void SetHealthState(float InHealth, float InMaxHealth);

private:
	void SetProgressBarRatio(float InRatio);
	void SetValueText(float InValue);

public:
	UPROPERTY(BlueprintReadWrite, Category = "Data")
	float CurrentHPRatio = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	float CurrentHPValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	uint8 bDebugHPBarUI : 1 = true;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HPBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Data")
	TObjectPtr<UTextBlock> HPText;
};
