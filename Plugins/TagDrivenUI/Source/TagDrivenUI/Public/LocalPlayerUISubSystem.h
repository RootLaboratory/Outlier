// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "CrossHairBase.h"
#include "MainUIBase.h"
#include "PartnerHPUI.h"
#include "LocalPlayerUISubSystem.generated.h"

class UEventDrivenUI;
class USceneCaptureComponent2D;
class UUserWidget;
enum class EWidgetWeaponType : uint8;


UCLASS()
class TAGDRIVENUI_API ULocalPlayerUISubSystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:
	void RegisterMainUI(UMainUIBase* InMainUI);
	void UnregisterMainUI(UMainUIBase* InMainUI);
	void OnRep_PlayerStateChanged(EUIPlayerState State);

public:
	//Replicated
	void OnRep_HUDActivate(bool bShouldActivate); //Whole Widgets Activation Toggle
	void OnRep_HealthChanged(float InHealth, float MaxHealth);
	void OnRep_PartnerShieldChanged(float InHealth, float MaxHealth);
	void OnRep_ShieldChanged( float InCurShield ,  float InMaxShield);
	void OnRep_AmmoCountChanged(int32 InAmmoCount);
	void OnRep_ShooterConditionRefresh();
public:
	void OnRep_Aiming();
	void OnRep_AimingOff();
	void OnRep_AttackSign(EAttackSign InType);
	void OnRep_ShootCrosshairChanged(float InFireRate);
	void OnRep_ShooterHPStateChanged(const FGameplayTag& InShooterConditionTag);
	void OnRep_ShooterDynamicCrosshairChanged(bool InFlag);
public:

	void PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D);
	void PartnerCameraToggle();
	void PartnerDistanceUpdate(const float Distance);
	void OnCurrentWeaponChanged(EWidgetWeaponType WeaponType);
	void OnCurrentAbilityChanged(const FGameplayTag& AbilityTag);
	bool ApplyCurrentAbilityCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime);
	void OnAbilityUsed(const FGameplayTag& AbilityTag, float CoolTime);
	void OnAbilityDisabledByDistance();
	void OnAbilityEnabledByDistance();
	void BindInteractionWidget(UUserWidget* InteractionWidget);
	void BindInteractionWidget(UUserWidget* InteractionWidget, const FVector2D& WidgetPosition);
	void UnbindInteractionWidget(UUserWidget* InteractionWidget);

private:
	UMainUIBase* GetMainUI() const;
	UEventDrivenUI* GetModule(const FGameplayTag& ModuleTag) const;
	UEventDrivenUI* GetModuleAny(const FGameplayTag& FirstTag, const FGameplayTag& SecondTag) const;

	UPROPERTY()
	TObjectPtr<UMainUIBase> MainUIInstance;

	UPROPERTY()
	TObjectPtr<UUserWidget> InteractionWidgetInstance;
};
