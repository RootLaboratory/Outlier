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

	namespace MiniGame
	{
		inline FGameplayTag SpinningCircle()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.MiniGame.SpinningCircle")));
			return Tag;
		}

		inline FGameplayTag ClickCircle()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.MiniGame.ClickCircle")));
			return Tag;
		}
	}

	namespace Time
	{
		inline FGameplayTag Root()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Time")));
			return Tag;
		}

		inline FGameplayTag Limited()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Time.Limited")));
			return Tag;
		}

		inline FGameplayTag Unlimited()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Time.Unlimited")));
			return Tag;
		}
	}

	namespace Use
	{
		inline FGameplayTag Root()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Use")));
			return Tag;
		}

		inline FGameplayTag Multiple()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Use.Multiple")));
			return Tag;
		}

		inline FGameplayTag Once()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Use.Once")));
			return Tag;
		}
	}

	namespace Info
	{
		inline FGameplayTag Root()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Info")));
			return Tag;
		}

		inline FGameplayTag Drone()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Info.Drone")));
			return Tag;
		}

		inline FGameplayTag Jump()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Info.Jump")));
			return Tag;
		}
	}

}
