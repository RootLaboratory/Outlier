// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"

#include "MainUIBase.h"

#include "LocalPlayerUISubSystem.generated.h"




class UEventDrivenUI;
class USceneCaptureComponent2D;

UCLASS()
class TAGDRIVENUI_API ULocalPlayerUISubSystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
	
public:
	//UnUsed
	void EventCall(const FGameplayTag& EventTag);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	void RegisterMainUI(UMainUIBase* InMainUI);
	void UnregisterMainUI(UMainUIBase* InMainUI);

public:
	//Replicated
	void  OnRep_HUDActivate(bool bShouldActivate); //Whole Widgets Activation Toggle


	void OnRep_HealthChanged(float InHealth, float MaxHealth);
	void OnRep_ShieldChanged( float InCurShield ,  float InMaxShield);

	void OnRep_AmmoCountChanged(int32 InAmmoCount);
	void OnRep_PartnerCameraChanged(bool InFlag);

	void TempTestingCode();


	void PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D);

private:
	UPROPERTY()
	TObjectPtr<UMainUIBase> MainUIInstance;

};

