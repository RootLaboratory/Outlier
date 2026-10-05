// Fill out your copyright notice in the Description page of Project Settings.


#include "GangTongMainUI.h"
#include "AbilityIconUI.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "EventDrivenUI.h"
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

	InitHudImageOnly();
}

void UGangTongMainUI::InitHudImageOnly()
{
	if (bHudImageInitialized || !Image_Hud)
	{
		return;
	}
	bHudImageInitialized = true;

	// 이미지까지 이어지는 부모만 열고, 같은 부모 아래의 나머지 위젯은 숨긴다.
	// DefaultLayer가 Collapsed인 상태에서도 이미지가 실제로 보이게 한다.
	UWidget* ImageBranch = Image_Hud;
	for (UPanelWidget* Parent = ImageBranch->GetParent(); Parent; Parent = ImageBranch->GetParent())
	{
		InitialHudWidgetVisibilities.Add(Parent, Parent->GetVisibility());
		Parent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		for (int32 Index = 0; Index < Parent->GetChildrenCount(); ++Index)
		{
			UWidget* Child = Parent->GetChildAt(Index);
			if (Child && Child != ImageBranch)
			{
				InitialHudWidgetVisibilities.Add(Child, Child->GetVisibility());
				Child->SetVisibility(ESlateVisibility::Collapsed);
			}
		}

		ImageBranch = Parent;
	}

	Image_Hud->SetVisibility(ESlateVisibility::Visible);
}

void UGangTongMainUI::SetSuitGatedModulesEnabled(bool bEnabled)
{
	if (!InitialHudWidgetVisibilities.IsEmpty())
	{
		// 초기 UI 등록에서 반복되는 미착용 상태는 최초 이미지 표시를 지우지 않는다.
		// 해킹/EMP 등의 전체 숨김은 기존 ModulesControl(false)가 부모까지 숨긴다.
		if (!bEnabled)
		{
			return;
		}

		for (const TPair<TObjectPtr<UWidget>, ESlateVisibility>& Entry : InitialHudWidgetVisibilities)
		{
			if (Entry.Key)
			{
				Entry.Key->SetVisibility(Entry.Value);
			}
		}
		InitialHudWidgetVisibilities.Empty();
	}

	// Shooter 와 같은 흐름 — ModuleLayer만 제어하고 자식의 개별 상태는 보존한다.
	// (AFirstPersonPlayerController::ControlMainWidget 이 Shooter 쪽에서 하는 것과 동일)
	ModulesControl(bEnabled);
}

void UGangTongMainUI::ModuleDestruct()
{
}
