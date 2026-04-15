// Fill out your copyright notice in the Description page of Project Settings.
#include "LocalPlayerUISubSystem.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneCaptureComponent2D.h"



#include "MainUIBase.h"
#include "HPBarUI.h"
#include "PartnerCamUI.h"
#include "AmmoUI.h"

#include "EventDrivenUI.h"


void ULocalPlayerUISubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	//UE_LOG(LogTemp, Warning, TEXT("LocalPlayerUISubSystem Initialized"));
}

void ULocalPlayerUISubSystem::RegisterMainUI(UMainUIBase* InMainUI)
{
	MainUIInstance = InMainUI;
}

void ULocalPlayerUISubSystem::UnregisterMainUI(UMainUIBase* InMainUI)
{
	if (MainUIInstance == InMainUI)
	{
		MainUIInstance = nullptr;
	}
}



void ULocalPlayerUISubSystem::OnRep_HUDActivate(bool bShouldActivate)
{
	if (!MainUIInstance)
	{
		return;

	}


	if (bShouldActivate) 	MainUIInstance->ModuleActivate();
	else MainUIInstance->ModuleDeActivate();

}

void ULocalPlayerUISubSystem::OnRep_HealthChanged(float InHealth, float MaxHealth)
{

	float Ratio = InHealth / MaxHealth;

	if (!MainUIInstance)
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(MainUIInstance->GetModule(EUIModule::HP)))
	{
		//UE_LOG(LogTemp, Warning, TEXT("HP Changed, %f"), Ratio);
		HPBarUI->HealthChanged(Ratio);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShieldChanged( float InCurShield,  float InMaxShield)
{
	float Ratio = InCurShield / InMaxShield;

	if (!MainUIInstance)
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(MainUIInstance->GetModule(EUIModule::HP)))
	{
	//	UE_LOG(LogTemp, Warning, TEXT("HP Changed, %f"), Ratio);
		HPBarUI->ShieldChanged(Ratio);
	}
}

void ULocalPlayerUISubSystem::OnRep_AmmoCountChanged(int32 InAmmoCount)
{

	if (!MainUIInstance)
	{
		return;
	}

	if (UAmmoUI* AmmoUI = Cast<UAmmoUI>(MainUIInstance->GetModule(EUIModule::Ammo)))
	{
		AmmoUI->AmmoCountChanged(InAmmoCount);
	}

}

void ULocalPlayerUISubSystem::OnRep_PartnerCameraToggle()
{
	if (!MainUIInstance)
	{
		return;
	}

	if (UPartnerCamUI* PartnerCamUI = Cast<UPartnerCamUI>(MainUIInstance->GetModule(EUIModule::PartnerCam)))
	{
		PartnerCamUI->TogglePartnerCamera();
	}
}


void ULocalPlayerUISubSystem::PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D)
{
	//UE_LOG(LogTemp, Error, TEXT("Try PartnerCameraBind"));

	if (UPartnerCamUI* PartnerCamUI = Cast<UPartnerCamUI>(MainUIInstance->GetModule(EUIModule::PartnerCam)))
	{
		UTextureRenderTarget2D* RenderTarget = InCaptureComponent2D->TextureTarget;
		if(RenderTarget)
		PartnerCamUI->SetPartnerRenderTarget(RenderTarget);
		else
		{
		//	UE_LOG(LogTemp, Error, TEXT("Cant RT"));

		}
	}
	else
	{
	//	UE_LOG(LogTemp, Error, TEXT("Cant PartnerCamUI"));

	}
}
uint8 ULocalPlayerUISubSystem::IsAiming()
{
	return bPlayerAiming;
}

void ULocalPlayerUISubSystem::SetPlayerAiming(uint8 bInAiming)
{
	bPlayerAiming = bInAiming;
}


