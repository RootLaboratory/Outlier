// Fill out your copyright notice in the Description page of Project Settings.
#include "LocalPlayerUISubSystem.h"
#include "GameFramework/PlayerController.h"
#include "AbilityIconUI.h"
#include "MainUIBase.h"
#include "HPBarUI.h"
#include "AmmoUI.h"
#include "GangTongMainUI.h"
#include "DynamicCrossHair.h"
#include "EventDrivenUI.h"
#include "StaticCrossHair.h"
#include "DamageFeedbackLayer.h"
#include "PartnerHPUI.h"
#include "PartnerHealthUI.h"
#include "PartnerLeftHudWidget.h"
#include "ShooterCurrentAbilityIcon.h"
#include "ShooterMainWidget.h"
#include "TagDrivenUIGameplayTags.h"
#include "Engine/World.h"
#include "InfoCanvasBase.h"

UInfoCanvasBase* ULocalPlayerUISubSystem::GetInfoCanvas() const
{
	UMainUIBase* MainUI = GetMainUI();
	return MainUI && IsValid(MainUI->InfoLayer) ? MainUI->InfoLayer.Get() : nullptr;
}

bool ULocalPlayerUISubSystem::DispatchInfoRequest(UInfoCanvasBase* Canvas, const FOutlierPendingInfoRequest& Pending)
{
	if (!Canvas) return false;
	switch (Pending.Operation)
	{
	case EOutlierInfoRequestOperation::Show:
		return Canvas->EnqueueInfo(Pending.Region, Pending.Request);
	case EOutlierInfoRequestOperation::Complete:
		return Canvas->CompleteInfo(Pending.Region, Pending.Request.EventTag);
	case EOutlierInfoRequestOperation::CompleteInput:
		return Canvas->CompleteInputInfo(Pending.Request.EventTag);
	case EOutlierInfoRequestOperation::Overlap:
		return Canvas->NotifyInfoOverlap(Pending.Request.EventTag);
	case EOutlierInfoRequestOperation::CancelInputs:
		Canvas->CancelInputInfos();
		return true;
	default:
		return false;
	}
}

bool ULocalPlayerUISubSystem::SubmitInfoRequest(const FOutlierPendingInfoRequest& Pending)
{
	UWorld* World = GetWorld();
	if (bIsDeinitializing || bClearingEventInfo || !World || World->bIsTearingDown
		|| (Pending.Operation != EOutlierInfoRequestOperation::CancelInputs && !Pending.Request.EventTag.IsValid()))
	{
		return false;
	}
	if (Pending.Operation == EOutlierInfoRequestOperation::Show
		&& (!FMath::IsFinite(Pending.Request.Duration) || Pending.Request.Duration < 0.0f))
	{
		return false;
	}
	if (UInfoCanvasBase* Canvas = GetInfoCanvas(); Canvas && (!bDispatchingInfoRequests
		|| (Pending.Operation != EOutlierInfoRequestOperation::Show
			&& Pending.Operation != EOutlierInfoRequestOperation::CancelInputs)))
	{
		return DispatchInfoRequest(Canvas, Pending);
	}
	if (Pending.Operation == EOutlierInfoRequestOperation::Show)
	{
		for (int32 Index = PendingInfoRequests.Num() - 1; Index >= 0; --Index)
		{
			const FOutlierPendingInfoRequest& Existing = PendingInfoRequests[Index];
			if (Existing.Region != Pending.Region) continue;
			if (Existing.Operation == EOutlierInfoRequestOperation::CancelInputs) break;
			if (Existing.Operation == EOutlierInfoRequestOperation::Show
				&& Existing.Request.EventTag == Pending.Request.EventTag) return true;
		}
	}
	if (Pending.Operation != EOutlierInfoRequestOperation::Show
		&& Pending.Operation != EOutlierInfoRequestOperation::CancelInputs
		&& !PendingInfoRequests.ContainsByPredicate([&Pending](const FOutlierPendingInfoRequest& Existing)
		{
			return Existing.Operation == EOutlierInfoRequestOperation::Show
				&& Existing.Region == Pending.Region && Existing.Request.EventTag == Pending.Request.EventTag;
		}))
	{
		return false;
	}
	FOutlierPendingInfoRequest Copy = Pending;
	Copy.World = World;
	PendingInfoRequests.Add(Copy);
	return true;
}

