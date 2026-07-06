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
class TAGDRIVENUI_API UAbilityIconUI : public UUserWidget
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

	void VisibilityControl(bool InFlag);

public:
	UFUNCTION(BlueprintCallable, Category = "Ability|Cooldown")
	void SetCoolTime(float InCoolTime);

	void SetCoolTimeAt(float InCoolTime, float InStartTime);
	void UpdateCoolTime(float Delta);

	UFUNCTION(BlueprintPure, Category = "Ability|Cooldown")
	bool IsCooldowning() const;

	UFUNCTION(BlueprintCallable, Category = "Ability|Cooldown")
	void CooldownDone();

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AbilityIcon;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AbilityMID;

	// 파라미터: IconTexture, CooldownProgress, LockFactor
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Material")
	TObjectPtr<UMaterialInterface> M_AbilityIconMaster;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability"))
	FGameplayTag AbilityTag;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	FSlateBrush DefaultIconBrush;

private:
	// 마스터 머티리얼이 필요한 상태(쿨타임 or 잠금)면 MID로 교체
	void EnsureMasterMaterial();
	void SyncMaterialState();

	// 쿨타임도 끝나고 잠금도 없으면 기본 텍스처로 복귀
	void TryRestoreDefaultBrush();

private:
	uint8 bAbilityUnlocked : 1 = false;
	uint8 bCooldowning     : 1 = false;
	uint8 bAbilityEnabled  : 1 = true;

	float CoolTime        = 0.0f;
	float CooldownStartTime = 0.0f;
	float AccumulatedTime = 0.0f;
};
