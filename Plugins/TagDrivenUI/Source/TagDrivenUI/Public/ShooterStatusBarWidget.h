// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterStatusBarWidget.generated.h"

class UAmmoUI;
class UShooterCurrentAbilityIcon;
class UShooterCurrentWeaponIcon;

/**
 * 오른쪽 HUD가 모듈화된 뒤 내부 WidgetTree의 바인딩을 소유하는 컨테이너.
 * UMG BindWidget은 중첩 UserWidget의 내부까지 탐색하지 않으므로,
 * ShooterMainWidget 대신 이 클래스가 Ammo/Ability/Weapon 위젯을 직접 바인딩한다.
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UShooterStatusBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAmmoUI* GetAmmoUI() const { return AmmoUI; }
	UShooterCurrentAbilityIcon* GetCurrentAbilityUI() const { return CurrentAbilityUI; }
	UShooterCurrentWeaponIcon* GetCurrentWeaponUI() const { return CurrentWeaponUI; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAmmoUI> AmmoUI;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterCurrentAbilityIcon> CurrentAbilityUI;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterCurrentWeaponIcon> CurrentWeaponUI;
};
