// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySlotUI.h"

#include "AbilityIconUI.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "TagDrivenUIGameplayTags.h"

void UAbilitySlotUI::SetIconLayout(const FVector2D& RenderScale, const FVector2D& PositionOffset)
{
	if (!IconSizeBox) return;
	USizeBox* TargetSizeBox = IconSizeBox;

	// 첫 적용 시 Designer 설정을 저장한다. 스킬을 바꿔도 위치 보정이 누적되지 않는다.
	if (CachedIconSizeBox.Get() != TargetSizeBox)
	{
		CachedIconSizeBox = TargetSizeBox;
		DefaultIconScale = TargetSizeBox->GetRenderTransform().Scale;
		DefaultIconTranslation = TargetSizeBox->GetRenderTransform().Translation;
	}

	// SizeBox의 배치 크기를 바꾸지 않아 주변 UI와 ScaleBox의 배치 계산이 흔들리지 않는다.
	const FVector2D Scale(FMath::Max(0.01, RenderScale.X), FMath::Max(0.01, RenderScale.Y));
	TargetSizeBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	TargetSizeBox->SetRenderScale(DefaultIconScale * Scale);
	TargetSizeBox->SetRenderTranslation(DefaultIconTranslation + PositionOffset);
}

void UAbilitySlotUI::NativePreConstruct()
{
	Super::NativePreConstruct();
	UpdateSkillCountVisibility();
	ApplyIconTexture();
}

void UAbilitySlotUI::NativeConstruct()
{
	Super::NativeConstruct();

	if (!CurrentAbilityTag.IsValid())
	{
		CurrentAbilityTag = TagDrivenUITags::Ability::Shooter::QuantumLeap();
	}
	// 기존 Shooter 슬롯의 모듈 태그만 보완한다. Partner의 고정 슬롯에는 덮어쓰지 않는다.
	if (!ModuleTag.IsValid() && CurrentAbilityTag.MatchesTag(
		TagDrivenUITags::Ability::Shooter::QuantumLeap().RequestDirectParent()))
	{
		ModuleTag = TagDrivenUITags::Shooter::CurrentAbility();
	}

	SetCurrentAbility(CurrentAbilityTag);
}

void UAbilitySlotUI::SetSkillCount(int32 InSkillCount)
{
	SetAbilitySkillCount(CurrentAbilityTag, InSkillCount);
}

void UAbilitySlotUI::SetAbilitySkillCount(const FGameplayTag& AbilityTag, int32 InSkillCount)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}
	AbilitySkillCounts.Add(AbilityTag, FMath::Clamp(InSkillCount, 0, 2));
	if (AbilityTag == CurrentAbilityTag)
	{
		UpdateSkillCountVisibility();
	}
}

int32 UAbilitySlotUI::GetAbilitySkillCount(const FGameplayTag& AbilityTag) const
{
	if (const FChargeDisplayState* State = ChargeDisplayStates.Find(AbilityTag))
	{
		return State->Maximum;
	}
	const int32* Count = AbilitySkillCounts.Find(AbilityTag);
	return Count ? FMath::Clamp(*Count, 0, 2) : 1;
}

void UAbilitySlotUI::RegisterSkillCountImages(UImage* SingleImage, UImage* LeftImage, UImage* RightImage)
{
	ResetBarFeedback();
	SkillCountSingleImage = SingleImage;
	SkillCountLeftImage = LeftImage;
	SkillCountRightImage = RightImage;
	UpdateSkillCountVisibility();
}

void UAbilitySlotUI::SetAbilityChargeState(const FGameplayTag& AbilityTag, int32 CurrentCharges,
	int32 MaxCharges, float RechargeDuration, float RechargeRemaining, bool bPlayRecoveryFeedback, bool bTemporarilyBlocked)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}
	const FChargeDisplayState* Previous = ChargeDisplayStates.Find(AbilityTag);
	const bool bHadState = Previous != nullptr;
	const int32 PreviousCurrent = Previous ? Previous->Current : 0;
	const int32 PreviousMaximum = Previous ? Previous->Maximum : 0;
	const bool bWasTemporarilyBlocked = Previous && Previous->bTemporarilyBlocked;
	FChargeDisplayState& State = ChargeDisplayStates.FindOrAdd(AbilityTag);
	State.Maximum = FMath::Clamp(MaxCharges, 1, 2);
	State.Current = FMath::Clamp(CurrentCharges, 0, State.Maximum);
	State.Duration = FMath::Max(0.0f, RechargeDuration);
	State.Remaining = FMath::Clamp(RechargeRemaining, 0.0f, State.Duration);
	State.ReceivedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	State.bTemporarilyBlocked = bTemporarilyBlocked;
	if (AbilityTag != CurrentAbilityTag)
	{
		return;
	}
	if (State.Current < PreviousCurrent || State.Maximum != PreviousMaximum || State.bTemporarilyBlocked)
	{
		ResetBarFeedback();
		if (CurrentAbilityIcon)
		{
			CurrentAbilityIcon->ResetChargeFeedback();
		}
	}
	UpdateSkillCountVisibility();
	ApplyCurrentChargeState();
	// 취소 제한이 끝나면 보유 막대를 다시 켜고 펄스를 준다. 실제 충전 완료와 구분해 글로우는 생략한다.
	// 초기 동기화와 최대 횟수 변경에는 연출을 재생하지 않는다.
	const bool bRecoveredCharge = bPlayRecoveryFeedback && State.Current > PreviousCurrent;
	const bool bRetryFinished = bWasTemporarilyBlocked && !State.bTemporarilyBlocked && State.Current > 0;
	if (bHadState && PreviousMaximum == State.Maximum && !State.bTemporarilyBlocked
		&& (bRecoveredCharge || bRetryFinished))
	{
		UImage* RecoveredImage = State.Maximum == 1 ? SkillCountSingleImage.Get()
			: (State.Current == 2 ? SkillCountRightImage.Get() : SkillCountLeftImage.Get());
		PlayBarFeedback(RecoveredImage);
		if (bRecoveredCharge && State.Current == State.Maximum && CurrentAbilityIcon)
		{
			CurrentAbilityIcon->PlayFullChargeFeedback();
		}
	}
}

