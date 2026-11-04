#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnemyHudWidget.generated.h"

class AEnemyBase;
class UAbilitySystemComponent;
class ULocalPlayerPostProcessSubsystem;
class UTexture2D;
class UHackEffectTimerWidget;
struct FOnAttributeChangeData;

// Partner가 Enemy를 빙의한 동안 Partner MainWidget 대신 띄우는 HUD.
// PartnerPlayerController가 EnemyPossessed 진입 때 Push하고, PartnerControlled 복귀 때 Pop한다.
// 빙의 대상 체력이 20% 깎일 때마다 모서리 마스크 하나를 무작위로 골라 Drone Damage Feedback(RDG)에 드러낸다.
UCLASS(Abstract)
class OUTLIER_API UEnemyHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Push 직전에 빙의 대상을 넘긴다. 체력과 해킹 시간을 구독하고 현재 상태를 바로 반영한다.
	// 터렛용 WBP도 같은 함수로 대상을 연결한다. nullptr는 이전 대상의 구독을 해제한다.
	UFUNCTION(BlueprintCallable, Category = "Enemy HUD")
	void InitializeForEnemy(AEnemyBase* InEnemy);
	UFUNCTION(BlueprintPure, Category = "Enemy HUD")
	AEnemyBase* GetOwningEnemy() const;
	// 빙의 액터나 GAS 상태 변경 없이 동일한 마스크, 섞기 및 20% 임계값을 사용한다.
	bool InitializeDamagePreview(int32 Seed);
	void SetDamagePreviewHealthRatio(float HealthRatio);

	// Pop 직전에 PartnerPC가 부른다. 체력/시간 구독을 끊고 RDG 마스크를 지운다.
	void ReleaseDamageFeedback(bool bPreserveHackExpiry = false);

	// 서버의 복귀 통보가 Enemy 상태 복제보다 먼저 도착해도 마지막 숫자를 준비한다.
	void ShowHackTimerExpiry(AEnemyBase* Target);
	bool IsShowingHackTimerExpiry() const;
	FSimpleDelegate OnHackTimerExpiryDisplayFinished;

	// 사망 연출로 HUD가 숨겨진 동안 마스크 글리치도 멈춘다. 드러난 상태는 유지한다.
	void SetDamageFeedbackSuppressed(bool bSuppressed);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UHackEffectTimerWidget> HackEffectTimerWidget;

	// 화면 다섯 위치의 텍스처. Image 배치와 바인딩은 WBP에서 한다.
	// Middle을 뺀 4장은 화면 해상도에 맞춘 마스크(.r × .a)로 Drone Damage Feedback에도 쓰인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Texture")
	TObjectPtr<UTexture2D> LeftTopTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Texture")
	TObjectPtr<UTexture2D> LeftBottomTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Texture")
	TObjectPtr<UTexture2D> MiddleTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Texture")
	TObjectPtr<UTexture2D> RightTopTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Texture")
	TObjectPtr<UTexture2D> RightBottomTexture;

private:
	friend class FEnemyHackEffectHUDTest;
	friend class FEnemyHackEffectAssetBindingTest;

	void InitializeHackTimer();
	void HandleHackTimerExpiryDisplayFinished();

	void BindDamageFeedback();
	void HandleVitalAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void RefreshDamageStage();
	bool InitializeDamageMasks(int32 Seed);
	void ApplyDamageLostRatio(float LostRatio);
	ULocalPlayerPostProcessSubsystem* GetPostProcessSubsystem() const;
	UTexture2D* GetDamageMaskTexture(int32 MaskIndex) const;

	TWeakObjectPtr<AEnemyBase> OwningEnemy;
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;

	// 단계가 열리는 모서리 순서(EDroneDamageMask 값). 적마다 고정되도록 적 고유 ID로 섞는다.
	uint8 DamageRevealOrder[4] = { 0, 1, 2, 3 };
	int32 RevealedDamageStageCount = 0;
	bool bDamageFeedbackBound = false;
	bool bDamagePreview = false;
};
