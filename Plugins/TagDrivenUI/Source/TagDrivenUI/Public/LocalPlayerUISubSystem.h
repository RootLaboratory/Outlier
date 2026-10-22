// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "CrossHairBase.h"
#include "MainUIBase.h"
#include "PartnerHPUI.h"
#include "OutlierInfoTypes.h"
#include "LocalPlayerUISubSystem.generated.h"

class UEventDrivenUI;
class AActor;
class UInfoCanvasBase;
class UWorld;
enum class EWidgetWeaponType : uint8;

enum class EOutlierInfoRequestOperation : uint8
{
	Show,
	Complete,
	Overlap
};

/** MainUI 준비 전까지만 쓴다. 활성 콘텐츠/수명은 여기에 캐시하지 않는다. */
struct FOutlierPendingInfoRequest
{
	EOutlierInfoRegion Region = EOutlierInfoRegion::Upper;
	EOutlierInfoRequestOperation Operation = EOutlierInfoRequestOperation::Show;
	FOutlierInfoWidgetRequest Request;
	TWeakObjectPtr<UWorld> World;
};

UCLASS()
class TAGDRIVENUI_API ULocalPlayerUISubSystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 요청을 MainUI.InfoLayer로 전달한다. 데이터 해석과 큐는 위젯이 소유한다. */
	UFUNCTION(BlueprintPure, Category = "UI|Info")
	UInfoCanvasBase* GetInfoCanvas() const;

	UFUNCTION(BlueprintCallable, Category = "UI|Info")
	bool QueueInfoEvent(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "UI|Info")
	bool CancelInfoEvent(EOutlierInfoRegion Region, FGameplayTag EventTag);

	UFUNCTION(BlueprintCallable, Category = "UI|Info")
	bool CompleteInfoEvent(EOutlierInfoRegion Region, FGameplayTag EventTag);

	bool NotifyInfoOverlap(FGameplayTag EventTag);
	bool NotifyInfoOverlap(EOutlierInfoRegion Region, FGameplayTag EventTag);
	void ClearEventInfo();
	bool ShowEventUpper(const FOutlierInfoWidgetRequest& Request);
	bool HideEventUpper(FGameplayTag EventTag);
	bool ShowEventGif(FGameplayTag EventTag, FGameplayTag GifTag);
	bool HideEventGif(FGameplayTag EventTag);
	/** 자막은 음성과 수명이 같다. FinishEventScript가 종료한다. */
	bool ShowEventScript(FGameplayTag EventTag, FGameplayTag TTSTag = FGameplayTag());
	void FinishEventScript(FGameplayTag EventTag, bool bCompleted);

	/** 미전달 요청을 나중에 생성된 HUD로 넘긴다. 활성 위젯 상태는 재생하지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "UI|Event")
	void RefreshEventUI();

public:
	void RegisterMainUI(UMainUIBase* InMainUI);
	void UnregisterMainUI(UMainUIBase* InMainUI);
	// 로컬 HUD가 늦게 생성되거나 교체됐는지 확인할 때 현재 등록된 인스턴스를 반환한다.
	UMainUIBase* GetMainUI() const;
	void FlushMainUITransientWidgets();
	// 사망 중 늦게 도착한 피격 표시가 다시 쌓이지 않게 한다.
	void SetTransientWidgetsSuppressed(bool bSuppressed);
	void OnRep_PlayerStateChanged(EUIPlayerState State);

public:
	//Replicated
	void OnRep_HUDActivate(bool bShouldActivate); //Whole Widgets Activation Toggle
	void OnRep_HealthChanged(float InHealth, float MaxHealth);
	void OnRep_PartnerHealthChanged(float InHealth, float MaxHealth);
	void OnRep_PartnerShieldChanged(float InHealth, float MaxHealth);
	void OnRep_ShieldChanged( float InCurShield ,  float InMaxShield);
	void OnRep_AmmoCountChanged(int32 InCurrentAmmo, int32 InMaxAmmo);
	void OnRep_WeaponOverchargeChanged(bool bActive);
	void OnRep_InfiniteAmmoDisplayChanged(bool bActive);
	void OnDamageFeedback(AActor* DamagedCharacter, const FVector& DamageOrigin);

	// Shooter 의 슈트 획득 상태가 바뀌었을 때 로컬 화면에 반영한다.
	// 지금은 Partner 의 거리 위젯만 이 신호를 쓴다.
	void OnShooterSuitAcquiredChanged(bool bAcquired);

	// 뒤늦게 등록된 모듈에 현재 상태를 물려준다.
	// 값 푸시는 "그 순간 등록돼 있던" 모듈에만 닿으므로, 나중에 붙는 멤버 위젯은
	// Ammo WBP 기본값 그대로 남는다.
	void SyncRegisteredModule(UEventDrivenUI* InModule);
	void OnRep_ShooterConditionRefresh();
public:
	void OnRep_Aiming();
	void OnRep_AimingOff();
	void OnRep_AttackSign(EAttackSign InType);
	void OnRep_ShootCrosshairChanged(float InFireRate, float InElapsedTime = 0.0f);
	void OnRep_ShooterHPStateChanged(const FGameplayTag& InShooterConditionTag);
	void OnRep_ShooterDynamicCrosshairChanged(bool InFlag);
public:

	void PartnerDistanceUpdate(const float Distance);
	void OnCurrentWeaponChanged(EWidgetWeaponType WeaponType);
	void OnCurrentAbilityChanged(const FGameplayTag& AbilityTag);
	void SetShooterAbilitySkillCount(const FGameplayTag& AbilityTag, int32 SkillCount);
	void SetShooterAbilityChargeState(const FGameplayTag& AbilityTag, int32 CurrentCharges, int32 MaxCharges,
		float RechargeDuration, float RechargeRemaining, bool bPlayRecoveryFeedback);
	void SetPartnerAbilityChargeState(const FGameplayTag& AbilityTag, int32 CurrentCharges, int32 MaxCharges,
		float RechargeDuration, float RechargeRemaining, bool bPlayRecoveryFeedback, bool bTemporarilyBlocked);
	void ResetShooterAbilityState(const FGameplayTag& SelectedAbilityTag);
	bool ApplyCurrentAbilityCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime);
	void OnAbilityUsed(const FGameplayTag& AbilityTag, float CoolTime);
	void OnAbilityDisabledByDistance();
	void OnAbilityEnabledByDistance();

private:
	bool SubmitInfoRequest(const FOutlierPendingInfoRequest& Pending);
	bool DispatchInfoRequest(UInfoCanvasBase* Canvas, const FOutlierPendingInfoRequest& Pending);
	void HandleInfoWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	TArray<FOutlierPendingInfoRequest> PendingInfoRequests;
	FDelegateHandle InfoWorldCleanupHandle;
	bool bDispatchingInfoRequests = false;
	bool bClearingEventInfo = false;
	bool bIsDeinitializing = false;

	UEventDrivenUI* GetModule(const FGameplayTag& ModuleTag) const;
	UEventDrivenUI* GetModuleAny(const FGameplayTag& FirstTag, const FGameplayTag& SecondTag) const;

	UPROPERTY()
	TObjectPtr<UMainUIBase> MainUIInstance;

	// 모듈이 아직 없을 때 들어온 값도 기억해둔다. SyncRegisteredModule 이 이걸 재생한다.
	int32 CachedAmmoCount = 0;
	int32 CachedMaxAmmo = 0;
	bool bCachedWeaponOvercharge = false;
	bool bCachedInfiniteAmmoDisplay = false;

	// 슈트 획득 신호는 MainUI 가 생기기 전에 도착할 수 있다(OnRep 은 값이 바뀌는 순간 한 번뿐).
	// 마지막 상태를 들고 있다가 RegisterMainUI 에서 새 위젯에 그대로 물려준다.
	bool bShooterSuitAcquired = false;

	bool bTransientWidgetsSuppressed = false;
};
