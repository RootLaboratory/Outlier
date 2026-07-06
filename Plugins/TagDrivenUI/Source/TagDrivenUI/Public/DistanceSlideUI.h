// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "DistanceSlideUI.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProgressBar;

/**
 * 
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UDistanceSlideUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateDistanceRatio(float InRatio);

private:
	void UpdateSlideRatioImagePosition();
	void UpdateLimitOverMaterial(bool bLimitOver);
	void CacheSlideRatioImageBasePosition();

public:
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	float CurrentDistanceRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	TObjectPtr<UMaterialInterface> DistanceLimitMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DistanceLimitMID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FName LimitOverParameterName = TEXT("flag");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	float SlideBarFallbackWidth = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	float SlideRatioImageOffsetX = 0.0f;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> LimitOverImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> SlideBarProgressBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SlideRatioImage;

private:
	FVector2D SlideRatioImageBasePosition = FVector2D::ZeroVector;
	uint8 bHasCachedLimitOverState : 1 = false;
	uint8 bCachedLimitOverState : 1 = false;
	uint8 bHasCachedSlideRatioImageBasePosition : 1 = false;
};
