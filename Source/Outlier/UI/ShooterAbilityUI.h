// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "GameplayTagContainer.h"
#include "Components/Widget.h"
#include "ShooterAbilityUI.generated.h"

/**
 * 
 */
class UAbilityIconUI;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UBorder;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShooterAbilitySelected, FGameplayTag, AbilityTag);

//ICON
// Image
// 해금조건
// 쿨타임 받는 거


UCLASS()
class OUTLIER_API UShooterAbilityUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:
	bool TryGetHoveredAbility(FGameplayTag& OutAbilityTag, bool bBroadcastSelection = true);
	bool ApplyCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime);
	void ResetCooldowns();
	void TryHovering();
	UAbilityIconUI* GetAbilityIcon(const FGameplayTag& AbilityTag) const;

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> CenterCircle; 

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> BigCircle;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> IconQuantumLeap; // QuantumLeap

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> IconBulletReflection; // BulletReflection

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> IconStealth; // Stealth

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> IconWeaponOvercharge; // WeaponOvercharge

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability UI|Material")
	TObjectPtr<UMaterialInterface> M_ShooterAbilityUI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShooterAbilityMID;

	UPROPERTY(BlueprintReadOnly, Category = "Shooter Ability Sections")
	TMap<FGameplayTag, TObjectPtr<UAbilityIconUI>> AbilitySections;

	UPROPERTY(BlueprintAssignable, Category = "Ability")
	FOnShooterAbilitySelected OnAbilitySelected;
	
private:
	bool TryCalculateCoordinate(float& OutAngleDeg) const;
	void RegisterAbilityIcon(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag, bool bUnlock = false);
	FGameplayTag GetAbilityTagByAngle(float AngleDeg) const;
	bool IsAbilityUnlocked(const FGameplayTag& AbilityTag) const;

	UPROPERTY(Transient)
	FGameplayTag RightAbilityTag;

	UPROPERTY(Transient)
	FGameplayTag BottomAbilityTag;

	UPROPERTY(Transient)
	FGameplayTag LeftAbilityTag;

	UPROPERTY(Transient)
	FGameplayTag TopAbilityTag;
};
