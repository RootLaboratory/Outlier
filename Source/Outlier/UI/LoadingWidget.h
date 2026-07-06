// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoadingWidget.generated.h"

class UImage;
class UMaterialInstance;
class UMaterialInstanceDynamic;
class UTextBlock;

UCLASS()
class OUTLIER_API ULoadingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Loading")
	void SetDescriptionText(const FText& NewDescription);

	UFUNCTION(BlueprintCallable, Category = "Loading")
	void SetLoadingProgress(float NewProgress);

	UFUNCTION(BlueprintPure, Category = "Loading")
	float GetLoadingProgress() const { return LoadingProgressValue; }

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> LoadingBackGroundImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Description;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> LoadingProgress;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> GangTongImage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading")
	TObjectPtr<UMaterialInstance> MaterialInstance;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Loading")
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterialInstance;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading")
	FName ProgressParameterName = TEXT("progress");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading|GangTong")
	float GangTongBobAmplitude = 16.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading|GangTong")
	float GangTongBobSpeed = 2.5f;

private:
	void UpdateProgressMaterial();
	void UpdateGangTongAnimation(float InDeltaTime);

	float LoadingProgressValue = 0.0f;
	float GangTongElapsedTime = 0.0f;
	FVector2D GangTongInitialTranslation = FVector2D::ZeroVector;
};
