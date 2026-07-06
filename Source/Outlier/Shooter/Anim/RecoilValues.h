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
	float CurvePlayRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Aim")
	float AimAlpha;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amplitude")
	FVector RecoilAmplitudeLoc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amplitude")
	FVector RecoilAmplitudeRot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocXMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocXMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocYMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocYMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocZMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Location")
	float RandomLocZMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotXMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotXMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotYMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotYMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotZMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Random Rotation")
	float RandomRotZMax;

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
	float CriticalDampingFactorLoc;					// 위치 반동이 흔들리다 멈추는 감쇠

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float CriticalDampingFactorRot;					// 회전 반동 감쇠

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float MassLoc;									// 위치 반동의 관성

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float MassRot;									// 회전 반동의 관성

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float StiffnessLoc;								// 위치 반동이 원래 위치로 돌아가려는 힘

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float StiffnessRot;								// 회전 반동이 원래 회전으로 돌아가려는 힘

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float TargetVelocityAmountLoc;					// 목표 위치 변화에 속도를 얼마나 반영할지

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil|Spring")
	float TargetVelocityAmountRot;					// 목표 회전 변화에 속도를 얼마나 반영할지
};
