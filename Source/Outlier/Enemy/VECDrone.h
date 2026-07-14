#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "VECDrone.generated.h"

class UDroneInputConfig;
class UInputComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UVECDroneMovementComponent;
struct FInputActionValue;

UCLASS()
class OUTLIER_API AVECDrone : public AEnemyBase
{
	GENERATED_BODY()

public:
	AVECDrone();

	UFUNCTION(BlueprintPure, Category = "VECDrone|Camera")
	float GetCurrentCameraPitchDegrees() const;

	UFUNCTION(BlueprintPure, Category = "VECDrone|Camera")
	float GetCurrentCameraRollDegrees() const;

	UFUNCTION(BlueprintPure, Category = "VECDrone|Camera")
	float GetMaxCameraPitchDegrees() const;

	UFUNCTION(BlueprintPure, Category = "VECDrone|Camera")
	float GetMaxCameraRollDegrees() const;

	USceneComponent* GetFirstPersonCameraRoot() const { return FirstPersonCameraRoot; }
	USceneComponent* GetFirstPersonViewModelRoot() const { return FirstPersonViewModelRoot; }
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

protected:
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void ApplyMovementFromRuntimeStat() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	TObjectPtr<UVECDroneMovementComponent> VECMovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|FirstPerson")
	TObjectPtr<USceneComponent> FirstPersonCameraRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|FirstPerson")
	TObjectPtr<USceneComponent> FirstPersonViewModelRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|FirstPerson")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Input")
	TObjectPtr<UDroneInputConfig> InputConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Control")
	float LookSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Control")
	float LookInputDeadZone = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Control")
	float PitchMin = -80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Control")
	float PitchMax = 80.0f;

private:
	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);
	void VerticalMove(const FInputActionValue& Value);
	void StopVerticalMove();
};
