// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "Shooter/Ability/ShooterAbility.h"
#include "ShooterAbilityUI.generated.h"

/**
 * 
 */
class UShooterAbilitySectionUI;

UCLASS()
class OUTLIER_API UShooterAbilityUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:

	virtual void NativeConstruct() override;

	bool TryGetHoveredAbility(EShooterAbility& OutAbility);

public:

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget)) 
	TObjectPtr< UShooterAbilitySectionUI> Buttom; //Teleport

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr< UShooterAbilitySectionUI> TOP; // Shield

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr< UShooterAbilitySectionUI> Left; // Stealth

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr< UShooterAbilitySectionUI> Right; // Stimpack

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr< UShooterAbilitySectionUI> Circle; // None

	UPROPERTY(BlueprintReadOnly, Category = "Shooter Ability Sections")
	TArray < TObjectPtr<UShooterAbilitySectionUI>> AbilitySections;
	
};


