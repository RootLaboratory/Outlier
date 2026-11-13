// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PartnerHealthUI.h"
#include "PartnerLeftHudWidget.generated.h"

class UImage;
class URetainerBox;
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

	// 다운 상태: GrayoutRetainer 를 켜고, Partner 이미지는 같은 GrayAmount 로 시작해 Progress 에 따라 원본으로 복원된다.
	void SetKnockedDown(bool bInKnockedDown, float InGrayoutAmount);
	// StartWorldTime 은 World->GetTimeSeconds 기준. Duration 이 0 이면 Progress 0(회색 + 최대 왜곡)으로 유지한다.
	void SetKnockDownTiming(float StartWorldTime, float Duration);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Partner Image 를 제외한 Left HUD 전체를 감싼다. Effect Material 의 GrayAmount 를 코드에서 넣는다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<URetainerBox> GrayoutRetainer;

	// GrayoutRetainer 밖에 둔다. Brush 의 Material(Glitch/CA/색 복원)에 Progress, GrayAmount 를 넣는다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> Partner;

	// 경과 비율(선형)을 EaseInOut 으로 바꿔 Progress 에 넣는다. 1 = 선형, 클수록 시작 / 끝이 느리고 가운데가 빠르다.
	UPROPERTY(EditDefaultsOnly, Category = "KnockDown", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float KnockDownProgressEaseExponent = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category = "KnockDown")
	FName KnockDownProgressParameterName = TEXT("Progress");

	UPROPERTY(EditDefaultsOnly, Category = "KnockDown")
	FName GrayAmountParameterName = TEXT("GrayAmount");

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
	void ApplyKnockDownVisual();
	void UpdateKnockDownProgress();

	UPROPERTY(BlueprintReadOnly, Category = "Data", meta = (AllowPrivateAccess = "true", Categories = "UI.Condition.Shooter"))
	FGameplayTag CurrentShooterConditionTag;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DistanceLimitMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PartnerImageMID;

	bool bKnockedDown = false;
	float KnockDownGrayoutAmount = 0.0f;
	float KnockDownStartTime = 0.0f;
	float KnockDownDuration = 0.0f;

	FVector2D DistanceTriangleBasePosition = FVector2D::ZeroVector;
	float CurrentDistanceRatio = 0.0f;
	bool bDistanceTrianglePositionCached = false;
	bool bDistanceLimitOver = false;
	bool bDistanceLimitVisualInitialized = false;
};
