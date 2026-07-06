// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "HPBarUI.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProgressBar;

/**
 * 
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UHPBarUI : public UEventDrivenUI
{
	GENERATED_BODY()
	
public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void ShieldChanged(float InShieldRatio);
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void HealthChanged(float InHealthRatio);

	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void PartnerShieldChanged(float InPartnerShieldRatio);

private:
	void InitializeProgressBarMaterial(const TCHAR* DebugName, UProgressBar* ProgressBar, const FColor& BarColor, TObjectPtr<UMaterialInstanceDynamic>& OutMID);
	void SetProgressBarRatio(UProgressBar* ProgressBar, float InRatio);

public:
	UPROPERTY(BlueprintReadWrite, Category = "Data")
	float CurrentHPRatio = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Data")
	float CurrentShieldRatio = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Data")
	float CurrentPartnerShieldRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	TObjectPtr<UMaterialInterface> ProgressBarFillMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FName FillColorParameterName = TEXT("FillColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FColor HPColor = FColor(228, 47, 81, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FColor ShieldColor = FColor(134, 187, 223, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FColor PartnerShieldColor = FColor(38, 255, 166, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	uint8 bDebugHPBarUI : 1 = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "UIMaterial")
	TObjectPtr<UMaterialInstanceDynamic> ProgressBarMID;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HPBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> ShieldBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> PartnerShieldBar;

};