void UAbilitySlotUI::ApplyCurrentChargeState()
{
	const FChargeDisplayState* State = ChargeDisplayStates.Find(CurrentAbilityTag);
	if (!State || !CurrentAbilityIcon)
	{
		return;
	}
	// 선택하지 않은 동안 흐른 시간도 반영한다. 클라이언트는 시간이 끝나도 충전을 직접 지급하지 않는다.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : State->ReceivedTime;
	const float Remaining = FMath::Max(0.0f, State->Remaining - (Now - State->ReceivedTime));
	// 사용 제한은 보유 막대와 별개다. 전달받은 충전 또는 취소 쿨타임을 게이지로 표시한다.
	CurrentAbilityIcon->SetChargePresentation(State->Current > 0 && !State->bTemporarilyBlocked, State->Duration, Remaining);
}

void UAbilitySlotUI::PlayBarFeedback(UImage* Image)
{
	ResetBarFeedback();
	if (Image)
	{
		PulsingBar = Image;
		PulseBaseScale = Image->GetRenderTransform().Scale;
		PulseBasePivot = Image->GetRenderTransformPivot();
		Image->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		PulseElapsed = 0.0f;
	}
}

void UAbilitySlotUI::ResetBarFeedback()
{
	if (UImage* Image = PulsingBar.Get())
	{
		Image->SetRenderScale(PulseBaseScale);
		Image->SetRenderTransformPivot(PulseBasePivot);
	}
	PulsingBar.Reset();
	PulseElapsed = -1.0f;
}

void UAbilitySlotUI::NativeDestruct()
{
	ResetBarFeedback();
	// 재사용된 위젯의 첫 상태 수신은 회복 이벤트가 아니라 초기 동기화다.
	ChargeDisplayStates.Reset();
	AbilityCooldownStates.Reset();
	if (CurrentAbilityIcon)
	{
		CurrentAbilityIcon->ResetChargeFeedback();
	}
	Super::NativeDestruct();
}

