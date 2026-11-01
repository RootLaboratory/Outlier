#include "UI/EnemyHudWidget.h"

#include "AbilitySystemComponent.h"
#include "UI/HackEffectTimerWidget.h"
#include "Enemy/EnemyBase.h"
#include "Engine/LocalPlayer.h"
#include "FPostProcessStructures.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Math/RandomStream.h"
#include "Engine/World.h"

namespace EnemyHudDamage
{
	// 체력이 최대 대비 이만큼 깎일 때마다 모서리 마스크가 하나씩 드러난다.
	constexpr float StageStep = 0.2f;
}

void UEnemyHudWidget::InitializeForEnemy(AEnemyBase* InEnemy)
{
	ReleaseDamageFeedback();
	OwningEnemy = InEnemy;
	BindDamageFeedback();
	InitializeHackTimer();
}

AEnemyBase* UEnemyHudWidget::GetOwningEnemy() const
{
	return OwningEnemy.Get();
}

void UEnemyHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeHackTimer();
}

void UEnemyHudWidget::NativeDestruct()
{
	// 부모 Destruct가 자식보다 먼저 실행된다. 외부 제거 중에도 대기 중인 Controller 복귀를 끝낸다.
	const FSimpleDelegate DisplayFinished = IsShowingHackTimerExpiry()
		? OnHackTimerExpiryDisplayFinished : FSimpleDelegate();
	ReleaseDamageFeedback();
	Super::NativeDestruct();
	DisplayFinished.ExecuteIfBound();
}

void UEnemyHudWidget::InitializeHackTimer()
{
	if (HackEffectTimerWidget)
	{
		HackEffectTimerWidget->InitializeForEnemy(OwningEnemy.Get());
		HackEffectTimerWidget->OnHackTimerExpiryDisplayFinished.BindUObject(
			this, &ThisClass::HandleHackTimerExpiryDisplayFinished);
	}
}

void UEnemyHudWidget::ShowHackTimerExpiry(AEnemyBase* Target)
{
	if (HackEffectTimerWidget)
	{
		HackEffectTimerWidget->ShowHackTimerExpiry(Target);
	}
}

bool UEnemyHudWidget::IsShowingHackTimerExpiry() const
{
	return HackEffectTimerWidget && HackEffectTimerWidget->IsShowingHackTimerExpiry();
}

void UEnemyHudWidget::HandleHackTimerExpiryDisplayFinished()
{
	const FSimpleDelegate DisplayFinished = OnHackTimerExpiryDisplayFinished;
	OnHackTimerExpiryDisplayFinished.Unbind();
	DisplayFinished.ExecuteIfBound();
}

void UEnemyHudWidget::BindDamageFeedback()
{
	static_assert(UE_ARRAY_COUNT(DamageRevealOrder) == DroneDamageMaskCount, "DamageRevealOrder must match EDroneDamageMask");

	AEnemyBase* Enemy = OwningEnemy.Get();
	UAbilitySystemComponent* AbilitySystem = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
	ULocalPlayerPostProcessSubsystem* PPSubsystem = GetPostProcessSubsystem();
	if (!AbilitySystem || !PPSubsystem)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DroneDamage] Bind skipped Hud=%s Enemy=%s AbilitySystem=%d PostProcessSubsystem=%d"),
			*GetNameSafe(GetClass()),
			*GetNameSafe(Enemy),
			AbilitySystem ? 1 : 0,
			PPSubsystem ? 1 : 0);
		return;
	}

	// 같은 적을 다시 빙의해도 같은 모서리가 부서져 있도록 적 고유 ID로 섞는다.
	FRandomStream Stream(static_cast<int32>(Enemy->GetUniqueID()));
	for (int32 Index = 0; Index < DroneDamageMaskCount; ++Index)
	{
		DamageRevealOrder[Index] = static_cast<uint8>(Index);
	}
	for (int32 Index = DroneDamageMaskCount - 1; Index > 0; --Index)
	{
		Swap(DamageRevealOrder[Index], DamageRevealOrder[Stream.RandRange(0, Index)]);
	}

	for (int32 MaskIndex = 0; MaskIndex < DroneDamageMaskCount; ++MaskIndex)
	{
		const EDroneDamageMask MaskSlot = static_cast<EDroneDamageMask>(MaskIndex);
		UTexture2D* MaskTexture = GetDamageMaskTexture(MaskIndex);
		if (!MaskTexture)
		{
			// 비어 있으면 그 모서리는 드러나도 아무것도 안 그린다. Image Brush가 아니라 Class Defaults 값이어야 한다.
			UE_LOG(LogTemp, Warning,
				TEXT("[DroneDamage] %s has no mask texture for slot %d (Class Defaults > Enemy HUD|Texture)"),
				*GetNameSafe(GetClass()),
				MaskIndex);
		}
		PPSubsystem->SetDroneDamageMaskTexture(MaskSlot, MaskTexture);
		PPSubsystem->SetDroneDamageMaskRevealed(MaskSlot, false);
	}

	HealthChangedHandle = AbilitySystem
		->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetHealthAttribute())
		.AddUObject(this, &UEnemyHudWidget::HandleVitalAttributeChanged);
	MaxHealthChangedHandle = AbilitySystem
		->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &UEnemyHudWidget::HandleVitalAttributeChanged);
	BoundAbilitySystem = AbilitySystem;
	RevealedDamageStageCount = 0;
	bDamageFeedbackBound = true;

	// 이미 깎인 적이면 지난 단계까지 바로 부서진 상태로 시작한다.
	RefreshDamageStage();
}