bool ULocalPlayerUISubSystem::QueueInfoEvent(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request)
{
	FOutlierPendingInfoRequest Pending;
	Pending.Region = Region;
	Pending.Request = Request;
	return SubmitInfoRequest(Pending);
}

bool ULocalPlayerUISubSystem::CancelInfoEvent(EOutlierInfoRegion Region, FGameplayTag EventTag)
{
	if (!EventTag.IsValid()) return false;
	const int32 Removed = PendingInfoRequests.RemoveAll([Region, EventTag](const FOutlierPendingInfoRequest& Pending)
	{
		return Pending.Region == Region && Pending.Request.EventTag == EventTag;
	});
	UInfoCanvasBase* Canvas = GetInfoCanvas();
	return (Canvas && Canvas->CancelInfo(Region, EventTag)) || Removed > 0;
}

bool ULocalPlayerUISubSystem::CompleteInfoEvent(EOutlierInfoRegion Region, FGameplayTag EventTag)
{
	FOutlierPendingInfoRequest Pending;
	Pending.Region = Region;
	Pending.Operation = EOutlierInfoRequestOperation::Complete;
	Pending.Request.EventTag = EventTag;
	return SubmitInfoRequest(Pending);
}

bool ULocalPlayerUISubSystem::CompleteInputEvent(FGameplayTag EventTag)
{
	FOutlierPendingInfoRequest Pending;
	Pending.Operation = EOutlierInfoRequestOperation::CompleteInput;
	Pending.Request.EventTag = EventTag;
	return SubmitInfoRequest(Pending);
}

bool ULocalPlayerUISubSystem::NotifyInfoOverlap(FGameplayTag EventTag)
{
	FOutlierPendingInfoRequest Pending;
	Pending.Operation = EOutlierInfoRequestOperation::Overlap;
	Pending.Request.EventTag = EventTag;
	return SubmitInfoRequest(Pending);
}

void ULocalPlayerUISubSystem::CancelInputInfos()
{
	FOutlierPendingInfoRequest Pending;
	Pending.Operation = EOutlierInfoRequestOperation::CancelInputs;
	SubmitInfoRequest(Pending);
}

FGameplayTag ULocalPlayerUISubSystem::GetActiveInputEventTag() const
{
	UInfoCanvasBase* Canvas = GetInfoCanvas();
	return Canvas ? Canvas->GetActiveInputEventTag() : FGameplayTag();
}

void ULocalPlayerUISubSystem::ClearEventInfo()
{
	if (bClearingEventInfo) return;
	TGuardValue<bool> ClearingGuard(bClearingEventInfo, true);
	PendingInfoRequests.Reset();
	if (UInfoCanvasBase* Canvas = GetInfoCanvas()) Canvas->ClearInfoQueues();
}

bool ULocalPlayerUISubSystem::ShowEventUpper(const FOutlierInfoWidgetRequest& Request)
{
	return QueueInfoEvent(EOutlierInfoRegion::Upper, Request);
}

bool ULocalPlayerUISubSystem::HideEventUpper(FGameplayTag EventTag)
{
	return CancelInfoEvent(EOutlierInfoRegion::Upper, EventTag);
}

bool ULocalPlayerUISubSystem::ShowEventGif(FGameplayTag EventTag, FGameplayTag GifTag)
{
	FOutlierInfoWidgetRequest Request;
	Request.EventTag = EventTag;
	Request.GifTag = GifTag;
	Request.EndCondition = EOutlierInfoEndCondition::Time;
	return GifTag.IsValid() && QueueInfoEvent(EOutlierInfoRegion::Right, Request);
}

bool ULocalPlayerUISubSystem::HideEventGif(FGameplayTag EventTag)
{
	return CancelInfoEvent(EOutlierInfoRegion::Right, EventTag);
}

