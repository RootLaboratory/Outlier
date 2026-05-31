// Fill out your copyright notice in the Description page of Project Settings.


#include "GangTongMainUI.h"
#include "AbilityIconUI.h"
#include "EventDrivenUI.h"
#include "PartnerHPUI.h"
#include "PartnerCamUI.h"
#include "StaticCrossHair.h"
#include "DistanceSlideUI.h"
#include "TagDrivenUIGameplayTags.h"

void UGangTongMainUI::NativeConstruct()
{
	Super::NativeConstruct();

	ModuleInit();
}

void UGangTongMainUI::ModuleInit()
{
	Modules.Empty();
	Modules.Reserve(8);

	RegisterModule(TagDrivenUITags::Partner::HP(), PartnerHPUI);
	RegisterModule(TagDrivenUITags::Partner::PartnerCam(), PartnerCamUI);
	RegisterModule(TagDrivenUITags::Partner::CrossHair(), CrossHairUI);
	RegisterModule(TagDrivenUITags::Partner::DistanceLimit(), DistanceSlide);

	RegisterAbilityIcon(AbilityShieldIcon,  TagDrivenUITags::Ability::Partner::Shield(),  true);
	RegisterAbilityIcon(AbilityHackingIcon, TagDrivenUITags::Ability::Partner::Hacking(), true);
	RegisterAbilityIcon(AbilityScanIcon,    TagDrivenUITags::Ability::Partner::Scan(),    true);
	RegisterAbilityIcon(AbilityEMPIcon,     TagDrivenUITags::Ability::Partner::EMP(),     true);
}

void UGangTongMainUI::On_RepAbilityDisabledByDistance()
{
	if (UAbilityIconUI* ShieldIcon = GetAbilityIcon(TagDrivenUITags::Ability::Partner::Shield()))
	{
		ShieldIcon->SetAbilityEnabled(false);
	}
}

void UGangTongMainUI::On_RepAbilityabledByDistance()
{
	if (UAbilityIconUI* ShieldIcon = GetAbilityIcon(TagDrivenUITags::Ability::Partner::Shield()))
	{
		ShieldIcon->SetAbilityEnabled(true);
	}
}

void UGangTongMainUI::ModuleDestruct()
{
}

void UGangTongMainUI::ModuleActivate()
{
	for (auto& [Type, UIModule] : Modules)
	{
		if (UIModule)
		{
			UIModule->Activate();
		}
	}
}

void UGangTongMainUI::ModuleDeActivate()
{
	for (auto& [Type, UIModule] : Modules)
	{
		if (UIModule)
		{
			UIModule->Deactivate();
		}
	}
}
