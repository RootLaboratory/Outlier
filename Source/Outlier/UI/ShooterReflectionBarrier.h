#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterReflectionBarrier.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS()
class OUTLIER_API UShooterReflectionBarrier : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Reflection Barrier")
	void Init();

	UFUNCTION(BlueprintCallable, Category = "Reflection Barrier")
	void PlayHitRipple(const FVector& IncomingOrigin);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> BarrierTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reflection Barrier|Material")
	TObjectPtr<UMaterialInterface> ActivationMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reflection Barrier|Animation", meta = (ClampMin = "0.0"))
	float ProgressDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Reflection Barrier|Animation")
	float TimeScale = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0", Units = "Seconds"))
	float RippleDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0"))
	float RippleTimeScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0"))
	float RippleRadius = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reflection Barrier|Ripple", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float RippleHardness = 100.0f;

private:
	void Update(float DeltaTime);
	void UpdateRipple(float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ActivationMaterialInstance;

	float Progress = 0.0f;
	bool bIsProgressUpdating = false;
	float RippleProgress = 1.0f;
	bool bIsRippleUpdating = false;
};
