#include "UI/EnemyHudWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/TextBlock.h"
#include "Enemy/EnemyBase.h"
#include "Engine/LocalPlayer.h"
#include "FPostProcessStructures.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Math/RandomStream.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "TimerManager.h"

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
	BindHackTimer();
}

AEnemyBase* UEnemyHudWidget::GetOwningEnemy() const
{
	return OwningEnemy.Get();
}

void UEnemyHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindHackTimer();
}

void UEnemyHudWidget::NativeDestruct()
{
	ReleaseDamageFeedback();
	Super::NativeDestruct();
}

int32 UEnemyHudWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Result = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (bHackTimerShowingExpiry)
	{
		bHackTimerExpiryPainted = true;
	}
	return Result;
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
	if (!bPreserveHackExpiry || !bHackTimerShowingExpiry)
	{
		ReleaseHackTimer();
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

void UEnemyHudWidget::BindHackTimer()
{
	AEnemyBase* Enemy = OwningEnemy.Get();
	if (bHackTimerBound)
	{
		return;
	}
	SetHackTimerVisible(false);
	if (!IsValid(Enemy) || (!HackEffectMinutesText && !HackEffectSecondsText))
	{
		return;
	}

	Enemy->OnHackEffectStarted.AddUniqueDynamic(this, &ThisClass::HandleHackEffectStarted);
	Enemy->OnHackEffectEnded.AddUniqueDynamic(this, &ThisClass::HandleHackEffectEnded);
	Enemy->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleHackTargetEndPlay);
	bHackTimerBound = true;
	SetHackTimerTime(FEnemyHackEffectTime());
	SetHackTimerVisible(false);

	// 시작 이벤트보다 HUD 생성이 늦어도 현재 서버 상태부터 이어서 표시한다.
	if (Enemy->IsHackEffectActive())
	{
		HandleHackEffectStarted(Enemy, Enemy->GetHackEffectDuration());
	}
}

void UEnemyHudWidget::ReleaseHackTimer()
{
	if (AEnemyBase* Enemy = OwningEnemy.Get())
	{
		Enemy->OnHackEffectStarted.RemoveDynamic(this, &ThisClass::HandleHackEffectStarted);
		Enemy->OnHackEffectEnded.RemoveDynamic(this, &ThisClass::HandleHackEffectEnded);
		Enemy->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleHackTargetEndPlay);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HackTimerRefreshHandle);
		World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	}
	bHackTimerBound = false;
	bHackTimerShowingExpiry = false;
	bHackTimerTimeInitialized = false;
	bHackTimerExpiryPainted = false;
	OnHackTimerExpiryDisplayFinished.Unbind();
	SetHackTimerVisible(false);
}

void UEnemyHudWidget::HandleHackEffectStarted(AEnemyBase* Target, float Duration)
{
	if (!bHackTimerBound || bHackTimerShowingExpiry || Target != OwningEnemy.Get() || !Target->IsHackEffectActive())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	bHackTimerShowingExpiry = false;
	RefreshHackTimer();
	// 로컬 타이머는 다시 그릴 시점만 알린다. 남은 시간은 서버 시각에서 매번 구한다.
	FTimerManagerTimerParameters TimerParameters;
	TimerParameters.bLoop = true;
	TimerParameters.bMaxOncePerFrame = true;
	World->GetTimerManager().SetTimer(HackTimerRefreshHandle, this,
		&ThisClass::RefreshHackTimer, 0.1f, TimerParameters);
}

void UEnemyHudWidget::RefreshHackTimer()
{
	// 소유 Controller의 종료 통보가 먼저 오면 Enemy 상태가 아직 활성일 수 있다.
	// 늦은 갱신이 로컬에 보관한 00:00을 이전 남은 시간으로 덮어쓰지 않는다.
	if (bHackTimerShowingExpiry)
	{
		return;
	}
	AEnemyBase* Enemy = OwningEnemy.Get();
	if (!bHackTimerBound || !IsValid(Enemy) || !Enemy->IsHackEffectActive())
	{
		ReleaseHackTimer();
		return;
	}
	SetHackTimerTime(Enemy->GetHackEffectRemainingMinutesAndSeconds());
	SetHackTimerVisible(true);
}