void UEnemyHudWidget::ReleaseDamageFeedback(bool bPreserveHackExpiry)
{
	if (!bPreserveHackExpiry || !IsShowingHackTimerExpiry())
	{
		OnHackTimerExpiryDisplayFinished.Unbind();
		if (HackEffectTimerWidget)
		{
			HackEffectTimerWidget->ReleaseHackTimer();
		}
	}
	if (UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		AbilitySystem
			->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetHealthAttribute())
			.Remove(HealthChangedHandle);
		AbilitySystem
			->GetGameplayAttributeValueChangeDelegate(UOutlierVitalAttributeSet::GetMaxHealthAttribute())
			.Remove(MaxHealthChangedHandle);
	}
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	BoundAbilitySystem.Reset();
	RevealedDamageStageCount = 0;

	if (!bDamageFeedbackBound)
	{
		return;
	}

	bDamageFeedbackBound = false;
	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetPostProcessSubsystem())
	{
		PPSubsystem->ClearDroneDamage();
	}
}

void UEnemyHudWidget::SetDamageFeedbackSuppressed(bool bSuppressed)
{
	if (!bDamageFeedbackBound)
	{
		return;
	}

	if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetPostProcessSubsystem())
	{
		PPSubsystem->SetDroneDamageSuppressed(bSuppressed);
	}
}

void UEnemyHudWidget::HandleVitalAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	RefreshDamageStage();
}

void UEnemyHudWidget::RefreshDamageStage()
{
	const UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get();
	ULocalPlayerPostProcessSubsystem* PPSubsystem = GetPostProcessSubsystem();
	if (!bDamageFeedbackBound || !AbilitySystem || !PPSubsystem)
	{
		return;
	}

	const float MaxHealth = AbilitySystem->GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute());
	if (MaxHealth <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float Health = AbilitySystem->GetNumericAttribute(UOutlierVitalAttributeSet::GetHealthAttribute());
	const float LostRatio = FMath::Clamp(1.0f - Health / MaxHealth, 0.0f, 1.0f);
	// 부동소수 오차로 정확히 20% 지점에서 한 단계 늦게 열리지 않도록 살짝 여유를 둔다.
	const int32 Stage = FMath::Min(
		FMath::FloorToInt(LostRatio / EnemyHudDamage::StageStep + KINDA_SMALL_NUMBER),
		DroneDamageMaskCount);

	// 회복해도 부서진 모서리는 되돌리지 않는다.
	while (RevealedDamageStageCount < Stage)
	{
		PPSubsystem->SetDroneDamageMaskRevealed(
			static_cast<EDroneDamageMask>(DamageRevealOrder[RevealedDamageStageCount]),
			true);
		++RevealedDamageStageCount;
	}
}

ULocalPlayerPostProcessSubsystem* UEnemyHudWidget::GetPostProcessSubsystem() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>() : nullptr;
}

UTexture2D* UEnemyHudWidget::GetDamageMaskTexture(int32 MaskIndex) const
{
	switch (static_cast<EDroneDamageMask>(MaskIndex))
	{
	case EDroneDamageMask::LeftTop:     return LeftTopTexture;
	case EDroneDamageMask::LeftBottom:  return LeftBottomTexture;
	case EDroneDamageMask::RightTop:    return RightTopTexture;
	case EDroneDamageMask::RightBottom: return RightBottomTexture;
	default:                            return nullptr;
	}
}
