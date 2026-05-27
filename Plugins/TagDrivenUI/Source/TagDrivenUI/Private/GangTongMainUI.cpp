// Fill out your copyright notice in the Description page of Project Settings.


#include "GangTongMainUI.h"

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
	Modules.Reserve(4);

	RegisterModule(TagDrivenUITags::Partner::HP(), PartnerHPUI);
	RegisterModule(TagDrivenUITags::Partner::PartnerCam(), PartnerCamUI);
	RegisterModule(TagDrivenUITags::Partner::CrossHair(), CrossHairUI);
	RegisterModule(TagDrivenUITags::Partner::DistanceLimit(), DistanceSlide);
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