bool ULocalPlayerUISubSystem::ShowEventScript(FGameplayTag EventTag, FGameplayTag TTSTag, float FallbackDuration)
{
	FOutlierInfoWidgetRequest Request;
	Request.EventTag = EventTag;
	Request.TTSTag = TTSTag;
	if (FallbackDuration > 0.0f)
	{
		Request.EndCondition = EOutlierInfoEndCondition::Time;
		Request.Duration = FallbackDuration;
	}
	return QueueInfoEvent(EOutlierInfoRegion::BottomTTS, Request);
}

void ULocalPlayerUISubSystem::FinishEventScript(FGameplayTag EventTag, bool bCompleted)
{
	// An undelivered subtitle expires with its voice. A live widget decides its own lifetime.
	PendingInfoRequests.RemoveAll([EventTag](const FOutlierPendingInfoRequest& Pending)
	{
		return Pending.Region == EOutlierInfoRegion::BottomTTS && Pending.Request.EventTag == EventTag;
	});
	if (UInfoCanvasBase* Canvas = GetInfoCanvas())
	{
		Canvas->FinishInfoSource(EOutlierInfoRegion::BottomTTS, EventTag, bCompleted);
	}
}

void ULocalPlayerUISubSystem::RefreshEventUI()
{
	if (bDispatchingInfoRequests || bIsDeinitializing || bClearingEventInfo) return;
	TGuardValue<bool> DispatchGuard(bDispatchingInfoRequests, true);
	while (!PendingInfoRequests.IsEmpty())
	{
		UInfoCanvasBase* Canvas = GetInfoCanvas();
		if (!Canvas) break;
		const FOutlierPendingInfoRequest Pending = PendingInfoRequests[0];
		PendingInfoRequests.RemoveAt(0);
		if (Pending.World.IsValid() && Pending.World.Get() == GetWorld() && !Pending.World->bIsTearingDown)
		{
			DispatchInfoRequest(Canvas, Pending);
		}
	}
}

void ULocalPlayerUISubSystem::HandleInfoWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	PendingInfoRequests.RemoveAll([World](const FOutlierPendingInfoRequest& Pending)
	{
		return !Pending.World.IsValid() || Pending.World.Get() == World;
	});
	if (UInfoCanvasBase* Canvas = GetInfoCanvas(); Canvas && Canvas->GetWorld() == World)
	{
		ClearEventInfo();
	}
}

void ULocalPlayerUISubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InfoWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleInfoWorldCleanup);
	//UE_LOG(LogTemp, Warning, TEXT("LocalPlayerUISubSystem Initialized"));
}

void ULocalPlayerUISubSystem::Deinitialize()
{
	bIsDeinitializing = true;
	FWorldDelegates::OnWorldCleanup.Remove(InfoWorldCleanupHandle);
	ClearEventInfo();
	Super::Deinitialize();
}

void ULocalPlayerUISubSystem::RegisterMainUI(UMainUIBase* InMainUI)
{
	if (!IsValid(InMainUI)) return;
	if (MainUIInstance != InMainUI)
	{
		TGuardValue<bool> ClearingGuard(bClearingEventInfo, true);
		if (UInfoCanvasBase* PreviousCanvas = GetInfoCanvas()) PreviousCanvas->ClearInfoQueues();
	}
	MainUIInstance = InMainUI;
	if (bTransientWidgetsSuppressed)
	{
		FlushMainUITransientWidgets();
	}

	// 마지막으로 받은 슈트 상태를 새 MainUI 에 물려준다.
	// 위젯이 신호보다 늦게 생겨도 게이트가 맞도록 하는 유일한 경로다.
	OnShooterSuitAcquiredChanged(bShooterSuitAcquired);
	RefreshEventUI();
}

void ULocalPlayerUISubSystem::UnregisterMainUI(UMainUIBase* InMainUI)
{
	if (MainUIInstance == InMainUI)
	{
		ClearEventInfo();
		MainUIInstance = nullptr;
	}
}

void ULocalPlayerUISubSystem::FlushMainUITransientWidgets()
{
	ClearEventInfo();
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->FlushTransientWidgets();
	}
}

void ULocalPlayerUISubSystem::SetTransientWidgetsSuppressed(bool bSuppressed)
{
	bTransientWidgetsSuppressed = bSuppressed;
	if (bSuppressed)
	{
		FlushMainUITransientWidgets();
	}
}

