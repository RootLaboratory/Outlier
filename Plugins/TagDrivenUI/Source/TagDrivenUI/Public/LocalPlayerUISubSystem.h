// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "CrossHairBase.h"
#include "MainUIBase.h"
#include "LocalPlayerUISubSystem.generated.h"

class UEventDrivenUI;
class USceneCaptureComponent2D;


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
	void OnRep_ShieldChanged( float InCurShield ,  float InMaxShield);
	void OnRep_AmmoCountChanged(int32 InAmmoCount);
	
public:
	void OnRep_Aiming();
	void OnRep_AimingOff();
	void OnRep_AttackSign(EAttackSign InType);
	void OnRep_ShootCrosshairChanged();
public:

	void PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D);
	void OnRep_PartnerCameraToggle();

private:
	UPROPERTY()
	TObjectPtr<UMainUIBase> MainUIInstance; //PlayerController에게 책임 전가; Pawn 타입 받아서; (Bind된 BP 타입 반환시켜서 Bind)

};

