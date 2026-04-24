// Fill out your copyright notice in the Description page of Project Settings.
#include "LocalPlayerUISubSystem.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneCaptureComponent2D.h"
#include "MainUIBase.h"
#include "HPBarUI.h"
#include "PartnerCamUI.h"
#include "AmmoUI.h"
#include "DynamicCrossHair.h"
#include "EventDrivenUI.h"


void ULocalPlayerUISubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	//UE_LOG(LogTemp, Warning, TEXT("LocalPlayerUISubSystem Initialized"));
}

void ULocalPlayerUISubSystem::Deinitialize()
{
	Super::Deinitialize();
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

	if (bShouldActivate)
	{
		MainUIInstance->ModuleActivate();
	}
	else
	{
		MainUIInstance->ModuleDeActivate();
	}

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

void ULocalPlayerUISubSystem::OnRep_PlayerStateChanged(EUIPlayerState State)
{

	if (UDynamicCrossHair* CrossHairUI = Cast<UDynamicCrossHair>(MainUIInstance->GetModule(EUIModule::CrossHair)))
	{
		CrossHairUI->SetPlayerState(State);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UNVALID CLASS NOT DynamicCrossHairClass"));
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

void ULocalPlayerUISubSystem::OnRep_Aiming()
{
	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(MainUIInstance->GetModule(EUIModule::CrossHair)))
	{
		CrossHairBase->OnAiming();
	}
	
}

void ULocalPlayerUISubSystem::OnRep_AimingOff()
{
	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(MainUIInstance->GetModule(EUIModule::CrossHair)))
	{
		CrossHairBase->OnAimingOff();
	}
}


void ULocalPlayerUISubSystem::OnRep_AttackSign(EAttackSign InType)
{

	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(MainUIInstance->GetModule(EUIModule::CrossHair)))
	{
		//UE_LOG(LogTemp, Error, TEXT("CrossHair Instance Class: %s"), *GetNameSafe(CrossHairBase->GetClass()));
		//UE_LOG(LogTemp, Error, TEXT("OnRep_AttackSign %d"), (uint8)InType);
		CrossHairBase->SpawnAttckSign(InType);
	}
}

//차후 수정 예정; 
void ULocalPlayerUISubSystem::OnRep_ShootCrosshairChanged()
{
	if (UDynamicCrossHair* CrossHairBase = Cast<UDynamicCrossHair>(MainUIInstance->GetModule(EUIModule::CrossHair)))
	{
		UE_LOG(LogTemp, Log, TEXT("OnRep_ShootCrosshairChanged"));
		CrossHairBase->On_RepShoot();
	}
}


void ULocalPlayerUISubSystem::PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D)
{
	//UE_LOG(LogTemp, Error, TEXT("Try PartnerCameraBind"));

	if (UPartnerCamUI* PartnerCamUI = Cast<UPartnerCamUI>(MainUIInstance->GetModule(EUIModule::PartnerCam)))
	{
		UTextureRenderTarget2D* RenderTarget = InCaptureComponent2D->TextureTarget;
		if (RenderTarget)
		{
			PartnerCamUI->SetPartnerRenderTarget(RenderTarget);
		}
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


