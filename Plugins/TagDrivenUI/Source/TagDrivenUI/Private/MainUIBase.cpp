// Fill out your copyright notice in the Description page of Project Settings.
#include "MainUIBase.h"
#include "AbilityIconUI.h"
#include "EventDrivenUI.h"

void UMainUIBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	for (const TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>>& AbilitySection : AbilitySections)
	{
		UAbilityIconUI* Icon = AbilitySection.Value;
		if (!Icon || !Icon->IsCooldowning())
		{
			continue;
		}

		Icon->UpdateCoolTime(InDeltaTime);
	}
}

UEventDrivenUI* UMainUIBase::GetModule(const FGameplayTag& ModuleTag) const
{
	const TObjectPtr<UEventDrivenUI>* FoundModule = Modules.Find(ModuleTag);
	return FoundModule ? FoundModule->Get() : nullptr;
}

void UMainUIBase::AddAbilityIconEntry(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag)
{
	if (!Icon || !AbilityTag.IsValid())
	{
		return;
	}

	AbilitySections.Add(AbilityTag, Icon);
}

UAbilityIconUI* UMainUIBase::GetAbilityIcon(const FGameplayTag& AbilityTag) const
{
	const TObjectPtr<UAbilityIconUI>* Found = AbilitySections.Find(AbilityTag);
	return Found ? Found->Get() : nullptr;
}

void UMainUIBase::On_RepAbilityDisabledByDistance()
{
	for (const TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>>& AbilitySection : AbilitySections)
	{
		UAbilityIconUI* Icon = AbilitySection.Value;
		if (!Icon || !Icon->IsUnLock())
		{
			continue;
		}

		Icon->SetAbilityEnabled(false);
	}
}

void UMainUIBase::On_RepAbilityabledByDistance()
{
	for (const TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>>& AbilitySection : AbilitySections)
	{
		UAbilityIconUI* Icon = AbilitySection.Value;
		if (!Icon || !Icon->IsUnLock())
		{
			continue;
		}

		Icon->SetAbilityEnabled(true);
	}
}

void UMainUIBase::ModulesControl(bool Flag)
{
	if (Flag)
	{
		for (TPair<FGameplayTag, TObjectPtr<UEventDrivenUI>> Module : Modules)
		{
			UEventDrivenUI* Widget = Module.Value;
			{
				if (Widget)
				{
					Widget->Activate();
				}
			}
		}
	}
	else
	{
		for (TPair<FGameplayTag, TObjectPtr<UEventDrivenUI>> Module : Modules)
		{
			UEventDrivenUI* Widget = Module.Value;
			{
				if (Widget)
				{
					Widget->Deactivate();
				}
			}
		}
	}
}
//TMap<FGameplayTag, TObjectPtr<UAbilityIconUI>> AbilitySections;

void UMainUIBase::AbilitySectionControl(bool Flag)
{
	if (Flag)
	{
		for (TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>> Module : AbilitySections)
		{
			UAbilityIconUI* Widget = Module.Value;
			{
				if (Widget)
				{
					Widget->VisibilityControl(true);
				}
			}
		}
	}
	else
	{
		for (TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>> Module : AbilitySections)
		{
			UAbilityIconUI* Widget = Module.Value;
			{
				if (Widget)
				{
					Widget->VisibilityControl(false);
				}
			}
		}
	}
}

void UMainUIBase::RegisterAbilityIcon(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag, bool bUnlock)
{
	if (!Icon || !AbilityTag.IsValid())
	{
		return;
	}

	Icon->AbilityTag = AbilityTag;
	AbilitySections.Add(AbilityTag, Icon);

	if (bUnlock)
	{
		Icon->AbilityUnLock();
	}
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