UMainUIBase* ULocalPlayerUISubSystem::GetMainUI() const
{
	return IsValid(MainUIInstance) ? MainUIInstance.Get() : nullptr;
}

UEventDrivenUI* ULocalPlayerUISubSystem::GetModule(const FGameplayTag& ModuleTag) const
{
	UMainUIBase* MainUI = GetMainUI();
	return MainUI ? MainUI->GetModule(ModuleTag) : nullptr;
}

UEventDrivenUI* ULocalPlayerUISubSystem::GetModuleAny(
	const FGameplayTag& FirstTag,
	const FGameplayTag& SecondTag
) const
{
	if (UEventDrivenUI* FirstModule = GetModule(FirstTag))
	{
		return FirstModule;
	}

	return GetModule(SecondTag);
}

void ULocalPlayerUISubSystem::OnRep_HUDActivate(bool bShouldActivate)
{
	UMainUIBase* MainUI = GetMainUI();
	if (!MainUI)
	{
		return;

	}

	if (bShouldActivate)
	{
		MainUI->ModuleActivate();
	}
	else
	{
		MainUI->ModuleDeActivate();
	}

}

void ULocalPlayerUISubSystem::OnRep_HealthChanged(float InHealth, float MaxHealth)
{
	if (!GetMainUI())
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(GetModuleAny(TagDrivenUITags::Shooter::HP(), TagDrivenUITags::Partner::HP())))
	{
		HPBarUI->SetHealthState(InHealth, MaxHealth);
	}
}

void ULocalPlayerUISubSystem::OnRep_PartnerHealthChanged(float InHealth, float MaxHealth)
{
	if (UPartnerHealthUI* PartnerHealthUI = Cast<UPartnerHealthUI>(
		GetModule(TagDrivenUITags::Partner::Health())))
	{
		PartnerHealthUI->SetHealthState(InHealth, MaxHealth);
	}
}

void ULocalPlayerUISubSystem::OnRep_PartnerShieldChanged(float InPartnerShield, float MaxPartnerShield)
{
	float Ratio = MaxPartnerShield > 0.0f ? InPartnerShield / MaxPartnerShield : 0.0f;

	if (!GetMainUI())
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(GetModuleAny(TagDrivenUITags::Shooter::HP(), TagDrivenUITags::Partner::HP())))
	{
		//UE_LOG(LogTemp, Error, TEXT("PartnerShield Changed, %f"), Ratio);
		HPBarUI->PartnerShieldChanged(Ratio);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShieldChanged( float InCurShield,  float InMaxShield)
{
	if (!GetMainUI())
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(GetModuleAny(TagDrivenUITags::Shooter::HP(), TagDrivenUITags::Partner::HP())))
	{
		HPBarUI->SetShieldState(InCurShield, InMaxShield);
	}
}

void ULocalPlayerUISubSystem::OnShooterSuitAcquiredChanged(bool bAcquired)
{
	// 이 함수는 UI 갱신 때마다 반복해서 불린다. 크로스헤어 초기화는 상태가 실제로
	// 바뀐 순간에만 해야 한다 — 매번 하면 플레이 중 확산이 계속 0 으로 밟힌다.
	const bool bChanged = (bShooterSuitAcquired != bAcquired);
	bShooterSuitAcquired = bAcquired;

	if (UGangTongMainUI* PartnerMainUI = Cast<UGangTongMainUI>(GetMainUI()))
	{
		PartnerMainUI->SetSuitGatedModulesEnabled(bAcquired);
	}

	// 슈트 이전에 쏜 흔적(확산/조준 상태)이 남아 있으면 획득 후 무기를 다시 꺼낼 때
	// 그대로 되살아난다. 획득 시점에 양쪽 크로스헤어를 초기값으로 되돌린다.
	if (bAcquired && bChanged)
	{
		if (UShooterMainWidget* ShooterMainUI = Cast<UShooterMainWidget>(GetMainUI()))
		{
			ShooterMainUI->ResetCrossHairs();
		}
	}
}

