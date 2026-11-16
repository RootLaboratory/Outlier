#include "GAS/OutlierAbilitySystemComponent.h"

#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "TimerManager.h"

bool UOutlierAbilitySystemComponent::IsDeveloperSkillCooldown(const FGameplayTag& Tag) const
{
	return Tag == OutlierGameplayTags::Cooldown::Shooter::QuantumLeap()
		|| Tag == OutlierGameplayTags::Cooldown::Shooter::QuantumLeapRetry()
		|| Tag == OutlierGameplayTags::Cooldown::Shooter::BulletReflection()
		|| Tag == OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge()
		|| Tag == OutlierGameplayTags::Cooldown::Shooter::Stealth()
		|| Tag == OutlierGameplayTags::Cooldown::Partner::EMP()
		|| Tag == OutlierGameplayTags::Cooldown::Partner::Shield()
		|| Tag == OutlierGameplayTags::Cooldown::Partner::Scan()
		|| Tag == OutlierGameplayTags::Cooldown::Partner::Hacking();
}

float UOutlierAbilitySystemComponent::ResolveDeveloperCooldownDuration(const FGameplayTag& Tag, float NormalDuration) const
{
	const FOutlierSkillCooldownCycle* Cycle = SkillCooldownCycles.Find(Tag);
	// 중단했던 회차만 남은 시간을 승계한다. 빙의 복귀 후 처음 시작하는 충전에는 현재 옵션을 적용한다.
	const bool bResumingExistingCycle = bResumingSkillCooldowns && Cycle && Cycle->bSuspended;
	if (bResumingExistingCycle || !bDeveloperFastCooldown || !IsDeveloperSkillCooldown(Tag))
	{
		return NormalDuration;
	}
	return Tag == OutlierGameplayTags::Cooldown::Shooter::QuantumLeapRetry()
		? FMath::Min(NormalDuration, 1.0f) : 1.0f;
}

void UOutlierAbilitySystemComponent::TrackSkillCooldown(
	const FGameplayTag& Tag, FActiveGameplayEffectHandle Handle, float NormalDuration)
{
	if (!Handle.IsValid())
	{
		return;
	}
	const FOutlierSkillCooldownCycle* Previous = SkillCooldownCycles.Find(Tag);
	FOutlierSkillCooldownCycle Cycle;
	if (bResumingSkillCooldowns && Previous && Previous->bSuspended)
	{
		// 빙의 복귀의 재부여는 새 사용이 아니다. 정상 지속시간과 원래 회차를 승계한다.
		Cycle = *Previous;
	}
	else
	{
		Cycle.NormalDuration = NormalDuration;
		Cycle.StartedAt = GetWorld()->GetTimeSeconds();
	}
	Cycle.Handle = Handle;
	Cycle.bSuspended = false;
	SkillCooldownCycles.Add(Tag, Cycle);
	if (FOnActiveGameplayEffectRemoved_Info* Removed = OnGameplayEffectRemoved_InfoDelegate(Handle))
	{
		Removed->AddUObject(this, &ThisClass::HandleSkillCooldownRemoved, Tag, Handle);
	}
}

void UOutlierAbilitySystemComponent::HandleSkillCooldownRemoved(
	const FGameplayEffectRemovalInfo& Info, FGameplayTag Tag, FActiveGameplayEffectHandle Handle)
{
	FOutlierSkillCooldownCycle* Cycle = SkillCooldownCycles.Find(Tag);
	if (!Cycle || Cycle->Handle != Handle)
	{
		return;
	}
	if (Cycle->bSuspended)
	{
		Cycle->Handle.Invalidate();
	}
	else
	{
		// 완료/취소된 회차는 OFF 시 되살리지 않는다. 충전 완료 콜백의 다음 회차와도 분리한다.
		SkillCooldownCycles.Remove(Tag);
	}
}

