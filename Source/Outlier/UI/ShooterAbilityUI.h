// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "Shooter/Ability/ShooterAbility.h"
#include "Components/Widget.h"
#include "ShooterAbilityUI.generated.h"

/**
 * 
 */
class UShooterAbilitySectionUI;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UBorder;
class UImage;


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

	bool TryGetHoveredAbility(EShooterAbility& OutAbility);
	void TryHovering();
	float CalculateCoordinate();

public:

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> CenterCircle; 

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> BigCircle;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterAbilitySectionUI> IconTeleport; // Teleport

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterAbilitySectionUI> IconStealth; // Stealth

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterAbilitySectionUI> IconStimpack; // Stimpack

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterAbilitySectionUI> IconShield; // Shield

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability UI|Material")
	TObjectPtr<UMaterialInterface> M_ShooterAbilityUI;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShooterAbilityMID;


	//UPROPERTY(BlueprintReadOnly, Category = "Shooter Ability Sections")
	TArray < TObjectPtr<UShooterAbilitySectionUI>> AbilitySections;
	
};
