#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterReflectionBarrier.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// 피격 리플 1개분. 머티리얼에는 Ripple0~3 = (u, v, 경과 초, 진행도) 로 넘어간다.
struct FShooterReflectionRippleSlot
{
	FVector2D CenterUV = FVector2D(0.5, 0.5);
	float ElapsedSeconds = 0.0f;
	bool bActive = false;
};

UCLASS()
class OUTLIER_API UShooterReflectionBarrier : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:
	// 머티리얼 Custom 노드의 Ripple0~3 입력 개수와 같아야 한다.
	static constexpr int32 MaxRippleSlots = 4;

	void Init();
	void PlayHitRipple(const FVector& IncomingOrigin);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> BarrierTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Reflection Barrier|Material")
	TObjectPtr<UMaterialInterface> ActivationMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Reflection Barrier|Animation", meta = (ClampMin = "0.0"))
	float ProgressDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Reflection Barrier|Animation")
	float TimeScale = 0.4f;

	// 리플 하나가 끝나기까지의 시간. 형태 튜닝값(세기 / 반경 / 탄성 등)은 머티리얼 파라미터에 있다.
	UPROPERTY(EditDefaultsOnly, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0", Units = "Seconds"))
	float RippleDuration = 0.65f;

	UPROPERTY(EditDefaultsOnly, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0"))
	float RippleTimeScale = 1.0f;

	// 화면 밖 허용 폭(UV). 투영 좌표가 [-Margin, 1 + Margin] 밖이면 리플을 재생하지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0"))
	float RippleScreenMargin = 0.1f;

private:
	void Update(float DeltaTime);
	void UpdateRipples(float DeltaTime);
	void UpdateAspectRatio(const FGeometry& MyGeometry);
	void PushRippleSlot(int32 SlotIndex);
	int32 FindRippleSlotForNewHit() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ActivationMaterialInstance;

	float Progress = 0.0f;
	bool bIsProgressUpdating = false;

	FShooterReflectionRippleSlot RippleSlots[MaxRippleSlots];
	bool bIsRippleUpdating = false;
	float CachedAspectRatio = 0.0f;
};
