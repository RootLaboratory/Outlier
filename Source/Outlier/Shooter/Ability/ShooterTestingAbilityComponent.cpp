#include "Shooter/Ability/ShooterTestingAbilityComponent.h"

#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
	FGameplayTag StealthLogUpgradeTag()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Upgrade.Shooter.Testing.StealthLogUpgrade")));
		return Tag;
	}

	FOutlierAbilityRow MakeShooterAbilityRow(
		FGameplayTag AbilityTag,
		const TCHAR* CooldownTagName,
		const TCHAR* InputTagName,
		float CooldownSeconds)
	{
		FOutlierAbilityRow AbilityRow;
		AbilityRow.AbilityTag = AbilityTag;
		AbilityRow.CooldownTag = FGameplayTag::RequestGameplayTag(FName(CooldownTagName));
		AbilityRow.InputTag = FGameplayTag::RequestGameplayTag(FName(InputTagName));
		AbilityRow.CooldownSeconds = CooldownSeconds;
		AbilityRow.RangeCm = 0.0f;
		AbilityRow.bDefaultLocked = false;
		return AbilityRow;
	}
}

UShooterTestingAbilityComponent::UShooterTestingAbilityComponent()
{
}

void UShooterTestingAbilityComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshCachedShooterAbilityData();
}

void UShooterTestingAbilityComponent::RefreshCachedShooterAbilityData()
{
	CacheShooterAbilityData(DefaultStealthCooldownSeconds);
}

void UShooterTestingAbilityComponent::InitializeAbilityHandlers()
{
	RegisterAbilityHandler(
		OutlierGameplayTags::Ability::Shooter::Stealth(),
		FOutlierAbilityExecuteDelegate::CreateUObject(this, &UShooterTestingAbilityComponent::ExecuteStealth)
	);
}

EOutlierAbilityResult UShooterTestingAbilityComponent::ExecuteStealth(const FOutlierAbilityRow&)
{
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwner());
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return EOutlierAbilityResult::ExecutionFailed;
	}

	if (HasRuntimeTag(StealthLogUpgradeTag()))
	{
		UE_LOG(LogTemp, Error, TEXT("Upgrade Log"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Default Testing Log"));
	}

	ShooterCharacter->ToggleStealth();
	return EOutlierAbilityResult::RequestSent;
}

void UShooterTestingAbilityComponent::CacheShooterAbilityData(float StealthCooldownSeconds)
{
	CacheAbilityRow(MakeShooterAbilityRow(
		OutlierGameplayTags::Ability::Shooter::Stealth(),
		TEXT("Cooldown.Shooter.Stealth"),
		TEXT("Input.Shooter.Stealth"),
		StealthCooldownSeconds
	));
}
