// Fill out your copyright notice in the Description page of Project Settings.


#include "GangTongMainUI.h"
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

	ensureMsgf(PartnerLeftHUD && PartnerRightHUD,
		TEXT("GangTongMainUI requires PartnerLeftHUD and PartnerRightHUD container widgets."));

	ModuleInit();
}

void UGangTongMainUI::ModuleInit()
{
	AbilitySlots.Reset();
	AbilitySections.Reset();
	Modules.Empty();
	Modules.Reserve(10);

	RegisterModule(TagDrivenUITags::Partner::HP(), PartnerLeftHUD);
	RegisterModule(TagDrivenUITags::Partner::Health(), PartnerLeftHUD);
	RegisterModule(TagDrivenUITags::Partner::CrossHair(), CrossHairUI);
	RegisterModule(TagDrivenUITags::Partner::DistanceLimit(), PartnerLeftHUD);

	// 슬롯 등록 시 내부 아이콘도 등록되어 쿨타임과 거리 제한 알림을 받는다.
	if (PartnerRightHUD)
	{
		RegisterAbilitySlot(PartnerRightHUD->GetShieldAbilitySlot(), TagDrivenUITags::Ability::Partner::Shield());
		RegisterAbilitySlot(PartnerRightHUD->GetHackingAbilitySlot(), TagDrivenUITags::Ability::Partner::Hacking());
		RegisterAbilitySlot(PartnerRightHUD->GetScanAbilitySlot(), TagDrivenUITags::Ability::Partner::Scan());
		RegisterAbilitySlot(PartnerRightHUD->GetEMPAbilitySlot(), TagDrivenUITags::Ability::Partner::EMP());
	}

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
