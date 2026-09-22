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

	namespace Upgrade
	{
		namespace Shooter
		{
			namespace BulletReflection
			{
				inline FGameplayTag DurationShieldRegen()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.BulletReflection.DurationShieldRegen")));
					return Tag;
				}
			}

			namespace QuantumLeap
			{
				inline FGameplayTag InvincibleAfterLeap()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.QuantumLeap.InvincibleAfterLeap")));
					return Tag;
				}

				inline FGameplayTag DoubleCharge()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.QuantumLeap.DoubleCharge")));
					return Tag;
				}
			}

			namespace WeaponOvercharge
			{
				inline FGameplayTag AutoAim()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.WeaponOvercharge.AutoAim")));
					return Tag;
				}
			}

			namespace Stealth
			{
				inline FGameplayTag AssassinsStrike()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.Stealth.AssassinsStrike")));
					return Tag;
				}

				inline FGameplayTag DecoyOnCast()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.Stealth.DecoyOnCast")));
					return Tag;
				}

				inline FGameplayTag ShadowStep()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.Stealth.ShadowStep")));
					return Tag;
				}

				inline FGameplayTag DecoyExplosion()
				{
					static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
						FName(TEXT("Upgrade.Shooter.Stealth.DecoyExplosion")));
					return Tag;
				}
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

		inline FGameplayTag BulletReflecting()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.BulletReflecting")));
			return Tag;
		}

		inline FGameplayTag QuantumLeaping()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.QuantumLeaping")));
			return Tag;
		}

		inline FGameplayTag WeaponOvercharged()
		{
			static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("State.WeaponOvercharged")));
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

	// GameplayCue 태그 — 연출 전용이다.
	// 서버가 태그를 쏘면 각 클라이언트가 /Game/GameplayCues 에서 매칭되는 Notify 를 찾아
	// 자기 로컬에서 실행한다 ( 데디케이티드 서버는 실행하지 않는다 ).
	// 따라서 여기에 게임플레이 상태 변경을 절대 싣지 않는다 — 그림과 소리만.
	//
	// 매칭은 계층적이다. GameplayCue.Drone.Death.Gun 용 Notify 를 만들지 않으면
	// 부모인 GameplayCue.Drone.Death 가 대신 처리한다 ( Config/Tags/CueTags.ini 참고 ).
	namespace Cue
	{
		namespace Drone
		{
			inline FGameplayTag Hit()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("GameplayCue.Drone.Hit")));
				return Tag;
			}

			inline FGameplayTag Death()
			{
				static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("GameplayCue.Drone.Death")));
				return Tag;
			}
		}
	}
}
