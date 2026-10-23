// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "AbilityIconUI.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextBlock;
class UTexture2D;

UCLASS()
class TAGDRIVENUI_API UAbilityIconUI : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	UFUNCTION(BlueprintPure, Category = "Ability")
	bool IsUnLock() const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	FGameplayTag GetAbilityTag() const;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void SetAbility(FGameplayTag InAbility);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void AbilityUnLock();

	// Grant 가 빠진 능력( 업그레이드 초기화 등 )을 다시 잠근다. 진행 중이던 쿨타임 표시도 같이 정리한다.
	void AbilityLock();

	void VisibilityControl(bool InFlag);

public:
	UFUNCTION(BlueprintCallable, Category = "Ability|Cooldown")
	void SetCoolTime(float InCoolTime);

	void SetCoolTimeAt(float InCoolTime, float InStartTime);
	void UpdateCoolTime(float Delta);

	UFUNCTION(BlueprintPure, Category = "Ability|Cooldown")
	bool IsCooldowning() const;

	UFUNCTION(BlueprintCallable, Category = "Ability|Cooldown")
	void CooldownDone();

	// 충전 게이지와 사용 가능 여부는 독립적이다. 남은 1회가 있어도 게이지는 계속 돈다.
	void SetChargePresentation(bool bAvailable, float RechargeDuration, float RechargeRemaining);
	void ResetChargePresentation();
	void PlayFullChargeFeedback();
	void UpdateChargeFeedback(float DeltaTime);
	void ResetChargeFeedback();
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Ability|Appearance")
	void SetIconTexture(UTexture2D* InTexture);

	void SetIconBrush(const FSlateBrush& Brush);

public:
	// 부모 Designer에서는 인스턴스별로 지정하고, 실행 중 변경도 같은 적용 경로를 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetIconTexture, Category = "Ability|Appearance")
	TObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AbilityIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> GlowImage;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Feedback", meta = (ClampMin = "0.01"))
	float FullChargeFeedbackDuration = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Feedback", meta = (ClampMin = "1.0"))
	float FullChargeFeedbackScale = 1.15f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AbilityMID;

	// IsCoolDown은 게이지 표시, IsUnavailable은 아이콘 비활성 마스크를 제어한다.
	// 파라미터: IconTexture, CooldownProgress, IsCoolDown, IsUnavailable
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Material")
	TObjectPtr<UMaterialInterface> M_AbilityIconMaster;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability"))
	FGameplayTag AbilityTag;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	FSlateBrush DefaultIconBrush;

private:
	void ApplyIconTexture();

	// 쿨타임 중이면 마스터 머티리얼 MID로 교체
	void EnsureMasterMaterial();
	void SyncMaterialState();

	// 쿨타임이 끝나면 기본 텍스처로 복귀
	void TryRestoreDefaultBrush();

private:
	uint8 bAbilityUnlocked : 1 = false;
	uint8 bCooldowning     : 1 = false;

	float CoolTime        = 0.0f;
	float CooldownStartTime = 0.0f;
	float AccumulatedTime = 0.0f;
	bool bChargePresentation = false;
	bool bChargeAvailable = true;
	float FullChargeFeedbackElapsed = -1.0f;
	FVector2D GlowBaseScale = FVector2D::UnitVector;
};
