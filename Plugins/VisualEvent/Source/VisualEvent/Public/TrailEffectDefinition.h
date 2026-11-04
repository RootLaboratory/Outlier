// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TrailEffectDefinition.generated.h"

class UFXSystemAsset;

UENUM(BlueprintType)
enum class ETrailTriggerMode : uint8
{
	// 시스템에 User.Trigger 가 있으면 true, 없으면 건드리지 않는다.
	Auto,
	// 항상 true. 시스템에 Trigger 가 없으면 경고를 한 번 남긴다.
	Always,
	// 항상 false. Trigger 가 있는 시스템이라도 발사하지 않는다.
	Never,
};

// 일단 전역 scale 기반으로 처리했는데 sprite renderer 기반의 vfx에는 쓸모 없다는 걸 알았음.

/**
 * 
 */
UCLASS(Blueprintable)
class VISUALEVENT_API UTrailEffectDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	TObjectPtr<UFXSystemAsset> FXAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	FVector Scale = FVector(1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FX")
	FRotator RotationOffset = FRotator::ZeroRotator;

	// 시점별 크기 보정. Scale 에 곱해진다.
	// 1인칭은 카메라가 총구 바로 뒤라 3인칭과 같은 크기면 화면을 가린다.
	// 시점 구분이 있는 경로( 머즐 )에서만 쓰이고, Beam / Projectile 경로는 무시한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Viewpoint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FirstPersonScaleMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Viewpoint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ThirdPersonScaleMultiplier = 1.0f;

	// 시점에 맞는 최종 스케일. 호출자는 시점만 넘기고 값은 이 에셋이 정한다.
	FVector GetViewpointScale(bool bFirstPerson) const
	{
		const float Multiplier = bFirstPerson
			? FirstPersonScaleMultiplier
			: ThirdPersonScaleMultiplier;
		return Scale * FMath::Max(Multiplier, 0.0f);
	}

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FName StartParameterName = TEXT("User.Start");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FName EndParameterName = TEXT("User.End");

	// Lyra 트레이서처럼 NM_WeaponTrigger 로 발사되는 시스템의 User.Trigger( bool ) 처리.
	// 보통은 Auto 로 두면 시스템에 Trigger 가 있을 때 알아서 true 를 넣는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	ETrailTriggerMode TriggerMode = ETrailTriggerMode::Auto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FVector StartOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beam")
	FVector EndOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FRotator RelativeRotation = FRotator::ZeroRotator;
};
