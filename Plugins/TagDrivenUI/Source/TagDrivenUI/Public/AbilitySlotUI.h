// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "GameplayTagContainer.h"
#include "AbilitySlotUI.generated.h"

class UAbilityIconUI;
class UImage;
class UTexture2D;
class USizeBox;
class UInputAction;

// Shooter는 선택한 스킬 하나, Partner는 고정 스킬별 인스턴스로 사용한다.
// 아이콘, 충전 막대와 회복 연출을 하나의 슬롯에서 갱신한다.
UCLASS(meta = (DisplayName = "Ability Slot"))
class TAGDRIVENUI_API UAbilitySlotUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Ability|Input")
	void SetInputAction(UInputAction* InputAction);

	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetCurrentAbility(const FGameplayTag& AbilityTag);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetIconTexture(UTexture2D* InTexture);

	// 배치 크기는 유지하고 아이콘의 표시 배율과 위치만 조절한다.
	void SetIconLayout(const FVector2D& RenderScale, const FVector2D& PositionOffset);

	// 현재 선택된 능력의 표시 횟수를 설정한다. 0이면 두 표시를 모두 숨긴다.
	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetSkillCount(int32 InSkillCount);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetAbilitySkillCount(const FGameplayTag& AbilityTag, int32 InSkillCount);

	UFUNCTION(BlueprintPure, Category = "Ability")
	int32 GetAbilitySkillCount(const FGameplayTag& AbilityTag) const;

	UFUNCTION(BlueprintCallable, Category = "Ability|Charges")
	void SetAbilityChargeState(const FGameplayTag& AbilityTag, int32 CurrentCharges, int32 MaxCharges,
		float RechargeDuration, float RechargeRemaining, bool bPlayRecoveryFeedback, bool bTemporarilyBlocked = false);

	// 이름 바인딩 대신 기존 BP의 Image를 그대로 등록해도 된다.
	UFUNCTION(BlueprintCallable, Category = "Ability|Charges")
	void RegisterSkillCountImages(UImage* SingleImage, UImage* LeftImage, UImage* RightImage);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	bool ApplyCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime);

	void ResetCooldown();

	// 드론 거리 이탈/재부팅으로 슈트 사용이 막히면 LockIcon을 띄운다.
	void SetAbilityEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Ability")
	FGameplayTag GetCurrentAbilityTag() const { return CurrentAbilityTag; }

private:
	void ApplyInputAction();

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DisplayedInputAction;

	bool bHasInputActionOverride = false;

	void ApplyIconTexture();
	void UpdateSkillCountVisibility();
	void ApplyCurrentChargeState();
	void ResetBarFeedback();
	void PlayBarFeedback(UImage* Image);

public:
	// Matches the existing child name in the shared slot WBP.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, MustImplement = "/Script/TagDrivenUI.InputActionDisplayInterface"))
	TObjectPtr<UUserWidget> WBP_InputBindKeyDisplayWidget;

	// 미지정 능력은 1회로 표시한다. 업그레이드가 바뀌면 해당 능력의 값만 갱신한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability"))
	TMap<FGameplayTag, int32> AbilitySkillCounts;

	// HUD에서 전달받는 현재 스킬의 Texture. 슬롯 자체는 스킬별 리소스를 보관하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> CurrentAbilityIcon;

	// 100×100으로 고정한 아이콘 영역. 중앙을 기준으로 표시 배율과 위치만 바꾼다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> IconSizeBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> LockIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SkillCount_1;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SkillCount_2;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> SkillCountSingleImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> SkillCountLeftImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> SkillCountRightImage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Charges")
	TObjectPtr<UTexture2D> SkillCountOnTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Charges")
	TObjectPtr<UTexture2D> SkillCountOffTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Feedback", meta = (ClampMin = "0.01"))
	float RecoveryPulseDuration = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Feedback", meta = (ClampMin = "1.0"))
	float RecoveryPulseScale = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability"))
	FGameplayTag CurrentAbilityTag;

private:
	TWeakObjectPtr<USizeBox> CachedIconSizeBox;
	FVector2D DefaultIconScale = FVector2D::UnitVector;
	FVector2D DefaultIconTranslation = FVector2D::ZeroVector;

	struct FChargeDisplayState
	{
		int32 Current = 0;
		int32 Maximum = 1;
		float Duration = 0.0f;
		float Remaining = 0.0f;
		float ReceivedTime = 0.0f;
		bool bTemporarilyBlocked = false;
	};
	TMap<FGameplayTag, FChargeDisplayState> ChargeDisplayStates;
	// 충전 상태를 따로 받지 않는 능력은 서버 쿨타임의 시작/종료로 막대를 갱신한다.
	TMap<FGameplayTag, bool> AbilityCooldownStates;
	TWeakObjectPtr<UImage> PulsingBar;
	FVector2D PulseBaseScale = FVector2D::UnitVector;
	FVector2D PulseBasePivot = FVector2D::ZeroVector;
	float PulseElapsed = -1.0f;
};
