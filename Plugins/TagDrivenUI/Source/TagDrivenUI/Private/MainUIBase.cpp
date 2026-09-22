// Fill out your copyright notice in the Description page of Project Settings.
#include "MainUIBase.h"
#include "AbilityIconUI.h"
#include "Components/CanvasPanel.h"
#include "Components/Widget.h"
#include "EventDrivenUI.h"
#include "LocalPlayerUISubSystem.h"
#include "Engine/LocalPlayer.h"

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

void UMainUIBase::ResetAbilityCooldowns()
{
	for (const TPair<FGameplayTag, TObjectPtr<UAbilityIconUI>>& AbilitySection : AbilitySections)
	{
		if (UAbilityIconUI* Icon = AbilitySection.Value)
		{
			Icon->CooldownDone();
		}
	}
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
	bModulesActive = Flag;

	UWidget* VisibilityRoot = ModuleLayer ? ModuleLayer.Get() : DefaultLayer.Get();
	if (VisibilityRoot)
	{
		VisibilityRoot->SetVisibility(
			Flag
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed);
		return;
	}

	// Native class나 아직 ModuleLayer를 반영하지 않은 WBP를 위한 안전한 fallback.
	for (const TPair<FGameplayTag, TObjectPtr<UEventDrivenUI>>& Module : Modules)
	{
		if (UEventDrivenUI* Widget = Module.Value)
		{
			if (Flag)
			{
				Widget->Activate();
			}
			else
			{
				Widget->Deactivate();
			}
		}
	}
}

void UMainUIBase::ModuleActivate()
{
	ModulesControl(true);
}

void UMainUIBase::ModuleDeActivate()
{
	ModulesControl(false);
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

void UMainUIBase::SetModuleActive(UEventDrivenUI* InModule, bool bActive)
{
	if (!InModule)
	{
		return;
	}

	if (bActive)
	{
		InModule->Activate();
	}
	else
	{
		InModule->Deactivate();
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

	if (!InModule)
	{
		return;
	}

	// 값 푸시는 "그 순간 등록돼 있던" 모듈에만 닿는다. 나중에 붙는 멤버 위젯은 BP 기본값
	// 그대로 남으므로(UAmmoUI::Temp_AmmoCount 가 40 으로 시작) 마지막 값을 물려준다.
	// 가시성은 여기서 건드리지 않는다 — 전체 게이트를 쓰지 않는 화면(Partner)의
	// BP 기본 가시성을 등록만으로 뒤집게 된다.
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->SyncRegisteredModule(InModule);
		}
	}
}
