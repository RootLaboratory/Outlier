// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterMainWidget.h"

#include "EventDrivenUI.h"
#include "Blueprint/WidgetTree.h"

#include "HPBarUI.h"
#include "AmmoUI.h"
#include "PartnerCamUI.h"
#include "CrossHairBase.h"

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
	}

	if (HPBarUI)
	{
		Module.Add(EUIModule::HP, HPBarUI);
	}

	if (AmmoUI)
	{
		Module.Add(EUIModule::Ammo, AmmoUI);
	}

	
	Module.Add(EUIModule::CrossHair, nullptr); //키 bind만.


	SuitOnModuleInit();
}

void UShooterMainWidget::ModuleDestruct()
{

}

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

void UShooterMainWidget::DefaultModuleInit()
{
	ModuleDeActivate();
}

//스킬은 추후에, 
void UShooterMainWidget::SuitOnModuleInit()
{

	if (RifleCrossHairUI) RifleCrossHairUI->Deactivate();
	if (PistolCrossHairUI) PistolCrossHairUI->Deactivate();
	if (AmmoUI) AmmoUI->Deactivate();



	if(Module[EUIModule::HP])
		Module[EUIModule::HP]->Activate();

	if (Module[EUIModule::PartnerCam])
		Module[EUIModule::PartnerCam]->Activate();

}

void UShooterMainWidget::OnChangeWeapon(EWidgetWeaponType Type)
{
	switch (Type)
	{
	case EWidgetWeaponType::Melee:
	{
		UE_LOG(LogTemp, Error, TEXT("Melee"));

		if (AmmoUI) AmmoUI->Deactivate();

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = nullptr;
		Module[EUIModule::CrossHair] = CurrentCrossHairUI;

		break;
	}
	case EWidgetWeaponType::Pistol:
	{
		UE_LOG(LogTemp, Error, TEXT("Pistol"));

		if (AmmoUI) AmmoUI->Activate();

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = PistolCrossHairUI;

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Activate();
		}

		Module[EUIModule::CrossHair] = CurrentCrossHairUI;
		
		break;
	}
	case EWidgetWeaponType::Rifle:
	{
		UE_LOG(LogTemp, Error, TEXT("Rifle"));

		if (AmmoUI) AmmoUI->Activate();

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = RifleCrossHairUI;


		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Activate();
		}

		Module[EUIModule::CrossHair] = CurrentCrossHairUI;
		break;

	}


	}
	
}

