#pragma once

#include "CoreMinimal.h"
#include "RecoilValues.generated.h"

class UCurveVector;

USTRUCT(BlueprintType)
struct OUTLIER_API FRecoilValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Curve")
	TObjectPtr<UCurveVector> RecoilCurveLoc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Curve")
	TObjectPtr<UCurveVector> RecoilCurveRot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Curve")
	float CurvePlayRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Aim")
	float AimAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amplitude")
	FVector RecoilAmplitudeLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amplitude")
	FVector RecoilAmplitudeRot = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocXMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocXMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocYMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocYMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocZMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocZMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotXMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotXMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotYMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotYMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotZMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotZMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Shot Direction")
	float DirectionLocYInfluence = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Shot Direction")
	float DirectionLocZInfluence = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Shot Direction")
	float DirectionPitchInfluence = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Shot Direction")
	float DirectionYawInfluence = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Shot Direction")
	float DirectionRollInfluence = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float CriticalDampingFactorLoc = 0.0f;			// 위치 반동이 흔들리다 멈추는 감쇠

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float CriticalDampingFactorRot = 0.0f;			// 회전 반동 감쇠

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float MassLoc = 0.0f;							// 위치 반동의 관성

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float MassRot = 0.0f;							// 회전 반동의 관성

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float StiffnessLoc = 0.0f;						// 위치 반동이 원래 위치로 돌아가려는 힘

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float StiffnessRot = 0.0f;						// 회전 반동이 원래 회전으로 돌아가려는 힘

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float TargetVelocityAmountLoc = 0.0f;			// 목표 위치 변화에 속도를 얼마나 반영할지

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float TargetVelocityAmountRot = 0.0f;			// 목표 회전 변화에 속도를 얼마나 반영할지
};
