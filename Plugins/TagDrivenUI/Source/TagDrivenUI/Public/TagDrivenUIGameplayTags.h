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
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.HP")));
			return Tag;
		}

		inline FGameplayTag PartnerCam()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.PartnerCam")));
			return Tag;
		}

		inline FGameplayTag Ammo()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.Ammo")));
			return Tag;
		}

		inline FGameplayTag Suit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.Suit")));
			return Tag;
		}

		inline FGameplayTag CurrentAbility()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.CurrentAbility")));
			return Tag;
		}

		inline FGameplayTag CurrentWeapon()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.CurrentWeapon")));
			return Tag;
		}

		inline FGameplayTag AbilityWheel()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.AbilityWheel")));
			return Tag;
		}

		inline FGameplayTag ShieldSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.Skill.Shield")));
			return Tag;
		}

		inline FGameplayTag StimPackSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.Skill.StimPack")));
			return Tag;
		}

		inline FGameplayTag StealthSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.Skill.Stealth")));
			return Tag;
		}

		inline FGameplayTag TeleportSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.Skill.Teleport")));
			return Tag;
		}

		inline FGameplayTag CrossHair()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.CrossHair")));
			return Tag;
		}

		inline FGameplayTag DistanceLimit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Shooter.DistanceLimit")));
			return Tag;
		}
	}

	namespace Partner
	{
		inline FGameplayTag HP()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.HP")));
			return Tag;
		}

		inline FGameplayTag PartnerCam()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.PartnerCam")));
			return Tag;
		}

		inline FGameplayTag Ammo()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.Ammo")));
			return Tag;
		}

		inline FGameplayTag Suit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.Suit")));
			return Tag;
		}

		inline FGameplayTag EMPSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.Skill.EMP")));
			return Tag;
		}

		inline FGameplayTag ShieldSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.Skill.Shield")));
			return Tag;
		}

		inline FGameplayTag HackingSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.Skill.Hacking")));
			return Tag;
		}

		inline FGameplayTag ScanSkill()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.Skill.Scan")));
			return Tag;
		}

		inline FGameplayTag CrossHair()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.CrossHair")));
			return Tag;
		}

		inline FGameplayTag DistanceLimit()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Player.Partner.DistanceLimit")));
			return Tag;
		}
	}

	namespace Ability
	{
		namespace Shooter
		{
			inline FGameplayTag Teleport()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Teleport")));
				return Tag;
			}

			inline FGameplayTag Shield()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Shield")));
				return Tag;
			}

			inline FGameplayTag Stealth()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Stealth")));
				return Tag;
			}

			inline FGameplayTag StimPack()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.StimPack")));
				return Tag;
			}
		}

		namespace Partner
		{
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

			inline FGameplayTag EMP()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.EMP")));
				return Tag;
			}
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
