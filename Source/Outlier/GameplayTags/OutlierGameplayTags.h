#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace OutlierGameplayTags
{
	namespace Data
	{
		inline FGameplayTag Health()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.Health")));
			return Tag;
		}

		inline FGameplayTag MaxHealth()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.MaxHealth")));
			return Tag;
		}

		inline FGameplayTag Damage()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.Damage")));
			return Tag;
		}

		inline FGameplayTag ShieldRecovery()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.ShieldRecovery")));
			return Tag;
		}

		inline FGameplayTag PartnerShield()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.PartnerShield")));
			return Tag;
		}

		inline FGameplayTag MaxPartnerShield()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.MaxPartnerShield")));
			return Tag;
		}
	}

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

	namespace Cooldown
	{
		namespace Shooter
		{
			inline FGameplayTag QuantumLeap()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Shooter.QuantumLeap")));
				return Tag;
			}

			inline FGameplayTag BulletReflection()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Shooter.BulletReflection")));
				return Tag;
			}

			inline FGameplayTag Stealth()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Shooter.Stealth")));
				return Tag;
			}

			inline FGameplayTag WeaponOvercharge()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Shooter.WeaponOvercharge")));
				return Tag;
			}
		}

		namespace Weapon
		{
			inline FGameplayTag Reuse()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Weapon.Reuse")));
				return Tag;
			}
		}

		namespace Partner
		{
			inline FGameplayTag EMP()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Partner.EMP")));
				return Tag;
			}

			inline FGameplayTag Shield()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Partner.Shield")));
				return Tag;
			}

			inline FGameplayTag Hacking()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Partner.Hacking")));
				return Tag;
			}

			inline FGameplayTag Scan()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Cooldown.Partner.Scan")));
				return Tag;
			}
		}
	}

	namespace State
	{
		inline FGameplayTag Rebooting()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.Rebooting")));
			return Tag;
		}

		inline FGameplayTag DamageImmune()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.DamageImmune")));
			return Tag;
		}

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

	namespace Effect
	{
		inline FGameplayTag Buff()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Effect.Buff")));
			return Tag;
		}

		inline FGameplayTag Debuff()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Effect.Debuff")));
			return Tag;
		}
	}
}
