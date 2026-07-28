#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace OutlierGameplayTags
{
	namespace Ability
	{
		namespace Partner
		{
			inline FGameplayTag EMP()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.EMP")));
				return Tag;
			}

			inline FGameplayTag Shield()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.Shield")));
				return Tag;
			}

			inline FGameplayTag Hacking()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.Hacking")));
				return Tag;
			}

			inline FGameplayTag Scan()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.Scan")));
				return Tag;
			}
		}
	}

	namespace State
	{
		inline FGameplayTag Dead()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Dead")));
			return Tag;
		}

		inline FGameplayTag Immune()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Immune")));
			return Tag;
		}

		inline FGameplayTag Locked()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Locked")));
			return Tag;
		}

		inline FGameplayTag Disabled()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Disabled")));
			return Tag;
		}

		inline FGameplayTag Stunned()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Stunned")));
			return Tag;
		}

		inline FGameplayTag HackedOnce()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.HackedOnce")));
			return Tag;
		}

		inline FGameplayTag Stealthed()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Stealthed")));
			return Tag;
		}

		inline FGameplayTag PossessPending()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.PossessPending")));
			return Tag;
		}
	}
}
