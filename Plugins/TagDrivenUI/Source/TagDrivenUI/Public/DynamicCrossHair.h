// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CrossHairBase.h"
#include "DynamicCrossHair.generated.h"

/**
 * 
 */
class ULocalPlayerUISubSystem;

UCLASS()
class TAGDRIVENUI_API UDynamicCrossHair : public UCrossHairBase
{
	GENERATED_BODY()

	

public:
	  virtual void NativeConstruct() override;

	  virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnCrossHairTick(float InDeltaTime);


	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void SpawnCriticalUI();
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void SpawnHitUI();
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void SpawnAdaptedHitUI();
	UFUNCTION(BlueprintNativeEvent, Category = "UI")
	void SpawnKillHitUI();

	virtual void SpawnCriticalUI_Implementation();
	virtual void SpawnHitUI_Implementation();
	virtual void SpawnAdaptedHitUI_Implementation();
	virtual void SpawnKillHitUI_Implementation();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	float CrossHairLength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")

	float CrossHairThickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")

	float Offset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	float CrossHairSpreaed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data")
	float Ratio;



private:
	UPROPERTY()
	TObjectPtr<ULocalPlayerUISubSystem> CachedUISubsystem;

};

