#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace HackGameplayTags
{
	namespace Target
	{
		inline FGameplayTag Possessable()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Target.Possessable")));
			return Tag;
		}

		inline FGameplayTag NonPossessable()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Target.NonPossessable")));
			return Tag;
		}
	}

	namespace Effect
	{
		inline FGameplayTag Root()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect")));
			return Tag;
		}

		inline FGameplayTag Possess()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect.Possess")));
			return Tag;
		}

		inline FGameplayTag Explode()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect.Explode")));
			return Tag;
		}

		inline FGameplayTag Disrupt()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect.Disrupt")));
			return Tag;
		}

		inline FGameplayTag Disable()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect.Disable")));
			return Tag;
		}

		inline FGameplayTag Open()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect.Open")));
			return Tag;
		}

		inline FGameplayTag RevealInfo()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Effect.RevealInfo")));
			return Tag;
		}
	}

	namespace State
	{
		inline FGameplayTag Alive()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.State.Alive")));
			return Tag;
		}

		inline FGameplayTag Dead()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.State.Dead")));
			return Tag;
		}

		inline FGameplayTag HackedOnce()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.State.HackedOnce")));
			return Tag;
		}

		inline FGameplayTag Locked()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.State.Locked")));
			return Tag;
		}

		inline FGameplayTag Immune()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.State.Immune")));
			return Tag;
		}
	}
}
