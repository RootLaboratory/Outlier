// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Shooter/Ability/ShooterAbility.h"
#include "ShooterAbilitySectionUI.generated.h"

/**
 * 
 */

class UImage;

UCLASS()
class OUTLIER_API UShooterAbilitySectionUI : public UUserWidget
{
	GENERATED_BODY()
	
public:
	const EShooterAbility& GetAbility();

	void SetAbility(EShooterAbility InAbility);

public:
	void AbilityUnLock();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> BackgroundImage;

	/*UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AbilityIcon;*/

public:

	UFUNCTION(BlueprintCallable, Category = "MaterialStandard")
	bool IsUnLock();

	UFUNCTION(BlueprintCallable)
	void SetHovered(bool bInHovered);

	UFUNCTION(BlueprintImplementableEvent)
	void OnVisualStateChanged();

private:
	EShooterAbility BindAbility = EShooterAbility::None;
	bool bAbilityUnlocked : 1 = false;
	bool bHovered : 1 = false;
};

//
