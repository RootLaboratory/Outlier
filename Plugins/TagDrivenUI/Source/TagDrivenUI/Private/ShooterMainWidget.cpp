// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterMainWidget.h"

#include "AmmoUI.h"
#include "CrossHairBase.h"
#include "DamageFeedBackWidget.h"
#include "EventDrivenUI.h"
#include "HPBarUI.h"
#include "ShooterCurrentAbilityIcon.h"
#include "ShooterCurrentWeaponIcon.h"
#include "ShooterStatusBarWidget.h"
#include "TagDrivenUIGameplayTags.h"

void UShooterMainWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT("[ShooterHUD][Main] NativeConstruct Widget=%s Class=%s"),
		*GetNameSafe(this), *GetNameSafe(GetClass()));

	CacheStatusBarWidgets();
	ModuleInit();
}

void UShooterMainWidget::CacheStatusBarWidgets()
{
	if (!ensureMsgf(ShooterStatusBarHUD,
		TEXT("ShooterMainWidget requires a ShooterStatusBarHUD container widget.")))
	{
		return;
	}

	AmmoUI = ShooterStatusBarHUD->GetAmmoUI();
	CurrentAbilityUI = ShooterStatusBarHUD->GetCurrentAbilityUI();
	CurrentWeaponUI = ShooterStatusBarHUD->GetCurrentWeaponUI();

	UE_LOG(LogTemp, Log,
		TEXT("[ShooterHUD][Binding] Main=%s StatusBar=%s Ammo=%s Ability=%s Weapon=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ShooterStatusBarHUD),
		*GetNameSafe(AmmoUI),
		*GetNameSafe(CurrentAbilityUI),
		*GetNameSafe(CurrentWeaponUI));
}

void UShooterMainWidget::ModuleInit()
{
	//UE_LOG(LogTemp, Error, TEXT("ModuleInit"));

	Modules.Empty();
	Modules.Reserve(7);
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterHUD][Module] Register Ammo=%s Ability=%s Weapon=%s"),
		*GetNameSafe(AmmoUI), *GetNameSafe(CurrentAbilityUI), *GetNameSafe(CurrentWeaponUI));

	RegisterModule(TagDrivenUITags::Shooter::HP(), HPBarUI);
	RegisterModule(TagDrivenUITags::Shooter::Ammo(), AmmoUI);
	RegisterModule(TagDrivenUITags::Shooter::CrossHair(), nullptr);
	RegisterModule(TagDrivenUITags::Shooter::CurrentAbility(), CurrentAbilityUI);
	RegisterModule(TagDrivenUITags::Shooter::CurrentWeapon(), CurrentWeaponUI);
	RegisterModule(TagDrivenUITags::Shooter::DamageFeedback(), DamageFeedbackUI);

	// 전역 HUD 가시성은 ModuleLayer가 담당하고, 각 모듈의 상태는 별도로 초기화한다.
	ModulesControl(false);
	SuitOnModuleInit();
}

void UShooterMainWidget::ModuleDestruct()
{
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

	SetModuleActive(CurrentAbilityUI, true);

	SetModuleActive(CurrentWeaponUI, true);

	if (UEventDrivenUI* HPModule = GetModule(TagDrivenUITags::Shooter::HP()))
	{
		//_LOG(LogTemp, Error, TEXT("SuitOnModuleInit HPModule"));
		SetModuleActive(HPModule, true);
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
	UEventDrivenUI* RegisteredWeaponModule = GetModule(TagDrivenUITags::Shooter::CurrentWeapon());
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterHUD][WeaponEvent] Requested Type=%s RegisteredModule=%s DirectPointer=%s"),
		*StaticEnum<EWidgetWeaponType>()->GetNameStringByValue(static_cast<int64>(Type)),
		*GetNameSafe(RegisteredWeaponModule),
		*GetNameSafe(CurrentWeaponUI));

	if (UShooterCurrentWeaponIcon* CurrentWeaponModule = Cast<UShooterCurrentWeaponIcon>(RegisteredWeaponModule))
	{
		CurrentWeaponModule->SetCurrentWeapon(Type);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ShooterHUD][WeaponEvent] FAILED: registered CurrentWeapon module is missing or wrong type"));
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

		SetModuleActive(AmmoUI, true);

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = PistolCrossHairUI;

		SetModuleActive(CurrentCrossHairUI, true);

		RegisterModule(TagDrivenUITags::Shooter::CrossHair(), CurrentCrossHairUI);
		break;
	}
	case EWidgetWeaponType::Rifle:
	{
		//UE_LOG(LogTemp, Error, TEXT("Rifle"));

		SetModuleActive(AmmoUI, true);

		if (CurrentCrossHairUI)
		{
			CurrentCrossHairUI->Deactivate();
		}

		CurrentCrossHairUI = RifleCrossHairUI;

		SetModuleActive(CurrentCrossHairUI, true);

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
