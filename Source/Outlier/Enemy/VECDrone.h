#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "VECDrone.generated.h"

class UDroneInputConfig;
class UInputComponent;
class UVECDroneMovementComponent;
struct FInputActionValue;

UCLASS()
class OUTLIER_API AVECDrone : public AEnemyBase
{
	GENERATED_BODY()

public:
	AVECDrone();

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void ApplyMovementFromRuntimeStat() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	TObjectPtr<UVECDroneMovementComponent> VECMovementComponent;

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
