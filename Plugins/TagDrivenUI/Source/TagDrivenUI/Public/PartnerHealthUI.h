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
	virtual void NativeDestruct() override;
	virtual void Activate() override;
	virtual void Deactivate() override;

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void HealthChanged(float InHealthRatio);

	void SetHealthState(float InHealth, float InMaxHealth, bool bImmediate = false);

private:
	void SetProgressBarRatio(float InRatio, bool bImmediate = false);
	void SetValueText(float InValue);
	bool bHealthUpdateImmediate = false;
	bool bHasHealthState = false;
	float CachedMaxHealth = 0.0f;

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