void ULocalPlayerUISubSystem::SyncRegisteredModule(UEventDrivenUI* InModule)
{
	if (UAmmoUI* AmmoUI = Cast<UAmmoUI>(InModule))
	{
		AmmoUI->SetAmmoState(CachedAmmoCount, CachedMaxAmmo);
		AmmoUI->SetWeaponOverchargeActive(bCachedWeaponOvercharge);
		AmmoUI->SetInfiniteAmmoDisplayActive(bCachedInfiniteAmmoDisplay);
	}
}

void ULocalPlayerUISubSystem::OnRep_AmmoCountChanged(int32 InCurrentAmmo, int32 InMaxAmmo)
{
	// 모듈이 아직 등록되기 전이어도 값은 남겨둔다 (SyncRegisteredModule 이 재생).
	CachedAmmoCount = InCurrentAmmo;
	CachedMaxAmmo = InMaxAmmo;

	if (!GetMainUI())
	{
		return;
	}

	if (UAmmoUI* AmmoUI = Cast<UAmmoUI>(GetModuleAny(TagDrivenUITags::Shooter::Ammo(), TagDrivenUITags::Partner::Ammo())))
	{
		AmmoUI->SetAmmoState(InCurrentAmmo, InMaxAmmo);
	}

}

void ULocalPlayerUISubSystem::OnRep_WeaponOverchargeChanged(bool bActive)
{
	// 탄약과 같이 모듈 등록 전 신호도 남겨두고 SyncRegisteredModule 에서 재생한다.
	bCachedWeaponOvercharge = bActive;

	if (UAmmoUI* AmmoUI = Cast<UAmmoUI>(GetModule(TagDrivenUITags::Shooter::Ammo())))
	{
		AmmoUI->SetWeaponOverchargeActive(bActive);
	}
}

void ULocalPlayerUISubSystem::OnRep_InfiniteAmmoDisplayChanged(bool bActive)
{
	bCachedInfiniteAmmoDisplay = bActive;

	if (UAmmoUI* AmmoUI = Cast<UAmmoUI>(GetModule(TagDrivenUITags::Shooter::Ammo())))
	{
		AmmoUI->SetInfiniteAmmoDisplayActive(bActive);
	}
}

void ULocalPlayerUISubSystem::OnDamageFeedback(
	AActor* DamagedCharacter,
	const FVector& DamageOrigin)
{
	if (bTransientWidgetsSuppressed)
	{
		return;
	}
	UMainUIBase* MainUI = GetMainUI();
	if (MainUI && MainUI->DamageFeedbackLayer)
	{
		MainUI->DamageFeedbackLayer->ShowDamageFeedback(DamagedCharacter, DamageOrigin);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShooterConditionRefresh()
{
	if (UPartnerLeftHudWidget* PartnerLeftHUD = Cast<UPartnerLeftHudWidget>(
		GetModule(TagDrivenUITags::Partner::HP())))
	{
		PartnerLeftHUD->RefreshShooterConditionUI();
	}
}

void ULocalPlayerUISubSystem::OnRep_PlayerStateChanged(EUIPlayerState State)
{

	if (UDynamicCrossHair* CrossHairUI = Cast<UDynamicCrossHair>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		CrossHairUI->SetPlayerState(State);
	}
	else
	{
		//UE_LOG(LogTemp, Error, TEXT("UNVALID CLASS NOT DynamicCrossHairClass"));
	}
}



void ULocalPlayerUISubSystem::PartnerDistanceUpdate(const float Distance)
{
	if (UPartnerLeftHudWidget* PartnerLeftHUD = Cast<UPartnerLeftHudWidget>(GetModule(TagDrivenUITags::Partner::DistanceLimit())))
	{
		PartnerLeftHUD->UpdateDistanceRatio(Distance);
	}
}

void ULocalPlayerUISubSystem::OnCurrentWeaponChanged(EWidgetWeaponType WeaponType)
{
	if (UShooterMainWidget* ShooterMainUI = Cast<UShooterMainWidget>(GetMainUI()))
	{
		ShooterMainUI->OnChangeWeapon(WeaponType);
	}
}

void ULocalPlayerUISubSystem::OnCurrentAbilityChanged(const FGameplayTag& AbilityTag)
{
	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		CurrentAbilityIcon->SetCurrentAbility(AbilityTag);
	}
}

void ULocalPlayerUISubSystem::ResetShooterAbilityState(const FGameplayTag& SelectedAbilityTag)
{
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->ResetAbilityCooldowns();
	}

	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(
		GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		CurrentAbilityIcon->ResetCooldown();
		CurrentAbilityIcon->SetCurrentAbility(SelectedAbilityTag);
	}
}

