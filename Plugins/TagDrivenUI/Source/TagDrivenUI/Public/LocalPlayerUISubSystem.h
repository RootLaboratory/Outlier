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
class UUserWidget;
class AActor;
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
	void OnRep_PartnerHealthChanged(float InHealth, float MaxHealth);
	void OnRep_PartnerShieldChanged(float InHealth, float MaxHealth);
	void OnRep_ShieldChanged( float InCurShield ,  float InMaxShield);
	void OnRep_AmmoCountChanged(int32 InCurrentAmmo, int32 InMaxAmmo);
	void OnDamageFeedback(AActor* DamagedCharacter, const FVector& DamageOrigin);

	// Shooter 의 슈트 획득 상태가 바뀌었을 때 로컬 화면에 반영한다.
	// 지금은 Partner 의 거리 위젯만 이 신호를 쓴다.
	void OnShooterSuitAcquiredChanged(bool bAcquired);

	// 뒤늦게 등록된 모듈에 현재 상태를 물려준다.
	// 값 푸시는 "그 순간 등록돼 있던" 모듈에만 닿으므로, 나중에 붙는 멤버 위젯은
	// Ammo WBP 기본값 그대로 남는다.
	void SyncRegisteredModule(UEventDrivenUI* InModule);
	void OnRep_ShooterConditionRefresh();
public:
	void OnRep_Aiming();
	void OnRep_AimingOff();
	void OnRep_AttackSign(EAttackSign InType);
	void OnRep_ShootCrosshairChanged(float InFireRate, float InElapsedTime = 0.0f);
	void OnRep_ShooterHPStateChanged(const FGameplayTag& InShooterConditionTag);
	void OnRep_ShooterDynamicCrosshairChanged(bool InFlag);
public:

	void PartnerDistanceUpdate(const float Distance);
	void OnCurrentWeaponChanged(EWidgetWeaponType WeaponType);
	void OnCurrentAbilityChanged(const FGameplayTag& AbilityTag);
	void ResetShooterAbilityState(const FGameplayTag& SelectedAbilityTag);
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

	// 모듈이 아직 없을 때 들어온 값도 기억해둔다. SyncRegisteredModule 이 이걸 재생한다.
	int32 CachedAmmoCount = 0;
	int32 CachedMaxAmmo = 0;

	// 슈트 획득 신호는 MainUI 가 생기기 전에 도착할 수 있다(OnRep 은 값이 바뀌는 순간 한 번뿐).
	// 마지막 상태를 들고 있다가 RegisterMainUI 에서 새 위젯에 그대로 물려준다.
	bool bShooterSuitAcquired = false;

	UPROPERTY()
	TObjectPtr<UUserWidget> InteractionWidgetInstance;
};
