// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "GameplayTagContainer.h"
#include "ShooterCurrentAbilityIcon.generated.h"

class UAbilityIconUI;
class UImage;
class UTexture2D;

UCLASS()
class TAGDRIVENUI_API UShooterCurrentAbilityIcon : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetCurrentAbility(const FGameplayTag& AbilityTag);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	bool ApplyCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime);

	void ResetCooldown();

	// 드론 거리 이탈/재부팅으로 슈트 사용이 막히면 LockIcon을 띄운다.
	void SetAbilityEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Ability")
	FGameplayTag GetCurrentAbilityTag() const { return CurrentAbilityTag; }

private:
	UTexture2D* GetTextureForAbility(const FGameplayTag& AbilityTag) const;
	void ApplyTextureForAbility(const FGameplayTag& AbilityTag);

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> CurrentAbilityIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> LockIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UTexture2D> QuantumLeapTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UTexture2D> BulletReflectionTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UTexture2D> StealthTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UTexture2D> WeaponOverchargeTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability.Shooter"))
	FGameplayTag CurrentAbilityTag;
};
