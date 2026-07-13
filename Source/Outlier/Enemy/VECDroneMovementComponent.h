#pragma once

#include "CoreMinimal.h"
#include "Drone/FlightMovementComponentBase.h"
#include "VECDroneMovementComponent.generated.h"

class AVECDrone;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UVECDroneMovementComponent : public UFlightMovementComponentBase
{
	GENERATED_BODY()

public:
	UVECDroneMovementComponent();

	void ApplyDroneMoveSpeed(float NewMoveSpeed);

protected:
	virtual ACharacter* GetFlightOwnerCharacter() const override;
	virtual bool CanRunInputMovement() const override;
	virtual bool ShouldUpdateMovementFeel() const override;
	virtual EFlightInputMode GetFlightInputMode() const override;

private:
	AVECDrone* GetVECDroneOwner() const;
};