bool ULocalPlayerUISubSystem::ApplyCurrentAbilityCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime)
{
	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		return CurrentAbilityIcon->ApplyCooldownIfMatches(AbilityTag, CoolTime);
	}

	return false;
}

void ULocalPlayerUISubSystem::OnRep_Aiming()
{
	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		CrossHairBase->OnAiming();
	}
	
}

void ULocalPlayerUISubSystem::OnRep_AimingOff()
{
	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		CrossHairBase->OnAimingOff();
	}
}


void ULocalPlayerUISubSystem::OnRep_AttackSign(EAttackSign InType)
{

	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		//UE_LOG(LogTemp, Error, TEXT("CrossHair Instance Class: %s"), *GetNameSafe(CrossHairBase->GetClass()));
		//UE_LOG(LogTemp, Error, TEXT("OnRep_AttackSign %d"), (uint8)InType);
		CrossHairBase->SpawnAttackSign(InType);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShootCrosshairChanged(float InFireRate, float InElapsedTime)
{
	// 슈트 전에는 크로스헤어 상태를 아예 건드리지 않는다 (HUD 가 꺼져 있는 구간).
	if (!bShooterSuitAcquired)
	{
		return;
	}

	UEventDrivenUI* CrossHairModule = GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair());
	if (!CrossHairModule)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LocalPlayerUISubSystem] ShootCrosshair skipped: MainUI or CrossHair module is not ready"));
		return;
	}

	if (UDynamicCrossHair* CrossHairBase = Cast<UDynamicCrossHair>(CrossHairModule))
	{
		//UE_LOG(LogTemp, Log, TEXT("OnRep_ShootCrosshairChanged"));
		CrossHairBase->On_RepShoot();
	}
	else if (UStaticCrossHair* Crosshair = Cast<UStaticCrossHair>(CrossHairModule))
	{
		Crosshair->SetCoolTime(InFireRate, InElapsedTime);
		//UE_LOG(LogTemp, Log, TEXT("InFireRate %f"), InFireRate);

	}
	else
		UE_LOG(LogTemp, Log, TEXT("Type Error"));

}

void ULocalPlayerUISubSystem::OnRep_ShooterHPStateChanged(const FGameplayTag& InShooterConditionTag)
{
	if (UPartnerLeftHudWidget* PartnerLeftHUD = Cast<UPartnerLeftHudWidget>(
		GetModule(TagDrivenUITags::Partner::HP())))
	{
		PartnerLeftHUD->SetShooterCondition(InShooterConditionTag);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShooterDynamicCrosshairChanged(bool InFlag)
{
	if (!bShooterSuitAcquired)
	{
		return;
	}

	if (UCrossHairBase* Crosshair = Cast<UCrossHairBase>(GetModule(TagDrivenUITags::Shooter::CrossHair())))
	{
		if (InFlag)
		{
			Crosshair->OnAiming();
		}
		else
		{
			Crosshair->OnAimingOff();
		}
	}
}

void ULocalPlayerUISubSystem::OnAbilityDisabledByDistance()
{
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->On_RepAbilityDisabledByDistance();
	}
}

void ULocalPlayerUISubSystem::OnAbilityEnabledByDistance()
{
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->On_RepAbilityabledByDistance();
	}
}

void ULocalPlayerUISubSystem::OnAbilityUsed(const FGameplayTag& AbilityTag, float CoolTime)
{
	UMainUIBase* MainUI = GetMainUI();
	if (!MainUI)
	{
		return;
	}

	if (UAbilityIconUI* Icon = MainUI->GetAbilityIcon(AbilityTag))
	{
		Icon->SetCoolTime(CoolTime);
	}

	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		CurrentAbilityIcon->ApplyCooldownIfMatches(AbilityTag, CoolTime);
	}
}
