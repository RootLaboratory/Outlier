// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "AbilityIconUI.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextBlock;

UCLASS()
class OUTLIER_API UAbilityIconUI : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

public:
	UFUNCTION(BlueprintPure, Category = "Ability")
	bool IsUnLock() const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	bool IsAbilityEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetAbilityEnabled(bool bInAbilityEnabled);

	UFUNCTION(BlueprintPure, Category = "Ability")
	FGameplayTag GetAbilityTag() const;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetAbility(FGameplayTag InAbility);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void AbilityUnLock();

public:
	UFUNCTION(BlueprintCallable, Category = "Ability|Cooldown")
	void SetCoolTime(float InCoolTime);

	void UpdateCoolTime(float Delta);

	UFUNCTION(BlueprintPure, Category = "Ability|Cooldown")
	bool IsCooldowning() const;

	UFUNCTION(BlueprintCallable, Category = "Ability|Cooldown")
	void CooldownDone();

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AbilityIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> DisabledImage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AbilityMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisabledMID;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Cooldown|Material")
	TObjectPtr<UMaterialInterface> M_AbilityCoolTimeUI;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Disabled|Material")
	TObjectPtr<UMaterialInterface> DisabledMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability|Disabled|Material")
	FName DisabledParameterName = TEXT("bEnabled");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability"))
	FGameplayTag AbilityTag;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	FSlateBrush DefaultIconBrush;

private:
	void RefreshAbilityEnabledVisual();

private:
	uint8 bAbilityUnlocked : 1 = false;
	uint8 bCooldowning : 1 = false;
	uint8 bAbilityEnabled : 1 = true;

	float CoolTime = 0.0f;
	float AccumulatedTime = 0.0f;
};
