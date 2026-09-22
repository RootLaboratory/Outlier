// Fill out your copyright notice in the Description page of Project Settings.


#include "GangTongMainUI.h"
#include "AbilityIconUI.h"
#include "EventDrivenUI.h"
#include "PartnerHPUI.h"
#include "PartnerHealthUI.h"
#include "StaticCrossHair.h"
#include "DistanceSlideUI.h"
#include "DamageFeedBackWidget.h"
#include "TagDrivenUIGameplayTags.h"

void UGangTongMainUI::NativeConstruct()
{
	Super::NativeConstruct();

	ModuleInit();
}

void UGangTongMainUI::ModuleInit()
{
	Modules.Empty();
	Modules.Reserve(10);

	RegisterModule(TagDrivenUITags::Partner::HP(), PartnerHPUI);
	RegisterModule(TagDrivenUITags::Partner::Health(), PartnerHealthUI);
	RegisterModule(TagDrivenUITags::Partner::CrossHair(), CrossHairUI);
	RegisterModule(TagDrivenUITags::Partner::DistanceLimit(), DistanceSlide);
	RegisterModule(TagDrivenUITags::Partner::DamageFeedback(), DamageFeedbackUI);

	RegisterAbilityIcon(AbilityShieldIcon,  TagDrivenUITags::Ability::Partner::Shield(),  true);
	RegisterAbilityIcon(AbilityHackingIcon, TagDrivenUITags::Ability::Partner::Hacking(), true);
	RegisterAbilityIcon(AbilityScanIcon,    TagDrivenUITags::Ability::Partner::Scan(),    true);
	RegisterAbilityIcon(AbilityEMPIcon,     TagDrivenUITags::Ability::Partner::EMP(),     true);

	// Partner 모듈은 기본적으로 사용할 수 있는 상태로 준비하고, 전역 표시는 ModuleLayer가 맡는다.
	for (const TPair<FGameplayTag, TObjectPtr<UEventDrivenUI>>& Module : Modules)
	{
		SetModuleActive(Module.Value, true);
	}

	// 슈트 착용이 확인되기 전까지 ModuleLayer 전체를 숨겨둔다.
	SetSuitGatedModulesEnabled(false);

	if (UEventDrivenUI* PartnerCamModule = GetModule(TagDrivenUITags::Partner::PartnerCam()))
	{
		// 모듈은 Activate로 마운트(토글 동작 위해 bHudActive=true 필요).
		// 카메라 피드 자체는 PartnerCamUI::bCameraActive 기본 false라 시작 시 collapse됨.
		// 단 슈트 게이트를 우회하면 안 되므로 게이트를 존중하는 쪽으로 켠다.
		SetModuleActive(PartnerCamModule, true);
	}
}

void UGangTongMainUI::SetSuitGatedModulesEnabled(bool bEnabled)
{
	// Shooter 와 같은 흐름 — ModuleLayer만 제어하고 자식의 개별 상태는 보존한다.
	// (AFirstPersonPlayerController::ControlMainWidget 이 Shooter 쪽에서 하는 것과 동일)
	ModulesControl(bEnabled);
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
