#pragma once

#include "GameplayTagContainer.h"

namespace UILayerTags
{
	inline FGameplayTag Gameplay() //Hack, EMP, Mnigame etc
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Layer.Gameplay")));
		return Tag;
	}

	inline FGameplayTag GameMenu() //Stat, Pause Menu
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Layer.GameMenu")));
		return Tag;
	}

	inline FGameplayTag Modal() // 정말 종료하시겠습니까? 같은 애들
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Layer.Modal")));
		return Tag;
	}

	inline FGameplayTag System() //Loading.
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Layer.System")));
		return Tag;
	}
}