void UAbilitySlotUI::UpdateSkillCountVisibility()
{
	const int32 SkillCount = GetAbilitySkillCount(CurrentAbilityTag);
	if (SkillCount_1)
	{
		SkillCount_1->SetVisibility(SkillCount == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (SkillCount_2)
	{
		SkillCount_2->SetVisibility(SkillCount == 2 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	const FChargeDisplayState* State = ChargeDisplayStates.Find(CurrentAbilityTag);
	const bool* bCooldowning = AbilityCooldownStates.Find(CurrentAbilityTag);
	// 취소는 실제 횟수를 소모하지 않지만, 재사용 제한 동안은 사용할 수 없으므로 막대를 끈다.
	const int32 Current = State ? (State->bTemporarilyBlocked ? 0 : State->Current)
		: (bCooldowning && *bCooldowning ? 0 : SkillCount);
	const auto SetBar = [this](UImage* Image, bool bOn)
	{
		if (Image)
		{
			if (UTexture2D* Texture = bOn ? SkillCountOnTexture.Get() : SkillCountOffTexture.Get())
			{
				Image->SetBrushFromTexture(Texture);
			}
		}
	};
	SetBar(SkillCountSingleImage, Current >= 1);
	// 오른쪽부터 소모하고 왼쪽부터 회복한다. 잔여 1회는 항상 왼쪽 막대에 표시한다.
	SetBar(SkillCountLeftImage, Current >= 1);
	SetBar(SkillCountRightImage, Current >= 2);
}

void UAbilitySlotUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (CurrentAbilityIcon && CurrentAbilityIcon->IsCooldowning())
	{
		CurrentAbilityIcon->UpdateCoolTime(InDeltaTime);
	}
	if (CurrentAbilityIcon)
	{
		CurrentAbilityIcon->UpdateChargeFeedback(InDeltaTime);
	}
	if (UImage* Image = PulsingBar.Get())
	{
		PulseElapsed += InDeltaTime;
		const float Alpha = FMath::Clamp(PulseElapsed / FMath::Max(RecoveryPulseDuration, 0.01f), 0.0f, 1.0f);
		const float Pulse = 1.0f + (RecoveryPulseScale - 1.0f) * (1.0f - FMath::Abs(2.0f * Alpha - 1.0f));
		Image->SetRenderScale(PulseBaseScale * Pulse);
		if (Alpha >= 1.0f)
		{
			ResetBarFeedback();
		}
	}
}

void UAbilitySlotUI::SetCurrentAbility(const FGameplayTag& AbilityTag)
{
	UE_LOG(LogTemp, Verbose, TEXT("[AbilitySlot] SetCurrentAbility Widget=%s Tag=%s"),
		*GetNameSafe(this), *AbilityTag.ToString());

	if (!AbilityTag.IsValid())
	{
		return;
	}

	// 쿨타임 표시 유지 등의 조기 반환 경로에서도 새로 선택한 능력의 횟수는 먼저 반영한다.
	const FGameplayTag PreviousAbilityTag = CurrentAbilityTag;
	CurrentAbilityTag = AbilityTag;
	if (PreviousAbilityTag != AbilityTag)
	{
		ResetBarFeedback();
	}
	UpdateSkillCountVisibility();

	if (!CurrentAbilityIcon)
	{
		return;
	}
	if (ChargeDisplayStates.Contains(AbilityTag))
	{
		CurrentAbilityIcon->SetAbility(AbilityTag);
		ApplyIconTexture();
		CurrentAbilityIcon->AbilityUnLock();
		ApplyCurrentChargeState();
		return;
	}

	if (AbilityTag == PreviousAbilityTag && CurrentAbilityIcon->IsCooldowning())
	{
		CurrentAbilityIcon->SetAbility(AbilityTag);
		ApplyIconTexture();
		return;
	}

	if (AbilityTag != PreviousAbilityTag && CurrentAbilityIcon->IsCooldowning())
	{
		CurrentAbilityIcon->CooldownDone();
	}

	CurrentAbilityIcon->SetAbility(AbilityTag);
	ApplyIconTexture();
	CurrentAbilityIcon->AbilityUnLock();
}

bool UAbilitySlotUI::ApplyCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime)
{
	if (!AbilityTag.IsValid() || CoolTime < 0.0f)
	{
		return false;
	}
	// 충전 상태가 있는 능력은 서버의 주기 정보만 사용한다. 레거시 쿨타임 통지가 이를 덮어쓰지 않는다.
	if (ChargeDisplayStates.Contains(AbilityTag))
	{
		return AbilityTag == CurrentAbilityTag;
	}
	const bool* Previous = AbilityCooldownStates.Find(AbilityTag);
	const bool bRecovered = Previous && *Previous && CoolTime == 0.0f;
	AbilityCooldownStates.Add(AbilityTag, CoolTime > 0.0f);
	if (!CurrentAbilityIcon || AbilityTag != CurrentAbilityTag)
	{
		return false;
	}

	UpdateSkillCountVisibility();
	if (CoolTime == 0.0f)
	{
		CurrentAbilityIcon->CooldownDone();
		// 서버가 진행 중인 쿨타임의 종료를 통보한 경우에만 회복을 강조한다.
		// 최초 동기화, 중복 종료 통보, 선택하지 않은 능력에는 연출을 재생하지 않는다.
		if (bRecovered && GetAbilitySkillCount(AbilityTag) > 0)
		{
			PlayBarFeedback(GetAbilitySkillCount(AbilityTag) == 1
				? SkillCountSingleImage.Get() : SkillCountRightImage.Get());
			CurrentAbilityIcon->PlayFullChargeFeedback();
		}
	}
	else
	{
		ResetBarFeedback();
		CurrentAbilityIcon->ResetChargeFeedback();
		CurrentAbilityIcon->SetCoolTime(CoolTime);
	}
	return true;
}

void UAbilitySlotUI::ResetCooldown()
{
	ChargeDisplayStates.Reset();
	AbilityCooldownStates.Reset();
	ResetBarFeedback();
	if (CurrentAbilityIcon)
	{
		CurrentAbilityIcon->ResetChargePresentation();
	}
	UpdateSkillCountVisibility();
}

void UAbilitySlotUI::SetAbilityEnabled(bool bEnabled)
{
	if (LockIcon)
	{
		LockIcon->SetVisibility(bEnabled ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
	if (CurrentAbilityIcon)
	{
		CurrentAbilityIcon->SetRenderOpacity(bEnabled ? 1.0f : 0.5f);
	}
}

void UAbilitySlotUI::SetIconTexture(UTexture2D* InTexture)
{
	IconTexture = InTexture;
	ApplyIconTexture();
}

void UAbilitySlotUI::ApplyIconTexture()
{
	if (!CurrentAbilityIcon) return;
	if (IconTexture)
	{
		CurrentAbilityIcon->SetIconTexture(IconTexture);
	}
	CurrentAbilityIcon->SetVisibility(IconTexture ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
