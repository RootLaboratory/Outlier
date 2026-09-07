#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HackCircleBorderWidget.generated.h"

class UBorder;
class UMaterialInterface;

UCLASS(Blueprintable)
class OUTLIER_API UHackCircleBorderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void SetAngle(float NewAngleDegrees);

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void SetAmount(float NewAmount);

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void SetRotateEnabled(bool bNewRotate);

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void SetInnerRadius(float NewInnerRadius);

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void SetOuterRadius(float NewOuterRadius);

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void SetBrushMaterial(UMaterialInterface* NewMaterial);

	UFUNCTION(BlueprintCallable, Category = "Hack|CircleBorder")
	void RefreshMaterialParameters();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> BorderWidget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|CircleBorder")
	TObjectPtr<UMaterialInterface> BrushMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|CircleBorder")
	uint8 bRotate : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|CircleBorder")
	float AngleDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|CircleBorder")
	float Amount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|CircleBorder")
	float InnerRadius = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|CircleBorder")
	float OuterRadius = 0.48f;

private:
	void UpdateMaterialParameters();
	void UpdateRotationMaterialParameter();
	void UpdateGapSizeMaterialParameter();
	void UpdateInnerRadiusMaterialParameter();
	void UpdateOuterRadiusMaterialParameter();
	void InvalidateBorderPaint();
};