void UOutlierAbilitySystemComponent::SetSkillCooldownRemaining(FActiveGameplayEffectHandle Handle, float Remaining)
{
	FActiveGameplayEffect* Effect = ActiveGameplayEffects.GetActiveGameplayEffect(Handle);
	if (!Effect)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Remaining <= 0.0f)
	{
		// 정상 시간이 이미 지났다면 자연 만료로 처리한다. 직접 제거하면 충전이 최대치로 채워진다.
		const float Delta = -Effect->GetTimeRemaining(Now) - KINDA_SMALL_NUMBER;
		Effect->StartWorldTime += Delta;
		Effect->StartServerWorldTime += Delta;
		CheckDurationExpired(Handle);
		return;
	}
	// GE/콜백 정체성을 유지하고 만료 타이머·시간 알림·복제를 기존 GAS 경로로 함께 변경한다.
	// 정상 시작점은 SkillCooldownCycles에 남기므로 표시용 GE 시각 이동이 복원 기준을 바꾸지 않는다.
	ModifyActiveEffectStartTime(Handle, Remaining - Effect->GetTimeRemaining(Now));
}

void UOutlierAbilitySystemComponent::OnGameplayEffectDurationChange(FActiveGameplayEffect& ActiveEffect)
{
	Super::OnGameplayEffectDurationChange(ActiveEffect);
	if (!ActiveEffect.Spec.Def)
	{
		return;
	}
	for (const FGameplayTag& Tag : ActiveEffect.Spec.Def->GetGrantedTags())
	{
		if (IsDeveloperSkillCooldown(Tag))
		{
			OnSkillCooldownTimeChanged.Broadcast(Tag);
		}
	}
}

void UOutlierAbilitySystemComponent::RefreshSkillChargeTiming(const FGameplayTag& Tag, float Remaining)
{
	if (Tag == OutlierGameplayTags::Cooldown::Shooter::QuantumLeap() && QuantumLeapRechargeHandle.IsValid())
	{
		QuantumLeapCharges.RechargeStartServerTime = GetWorld()->GetTimeSeconds();
		QuantumLeapCharges.RechargeDuration = Remaining;
		NotifyQuantumLeapChargesChanged(false);
	}
	else if (Tag == OutlierGameplayTags::Cooldown::Partner::Hacking() && PartnerHackRechargeHandle.IsValid())
	{
		PartnerHackCharges.RechargeStartServerTime = GetWorld()->GetTimeSeconds();
		PartnerHackCharges.RechargeDuration = Remaining;
		NotifyPartnerHackChargesChanged(false);
	}
}

