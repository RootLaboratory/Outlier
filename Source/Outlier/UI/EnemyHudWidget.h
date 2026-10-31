#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Enemy/EnemyHackEffectTypes.h"
#include "EnemyHudWidget.generated.h"

class AEnemyBase;
class UAbilitySystemComponent;
class ULocalPlayerPostProcessSubsystem;
class UTexture2D;
class UTextBlock;
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

	// Pop 직전에 PartnerPC가 부른다. 체력/시간 구독을 끊고 RDG 마스크를 지운다.
	void ReleaseDamageFeedback(bool bPreserveHackExpiry = false);

	// 서버의 복귀 통보가 Enemy 상태 복제보다 먼저 도착해도 마지막 숫자를 준비한다.
	void ShowHackTimerExpiry(AEnemyBase* Target);
	bool IsShowingHackTimerExpiry() const { return bHackTimerShowingExpiry; }
	FSimpleDelegate OnHackTimerExpiryDisplayFinished;

	// 사망 연출로 HUD가 숨겨진 동안 마스크 글리치도 멈춘다. 드러난 상태는 유지한다.
	void SetDamageFeedbackSuppressed(bool bSuppressed);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// WBP에서 두 TextBlock을 같은 이름으로 만들고 그 사이에 ':'를 배치한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UTextBlock> HackEffectMinutesText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UTextBlock> HackEffectSecondsText;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Enemy HUD|Hack Timer")
	FEnemyHackEffectTime HackEffectTime;

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

	void BindHackTimer();
	void ReleaseHackTimer();
	void RefreshHackTimer();
	void SetHackTimerTime(const FEnemyHackEffectTime& Time);
	void SetHackTimerVisible(bool bVisible);
	void HideExpiredHackTimer();

	UFUNCTION()
	void HandleHackEffectStarted(AEnemyBase* Target, float Duration);

	UFUNCTION()
	void HandleHackEffectEnded(AEnemyBase* Target, EEnemyHackEffectEndReason EndReason);

	UFUNCTION()
	void HandleHackTargetEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	FTimerHandle HackTimerRefreshHandle;
	FTimerHandle HackTimerHideHandle;
	uint64 HackTimerZeroFrame = 0;
	mutable bool bHackTimerExpiryPainted = false;
	bool bHackTimerBound = false;
	bool bHackTimerShowingExpiry = false;
	bool bHackTimerTimeInitialized = false;

	void BindDamageFeedback();
	void HandleVitalAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void RefreshDamageStage();
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
};
