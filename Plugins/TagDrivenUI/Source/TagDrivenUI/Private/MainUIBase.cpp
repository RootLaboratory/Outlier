// Fill out your copyright notice in the Description page of Project Settings.
#include "MainUIBase.h"
#include "EventDrivenUI.h"

UEventDrivenUI* UMainUIBase::GetModule(const FGameplayTag& ModuleTag) const
{
	const TObjectPtr<UEventDrivenUI>* FoundModule = Modules.Find(ModuleTag);
	return FoundModule ? FoundModule->Get() : nullptr;
}

void UMainUIBase::RegisterModule(UEventDrivenUI* InModule)
{
	if (!InModule)
	{
		return;
	}

	RegisterModule(InModule->ModuleTag, InModule);
}

void UMainUIBase::RegisterModule(const FGameplayTag& ModuleTag, UEventDrivenUI* InModule)
{
	if (!ModuleTag.IsValid())
	{
		return;
	}

	if (InModule)
	{
		InModule->ModuleTag = ModuleTag;
	}

	Modules.Add(ModuleTag, InModule);
}
