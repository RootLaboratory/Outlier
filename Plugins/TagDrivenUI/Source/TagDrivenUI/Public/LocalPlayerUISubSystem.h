// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"

#include "MainUIBase.h"

#include "LocalPlayerUISubSystem.generated.h"




class UEventDrivenUI;
class USceneCaptureComponent2D;


// Local Player에 대응되는 UI 관리 클래스
// Player Controller에 UMainUIBase BP를 Register 하는 형식으로 처리.



UCLASS()
class TAGDRIVENUI_API ULocalPlayerUISubSystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	void RegisterMainUI(UMainUIBase* InMainUI);
	void UnregisterMainUI(UMainUIBase* InMainUI);

public:
	//Replicated
	void  OnRep_HUDActivate(bool bShouldActivate); //Whole Widgets Activation Toggle
	void OnRep_HealthChanged(float InHealth, float MaxHealth);
	void OnRep_ShieldChanged( float InCurShield ,  float InMaxShield);
	void OnRep_AmmoCountChanged(int32 InAmmoCount);
	void OnRep_PartnerCameraToggle();


	void PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D);
public:
	uint8 IsAiming();
	void SetPlayerAiming(uint8 bInAiming);
private:
	UPROPERTY()
	TObjectPtr<UMainUIBase> MainUIInstance; //PlayerController에게 책임 전가; Pawn 타입 받아서; (Bind된 BP 타입 반환시켜서 Bind)


	uint8 bPlayerAiming : 1 = false; 

};

