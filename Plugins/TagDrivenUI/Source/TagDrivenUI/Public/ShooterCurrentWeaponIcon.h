// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "ShooterMainWidget.h"
#include "ShooterCurrentWeaponIcon.generated.h"

class UImage;
class UTexture2D;

UCLASS()
class TAGDRIVENUI_API UShooterCurrentWeaponIcon : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetCurrentWeapon(EWidgetWeaponType WeaponType);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EWidgetWeaponType GetCurrentWeaponType() const { return CurrentWeaponType; }

private:
	UTexture2D* GetTextureForWeapon(EWidgetWeaponType WeaponType) const;

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> CurrentWeaponImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> UnarmedTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> PistolTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> RifleTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> MeleeTexture;

private:
	EWidgetWeaponType CurrentWeaponType = EWidgetWeaponType::Unarmed;
};
