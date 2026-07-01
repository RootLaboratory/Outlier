#pragma once

#include "CoreMinimal.h"
#include "ThirdPersonProceduralAnimRuntime.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FThirdPersonProceduralAnimRuntime
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float AimYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float AimPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float AimAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upper Body")
	float UpperBodyAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turn In Place")
	float TurnInPlaceAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turn In Place")
	float TurnInPlaceYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turn In Place")
	float TurnInPlaceDirection = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turn In Place")
	float TurnInPlacePlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	float LeanAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand IK")
	FTransform LeftHandIKTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand IK")
	float LeftHandIKAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	float FireAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	float ReloadAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	float SprintAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	float SlideAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	FVector WallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	FRotator WallOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallOffsetAlpha = 0.0f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bIsCrouching : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bIsAiming : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bIsReloading : 1 = false;

	void Reset()
	{
		AimYaw = 0.0f;
		AimPitch = 0.0f;
		AimAlpha = 0.0f;
		UpperBodyAlpha = 0.0f;
		TurnInPlaceAlpha = 0.0f;
		TurnInPlaceYaw = 0.0f;
		TurnInPlaceDirection = 0.0f;
		TurnInPlacePlayRate = 1.0f;
		LeanAlpha = 0.0f;
		LeftHandIKTransform = FTransform::Identity;
		LeftHandIKAlpha = 0.0f;
		FireAlpha = 0.0f;
		ReloadAlpha = 0.0f;
		SprintAlpha = 0.0f;
		SlideAlpha = 0.0f;
		WallOffsetLoc = FVector::ZeroVector;
		WallOffsetRot = FRotator::ZeroRotator;
		WallOffsetAlpha = 0.0f;
		bIsCrouching = false;
		bIsAiming = false;
		bIsReloading = false;
	}
};
