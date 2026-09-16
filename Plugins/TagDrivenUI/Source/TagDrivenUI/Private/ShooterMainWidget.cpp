// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterMainWidget.h"

#include "AmmoUI.h"
#include "CrossHairBase.h"
#include "EventDrivenUI.h"
#include "HPBarUI.h"
#include "ShooterCurrentAbilityIcon.h"
#include "ShooterCurrentWeaponIcon.h"
#include "TagDrivenUIGameplayTags.h"

void UShooterMainWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ModuleInit();
}

void UShooterMainWidget::ModuleInit()
{
	//UE_LOG(LogTemp, Error, TEXT("ModuleInit"));

	Modules.Empty();
	Modules.Reserve(6);

	RegisterModule(TagDrivenUITags::Shooter::HP(), HPBarUI);
	RegisterModule(TagDrivenUITags::Shooter::Ammo(), AmmoUI);
	RegisterModule(TagDrivenUITags::Shooter::CrossHair(), nullptr);
	RegisterModule(TagDrivenUITags::Shooter::CurrentAbility(), CurrentAbilityUI);
	RegisterModule(TagDrivenUITags::Shooter::CurrentWeapon(), CurrentWeaponUI);
	
	//ModuleActivate();

	SuitOnModuleInit();
}

void UShooterMainWidget::ModuleDestruct()
{
}

void UShooterMainWidget::ModuleActivate()
{
	for (auto& [Type, UIModule] : Modules)
	{
		if (UIModule)
		{
			UIModule->Activate();
		}
	}
}

void UShooterMainWidget::ModuleDeActivate()
{
	for (auto& [Type, UIModule] : Modules)
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

void UShooterMainWidget::SuitOnModuleInit()
{
	if (RifleCrossHairUI)
	{
		RifleCrossHairUI->Deactivate();
	}

	if (PistolCrossHairUI)
	{
		PistolCrossHairUI->Deactivate();
	}

	if (AmmoUI)
	{
		AmmoUI->Deactivate();
	}

	ActivateModuleIfAllowed(CurrentAbilityUI);

	ActivateModuleIfAllowed(CurrentWeaponUI);

	if (UEventDrivenUI* HPModule = GetModule(TagDrivenUITags::Shooter::HP()))
	{
		//_LOG(LogTemp, Error, TEXT("SuitOnModuleInit HPModule"));
		ActivateModuleIfAllowed(HPModule);
	}
	else
	{
		//_LOG(LogTemp, Error, TEXT(" HPModule"));

	}

}

void UShooterMainWidget::ResetCrossHairs()
{
	UCrossHairBase* CrossHairs[] = { RifleCrossHairUI, PistolCrossHairUI };
	for (UCrossHairBase* CrossHair : CrossHairs)
	{
		if (CrossHair)
		{
			CrossHair->ResetCrossHairState();
		}
	}
}

void UShooterMainWidget::OnChangeWeapon(EWidgetWeaponType Type)
{
	if (UShooterCurrentWeaponIcon* CurrentWeaponModule = Cast<UShooterCurrentWeaponIcon>(GetModule(TagDrivenUITags::Shooter::CurrentWeapon())))
	{
		CurrentWeaponModule->SetCurrentWeapon(Type);
	}

	switch (Type)
	{
	case EWidgetWeaponType::Melee:
	{
		//UE_LOG(LogTemp, Error, TEXT("Melee"));

		if (AmmoUI)
		{
			AmmoUI->Deactivate();
		}

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = nullptr;
		RegisterModule(TagDrivenUITags::Shooter::CrossHair(), CurrentCrossHairUI);
		break;
	}
	case EWidgetWeaponType::Pistol:
	{
		//UE_LOG(LogTemp, Error, TEXT("Pistol"));

		ActivateModuleIfAllowed(AmmoUI);

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = PistolCrossHairUI;

		ActivateModuleIfAllowed(CurrentCrossHairUI);

		RegisterModule(TagDrivenUITags::Shooter::CrossHair(), CurrentCrossHairUI);
		break;
	}
	case EWidgetWeaponType::Rifle:
	{
		//UE_LOG(LogTemp, Error, TEXT("Rifle"));

		ActivateModuleIfAllowed(AmmoUI);

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = RifleCrossHairUI;

		ActivateModuleIfAllowed(CurrentCrossHairUI);

		RegisterModule(TagDrivenUITags::Shooter::CrossHair(), CurrentCrossHairUI);
		break;
	}
	case EWidgetWeaponType::Unarmed:
	default:
	{
		if (AmmoUI)
		{
			AmmoUI->Deactivate();
		}

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = nullptr;
		RegisterModule(TagDrivenUITags::Shooter::CrossHair(), CurrentCrossHairUI);
		break;
	}
	}
}
