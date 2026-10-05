#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnemyHudWidget.generated.h"

class AEnemyBase;
class UAbilitySystemComponent;
class ULocalPlayerPostProcessSubsystem;
class UTexture2D;
struct FOnAttributeChangeData;

// Partner가 Enemy를 빙의한 동안 Partner MainWidget 대신 띄우는 HUD.
// PartnerPlayerController가 EnemyPossessed 진입 때 Push하고, PartnerControlled 복귀 때 Pop한다.
// 빙의 대상 체력이 20% 깎일 때마다 모서리 마스크 하나를 무작위로 골라 Drone Damage Feedback(RDG)에 드러낸다.
UCLASS(Abstract)
class OUTLIER_API UEnemyHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Push 직전에 빙의 대상을 넘긴다. 체력을 구독하고, 이미 깎인 만큼은 바로 드러낸다.
	void InitializeForEnemy(AEnemyBase* InEnemy);
	AEnemyBase* GetOwningEnemy() const;

	// Pop 직전에 PartnerPC가 부른다. 체력 구독을 끊고 RDG 마스크를 지운다.
	void ReleaseDamageFeedback();

	// 사망 연출로 HUD가 숨겨진 동안 마스크 글리치도 멈춘다. 드러난 상태는 유지한다.
	void SetDamageFeedbackSuppressed(bool bSuppressed);

protected:
	virtual void NativeDestruct() override;

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
