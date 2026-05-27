// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace TagDrivenUITags
{
	namespace Shooter
	{
		inline FGameplayTag HP()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.HP")));
			return Tag;
		}

		inline FGameplayTag PartnerCam()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.PartnerCam")));
			return Tag;
		}

		inline FGameplayTag Ammo()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.Ammo")));
			return Tag;
		}

		inline FGameplayTag Suit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.Suit")));
			return Tag;
		}

		inline FGameplayTag FirstSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.Skill.First")));
			return Tag;
		}

		inline FGameplayTag SecondSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.Skill.Second")));
			return Tag;
		}

		inline FGameplayTag ThirdSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.Skill.Third")));
			return Tag;
		}

		inline FGameplayTag CrossHair()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.CrossHair")));
			return Tag;
		}

		inline FGameplayTag DistanceLimit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Shooter.UI.Module.DistanceLimit")));
			return Tag;
		}
	}

	namespace Partner
	{
		inline FGameplayTag HP()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.HP")));
			return Tag;
		}

		inline FGameplayTag PartnerCam()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.PartnerCam")));
			return Tag;
		}

		inline FGameplayTag Ammo()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.Ammo")));
			return Tag;
		}

		inline FGameplayTag Suit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.Suit")));
			return Tag;
		}

		inline FGameplayTag FirstSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.Skill.First")));
			return Tag;
		}

		inline FGameplayTag SecondSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.Skill.Second")));
			return Tag;
		}

		inline FGameplayTag ThirdSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.Skill.Third")));
			return Tag;
		}

		inline FGameplayTag CrossHair()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.CrossHair")));
			return Tag;
		}

		inline FGameplayTag DistanceLimit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Partner.UI.Module.DistanceLimit")));
			return Tag;
		}
	}

	namespace Condition
	{
		namespace Shooter
		{
			inline FGameplayTag Shield()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Condition.Shooter.Shield")));
				return Tag;
			}

			inline FGameplayTag HP()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Condition.Shooter.HP")));
				return Tag;
			}

			inline FGameplayTag PartnerShield()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Condition.Shooter.PartnerShield")));
				return Tag;
			}
		}
	}
}
