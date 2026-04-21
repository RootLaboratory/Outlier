// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterMainWidget.h"

#include "EventDrivenUI.h"
#include "Blueprint/WidgetTree.h"

#include "HPBarUI.h"
#include "AmmoUI.h"
#include "PartnerCamUI.h"

void UShooterMainWidget::NativeConstruct()
{
	Super::NativeConstruct();
	//UE_LOG(LogTemp, Warning, TEXT("Shooter NativeConstructed"));

	ModuleInit();
}

// Binding된 오브젝트들 Initalize 하고; Event bind 호출
//
void UShooterMainWidget::ModuleInit()
{
	Module.Empty();
	Module.Reserve((int32)EUIModule::None);

	if (PartnerCamUI)
	{
		Module.Add(EUIModule::PartnerCam, PartnerCamUI);
		//UE_LOG(LogTemp, Warning, TEXT("Added PartnerCam / Num=%d"), Module.Num());
	}

	if (HPBarUI)
	{
		Module.Add(EUIModule::HP, HPBarUI);
		//UE_LOG(LogTemp, Warning, TEXT("Added HP / Num=%d"), Module.Num());
	}

	if (AmmoUI)
	{
		Module.Add(EUIModule::Ammo, AmmoUI);
		//UE_LOG(LogTemp, Warning, TEXT("Added Ammo / Num=%d"), Module.Num());
	}

	ModuleActivate();
}

void UShooterMainWidget::ModuleDestruct()
{

}
//
void UShooterMainWidget::ModuleActivate()
{
		for (auto& [Type, UIModule] : Module)
		{
			if (UIModule)
			{
				UIModule->Activate();
			}
		}
}

void UShooterMainWidget::ModuleDeActivate()
{
	for (auto& [Type, UIModule] : Module)
	{
		if (UIModule)
		{
			UIModule->Deactivate();
		}
	}

}
