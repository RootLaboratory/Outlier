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
class UBorder;

UCLASS()
class OUTLIER_API UShooterAbilitySectionUI : public UUserWidget
{
	GENERATED_BODY()
	
public:
	virtual void NativeConstruct() override;

public:

	UFUNCTION(BlueprintCallable, Category = "MaterialStandard")
	bool IsUnLock();
	const EShooterAbility& GetAbility();
	void SetAbility(EShooterAbility InAbility);
	void AbilityUnLock();

public:
	void SetCoolTime(float InCoolTime);
	void UpdateCoolTime(float delta); //delta 누적 및 Material Update
	bool IsCooldowning();
	void CooldownDone();

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AbilityIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability CoolTime UI|Material")
	TObjectPtr<UMaterialInterface> M_ShooterAbilityCoolTimeUI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShooterAbilityMID;

	//Image -> Material; 변경 시, 기존 Brush
	FSlateBrush DefaultIconBrush;

private:
	EShooterAbility BindAbility = EShooterAbility::None;
	uint8 bAbilityUnlocked : 1 = false;
	uint8 bCooldowning : 1 = false;

	float CoolTime = 0; // 후에 Material 연동
	float AccumulatedTime = 0; // 후에 Material 연동 
};