void UEnemyHudWidget::HandleHackEffectEnded(AEnemyBase* Target, EEnemyHackEffectEndReason EndReason)
{
	if (!bHackTimerBound || Target != OwningEnemy.Get())
	{
		return;
	}
	// Controller와 Enemy 양쪽에서 같은 만료를 통보할 수 있다. 이미 예약한 숨김은 유지한다.
	if (EndReason == EEnemyHackEffectEndReason::Expired && bHackTimerShowingExpiry)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		ReleaseHackTimer();
		return;
	}
	World->GetTimerManager().ClearTimer(HackTimerRefreshHandle);
	World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	if (EndReason != EEnemyHackEffectEndReason::Expired)
	{
		ReleaseHackTimer();
		return;
	}

	ShowHackTimerExpiry(Target);
}

void UEnemyHudWidget::ShowHackTimerExpiry(AEnemyBase* Target)
{
	if (!Target || Target != OwningEnemy.Get() || !bHackTimerBound || bHackTimerShowingExpiry)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(HackTimerRefreshHandle);
	World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	// 대상 파괴 후에도 마지막 숫자는 위젯이 보관한다. 사망/복귀 처리를 기다리게 하지 않는다.
	SetHackTimerTime(FEnemyHackEffectTime());
	SetHackTimerVisible(true);
	bHackTimerShowingExpiry = true;
	bHackTimerExpiryPainted = false;
	HackTimerZeroFrame = GFrameCounter;
	HackTimerHideHandle = World->GetTimerManager().SetTimerForNextTick(
		this, &ThisClass::HideExpiredHackTimer);
}

void UEnemyHudWidget::HideExpiredHackTimer()
{
	if (!bHackTimerShowingExpiry)
	{
		return;
	}
	// 화면에 붙은 HUD는 Paint를 한 번 거쳐야 제거한다. 프레임 번호만으로는 렌더링을 보장할 수 없다.
	// Slate가 없는 단위 테스트/화면 밖 위젯은 기존 프레임 경계를 사용한다.
	if (GFrameCounter <= HackTimerZeroFrame
		|| (GetCachedWidget().IsValid() && IsVisible() && !bHackTimerExpiryPainted))
	{
		if (UWorld* World = GetWorld())
		{
			HackTimerHideHandle = World->GetTimerManager().SetTimerForNextTick(
				this, &ThisClass::HideExpiredHackTimer);
		}
		return;
	}
	const FSimpleDelegate DisplayFinished = OnHackTimerExpiryDisplayFinished;
	ReleaseHackTimer();
	DisplayFinished.ExecuteIfBound();
}

void UEnemyHudWidget::HandleHackTargetEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	if (Actor == OwningEnemy.Get())
	{
		// 자연 만료의 마지막 숫자만 대상과 분리해 둔다. 위젯 제거 시에는 즉시 정리한다.
		if (bHackTimerShowingExpiry)
		{
			return;
		}
		ReleaseHackTimer();
	}
}

void UEnemyHudWidget::SetHackTimerTime(const FEnemyHackEffectTime& Time)
{
	if (bHackTimerTimeInitialized && HackEffectTime.Minutes == Time.Minutes && HackEffectTime.Seconds == Time.Seconds)
	{
		return;
	}
	HackEffectTime = Time;
	bHackTimerTimeInitialized = true;
	FNumberFormattingOptions Options;
	Options.SetUseGrouping(false);
	Options.SetMinimumIntegralDigits(2);
	if (HackEffectMinutesText)
	{
		HackEffectMinutesText->SetText(FText::AsNumber(Time.Minutes, &Options));
	}
	if (HackEffectSecondsText)
	{
		HackEffectSecondsText->SetText(FText::AsNumber(Time.Seconds, &Options));
	}
}

void UEnemyHudWidget::SetHackTimerVisible(bool bVisible)
{
	const ESlateVisibility TimerVisibility = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (HackEffectMinutesText)
	{
		HackEffectMinutesText->SetVisibility(TimerVisibility);
	}
	if (HackEffectSecondsText)
	{
		HackEffectSecondsText->SetVisibility(TimerVisibility);
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
