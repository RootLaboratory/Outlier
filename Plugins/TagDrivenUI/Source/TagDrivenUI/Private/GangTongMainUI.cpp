// Fill out your copyright notice in the Description page of Project Settings.


#include "GangTongMainUI.h"
#include "AbilityIconUI.h"
#include "Components/Image.h"
#include "EventDrivenUI.h"
#include "InfoCanvasBase.h"
#include "StaticCrossHair.h"
#include "PartnerLeftHudWidget.h"
#include "PartnerRightHudWidget.h"
#include "TagDrivenUIGameplayTags.h"

void UGangTongMainUI::NativeConstruct()
{
	Super::NativeConstruct();

	CacheNestedHudWidgets();
	ModuleInit();
}

void UGangTongMainUI::CacheNestedHudWidgets()
{
	if (!ensureMsgf(PartnerLeftHUD && PartnerRightHUD,
		TEXT("GangTongMainUI requires PartnerLeftHUD and PartnerRightHUD container widgets.")))
	{
		return;
	}

	AbilityShieldIcon = PartnerRightHUD->GetAbilityShieldIcon();
	AbilityHackingIcon = PartnerRightHUD->GetAbilityHackingIcon();
	AbilityScanIcon = PartnerRightHUD->GetAbilityScanIcon();
	AbilityEMPIcon = PartnerRightHUD->GetAbilityEMPIcon();
}

void UGangTongMainUI::ModuleInit()
{
	Modules.Empty();
	Modules.Reserve(10);

	RegisterModule(TagDrivenUITags::Partner::HP(), PartnerLeftHUD);
	RegisterModule(TagDrivenUITags::Partner::Health(), PartnerLeftHUD);
	RegisterModule(TagDrivenUITags::Partner::CrossHair(), CrossHairUI);
	RegisterModule(TagDrivenUITags::Partner::DistanceLimit(), PartnerLeftHUD);

	RegisterAbilityIcon(AbilityShieldIcon,  TagDrivenUITags::Ability::Partner::Shield(),  true);
	RegisterAbilityIcon(AbilityHackingIcon, TagDrivenUITags::Ability::Partner::Hacking(), true);
	RegisterAbilityIcon(AbilityScanIcon,    TagDrivenUITags::Ability::Partner::Scan(),    true);
	RegisterAbilityIcon(AbilityEMPIcon,     TagDrivenUITags::Ability::Partner::EMP(),     true);

	// Partner 모듈은 기본적으로 사용할 수 있는 상태로 준비하고, 전역 표시는 부모 Layer가 맡는다.
	for (const TPair<FGameplayTag, TObjectPtr<UEventDrivenUI>>& Module : Modules)
	{
		SetModuleActive(Module.Value, true);
	}

	if (UEventDrivenUI* PartnerCamModule = GetModule(TagDrivenUITags::Partner::PartnerCam()))
	{
		// 모듈은 Activate로 마운트(토글 동작 위해 bHudActive=true 필요).
		// 카메라 피드 자체는 PartnerCamUI::bCameraActive 기본 false라 시작 시 collapse됨.
		// 단 슈트 게이트를 우회하면 안 되므로 게이트를 존중하는 쪽으로 켠다.
		SetModuleActive(PartnerCamModule, true);
	}
	// 슈트 상태가 전달되기 전에는 HUD 이미지와 InfoLayer만 표시한다.
	SetSuitGatedModulesEnabled(false);
}

void UGangTongMainUI::ApplyExceptionVisibility()
{
	ShowExceptionLayer(InfoLayer);
	ShowExceptionLayer(Image_Hud);
}

void UGangTongMainUI::SetSuitGatedModulesEnabled(bool bEnabled)
{
	ModulesControl(bEnabled);
}

void UGangTongMainUI::ModuleDestruct()
{
}
