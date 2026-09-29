// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PartnerHealthUI.h"
#include "PartnerLeftHudWidget.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Partner 왼쪽 HUD 전체를 소유한다.
 * 체력 표시는 PartnerHealthUI 구현을 재사용하고 Shooter 상태 표시를 함께 처리한다.
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerLeftHudWidget : public UPartnerHealthUI
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetShooterCondition(FGameplayTag InConditionTag);

	void RefreshShooterConditionUI();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateDistanceRatio(float InRatio);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ShooterConditionUI;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> Distance;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> Dist_Triangle;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> Signal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Distance")
	float DistanceTriangleOffsetX = 0.0f;

	// 0이면 Distance 이미지의 Canvas 너비를 이동 거리로 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Distance", meta = (ClampMin = "0.0"))
	float DistanceTriangleTravelWidth = 0.0f;

	// 캐릭터 중심 간 거리는 밀착해도 0이 아니므로, 이 비율부터 트랙의 왼쪽 끝으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Distance", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float DistanceTriangleNearRatio = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Distance")
	FName DistanceLimitParameterName = TEXT("flag");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Distance")
	TObjectPtr<UMaterialInterface> DistanceLimitMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	TObjectPtr<UMaterialInterface> ShooterConditionMaterial;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UIMaterial")
	TObjectPtr<UMaterialInstanceDynamic> ShooterConditionMID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FName ShooterConditionParameterName = TEXT("condition");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (Categories = "UI.Condition.Shooter"))
	FGameplayTag DefaultShooterConditionTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TMap<FGameplayTag, float> ConditionMaterialValues;

private:
	void InitializeShooterConditionMaterial();
	void UpdateShooterConditionMaterial();
	float GetShooterConditionMaterialValue(const FGameplayTag& InConditionTag) const;
	void UpdateDistanceTrianglePosition();
	void UpdateDistanceLimitVisual(bool bOverLimit);

	UPROPERTY(BlueprintReadOnly, Category = "Data", meta = (AllowPrivateAccess = "true", Categories = "UI.Condition.Shooter"))
	FGameplayTag CurrentShooterConditionTag;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DistanceLimitMID;

	FVector2D DistanceTriangleBasePosition = FVector2D::ZeroVector;
	float CurrentDistanceRatio = 0.0f;
	bool bDistanceTrianglePositionCached = false;
	bool bDistanceLimitOver = false;
	bool bDistanceLimitVisualInitialized = false;
};
