#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace OutlierGameplayTags
{
	namespace Damage
	{
		inline FGameplayTag Weapon()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Damage.Weapon")));
			return Tag;
		}

		inline FGameplayTag Explosion()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Damage.Explosion")));
			return Tag;
		}
	}

	namespace Actor
	{
		namespace Role
		{
			inline FGameplayTag Shooter()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Actor.Role.Shooter")));
				return Tag;
			}
		}
	}

	namespace Ability
	{
		namespace Shooter
		{
			inline FGameplayTag QuantumLeap()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.QuantumLeap")));
				return Tag;
			}

			inline FGameplayTag BulletReflection()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.BulletReflection")));
				return Tag;
			}

			inline FGameplayTag Stealth()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Stealth")));
				return Tag;
			}

			inline FGameplayTag WeaponOvercharge()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.WeaponOvercharge")));
				return Tag;
			}

			inline FGameplayTag Combat()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Shooter.Combat")));
				return Tag;
			}
		}

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

		inline FGameplayTag Used()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Used")));
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
