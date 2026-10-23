// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "ShooterStatusBarWidget.generated.h"

class UAmmoUI;
class UTexture2D;
class UAbilitySlotUI;
class UShooterCurrentWeaponIcon;

USTRUCT(BlueprintType)
struct TAGDRIVENUI_API FShooterAbilityIconLayout
{
	GENERATED_BODY()

	// 고정된 아이콘 영역 안에서 표시 배율만 바꾼다. 기존 크기가 80×120이면 0.8×1.2로 설정한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.01"))
	FVector2D RenderScale = FVector2D::UnitVector;

	// Designer에서 배치한 위치를 기준으로 한 UMG 단위의 이동량이다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D PositionOffset = FVector2D::ZeroVector;
};

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
	virtual void NativePreConstruct() override;
	virtual void SynchronizeProperties() override;

	// 선택 스킬이 바뀌는 Shooter 상태바만 Tag별 아이콘을 관리한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Appearance", meta = (Categories = "Ability.Shooter"))
	TMap<FGameplayTag, TObjectPtr<UTexture2D>> AbilityIconTextures;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Appearance", meta = (Categories = "Ability.Shooter"))
	TMap<FGameplayTag, FShooterAbilityIconLayout> AbilityIconLayouts;

	UFUNCTION(BlueprintPure, Category = "Ability|Appearance")
	UTexture2D* GetAbilityIconTexture(const FGameplayTag& AbilityTag) const;

	UFUNCTION(BlueprintCallable, Category = "Ability|Appearance")
	void SetCurrentAbilityPresentation(const FGameplayTag& AbilityTag);

	UAmmoUI* GetAmmoUI() const { return AmmoUI; }
	UAbilitySlotUI* GetCurrentAbilityUI() const { return CurrentAbilityUI; }
	UShooterCurrentWeaponIcon* GetCurrentWeaponUI() const { return CurrentWeaponUI; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAmmoUI> AmmoUI;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilitySlotUI> CurrentAbilityUI;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UShooterCurrentWeaponIcon> CurrentWeaponUI;

private:
	void RefreshCurrentAbilityPresentation();
};