void UOutlierAbilitySystemComponent::SetDeveloperConvenienceFastCooldown(bool bEnabled)
{
	if (!IsOwnerActorAuthoritative() || !GetWorld() || bDeveloperFastCooldown == bEnabled)
	{
		return;
	}
	bDeveloperFastCooldown = bEnabled;
	const float Now = GetWorld()->GetTimeSeconds();
	// 적용 전 효과 목록을 고정한다. 자연 만료 콜백이 다음 충전 회차를 추가해도 중복 단축하지 않는다.
	for (const FActiveGameplayEffectHandle Handle : GetActiveEffects(FGameplayEffectQuery()))
	{
		const FActiveGameplayEffect* Effect = GetActiveGameplayEffect(Handle);
		if (!Effect || !Effect->Spec.Def || Effect->Spec.GetContext().GetSourceObject() != GetAvatarActor())
		{
			continue;
		}
		for (const FGameplayTag& Tag : Effect->Spec.Def->GetGrantedTags())
		{
			if (!IsDeveloperSkillCooldown(Tag))
			{
				continue;
			}
			const float CurrentRemaining = Effect->GetTimeRemaining(Now);
			if (CurrentRemaining <= 0.0f)
			{
				break;
			}
			if (!SkillCooldownCycles.Contains(Tag))
			{
				TrackSkillCooldown(Tag, Handle, Effect->GetDuration());
				SkillCooldownCycles.FindChecked(Tag).StartedAt = Effect->StartWorldTime;
			}
			const FOutlierSkillCooldownCycle Cycle = SkillCooldownCycles.FindChecked(Tag);
			const float Remaining = bEnabled ? FMath::Min(CurrentRemaining, 1.0f)
				: FMath::Max(0.0f, Cycle.NormalDuration - (Now - Cycle.StartedAt));
			SetSkillCooldownRemaining(Handle, Remaining);
			if (GetActiveGameplayEffect(Handle))
			{
				RefreshSkillChargeTiming(Tag, Remaining);
			}
			break;
		}
	}
	for (auto& Entry : SkillCooldownCycles)
	{
		FOutlierSkillCooldownCycle& Cycle = Entry.Value;
		if (!Cycle.bSuspended)
		{
			continue;
		}
		Cycle.Remaining = bEnabled ? FMath::Min(Cycle.Remaining, 1.0f)
			: FMath::Max(UE_SMALL_NUMBER, Cycle.NormalDuration - (Cycle.SuspendedAt - Cycle.StartedAt));
		if (Entry.Key == OutlierGameplayTags::Cooldown::Partner::Hacking())
		{
			SuspendedPartnerHackRechargeRemaining = Cycle.Remaining;
		}
		else
		{
			SuspendedPartnerCooldowns.FindOrAdd(Entry.Key) = Cycle.Remaining;
		}
	}
	if (PartnerHackCharges.bRetryBlocked)
	{
		FOutlierSkillCooldownCycle& Cycle = PartnerHackRetryCycle;
		const float CurrentRemaining = Cycle.bSuspended ? SuspendedPartnerHackRetryRemaining
			: GetWorld()->GetTimerManager().GetTimerRemaining(PartnerHackRetryTimer);
		if (CurrentRemaining > 0.0f)
		{
			const float Elapsed = (Cycle.bSuspended ? Cycle.SuspendedAt : Now) - Cycle.StartedAt;
			const float Remaining = bEnabled ? FMath::Min(CurrentRemaining, 1.0f)
				: FMath::Max(0.0f, Cycle.NormalDuration - Elapsed);
			if (Cycle.bSuspended)
			{
				SuspendedPartnerHackRetryRemaining = FMath::Max(UE_SMALL_NUMBER, Remaining);
			}
			else if (Remaining <= 0.0f)
			{
				GetWorld()->GetTimerManager().ClearTimer(PartnerHackRetryTimer);
				FinishPartnerHackRetryDelay();
			}
			else
			{
				PartnerHackCharges.RetryStartServerTime = Now;
				PartnerHackCharges.RetryDuration = Remaining;
				GetWorld()->GetTimerManager().SetTimer(PartnerHackRetryTimer, this,
					&ThisClass::FinishPartnerHackRetryDelay, Remaining, false);
			}
		}
		NotifyPartnerHackChargesChanged(false);
	}
}

void UOutlierAbilitySystemComponent::SuspendDeveloperCooldownCycles()
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (auto& Entry : SkillCooldownCycles)
	{
		FOutlierSkillCooldownCycle& Cycle = Entry.Value;
		const FGameplayTag& Tag = Entry.Key;
		if (Tag != OutlierGameplayTags::Cooldown::Partner::EMP()
			&& Tag != OutlierGameplayTags::Cooldown::Partner::Shield()
			&& Tag != OutlierGameplayTags::Cooldown::Partner::Scan()
			&& Tag != OutlierGameplayTags::Cooldown::Partner::Hacking())
		{
			continue;
		}
		const FActiveGameplayEffect* Effect = GetActiveGameplayEffect(Cycle.Handle);
		if (Effect && Effect->GetTimeRemaining(Now) > 0.0f)
		{
			Cycle.Remaining = Effect->GetTimeRemaining(Now);
			Cycle.SuspendedAt = Now;
			Cycle.bSuspended = true;
		}
	}
	if (PartnerHackCharges.bRetryBlocked)
	{
		PartnerHackRetryCycle.SuspendedAt = Now;
		PartnerHackRetryCycle.bSuspended = true;
	}
}

void UOutlierAbilitySystemComponent::ResumeDeveloperCooldownCycles()
{
	const float Now = GetWorld()->GetTimeSeconds();
	// 중단 구간을 기준 시각에서 제외한다. bSuspended 해제는 실제 GE 재부여의 Track에서 확정한다.
	for (auto& Entry : SkillCooldownCycles)
	{
		FOutlierSkillCooldownCycle& Cycle = Entry.Value;
		if (Cycle.bSuspended)
		{
			Cycle.StartedAt += Now - Cycle.SuspendedAt;
			Cycle.SuspendedAt = Now;
		}
	}
	if (PartnerHackRetryCycle.bSuspended)
	{
		PartnerHackRetryCycle.StartedAt += Now - PartnerHackRetryCycle.SuspendedAt;
		PartnerHackRetryCycle.bSuspended = false;
	}
}
