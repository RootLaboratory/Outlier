#pragma once

#include "CoreMinimal.h"
#include "Drone/FlightMovementComponentBase.h"
#include "PartnerMovementComponent.generated.h"

class APartnerCharacter;
class AShooterCharacter;
enum class EPartnerMoveMode : uint8;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerMovementComponent : public UFlightMovementComponentBase
{
	GENERATED_BODY()

public:
	UPartnerMovementComponent();

	void RefreshCharacterRefsFromPlayerState();
	void RefreshMovementState();
	void ApplyCameraAssist();
	void StopCameraAssist();
	void SetSyncMove(bool SyncMove);
	void SetFreeMove(bool FreeMove);
	virtual void SetMoveInput(const FVector2D& MoveInput) override;
	virtual void SetVerticalInput(float Axis) override;
	void OnMoveModeChanged(EPartnerMoveMode NewMode);
	void ApplyPartnerFlightSettings();

protected:
	virtual void BeginPlay() override;
	virtual ACharacter* GetFlightOwnerCharacter() const override;
	virtual USceneComponent* GetFlightViewModelRoot() const override;
	virtual bool CanRunInputMovement() const override;
	virtual bool ShouldUpdateMovementFeel() const override;
	virtual EFlightInputMode GetFlightInputMode() const override;
	virtual void OnAfterInputMovement(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Partner|Move")
	float DirectionWeight = 500.0f;

private:
	bool IsAutoFollowMoveMode() const;
	void EnterSyncMove();
	void UpdateSyncMove(float DeltaTime);
	void UpdateCameraAssist(float DeltaTime);
	void MoveTowardTargetWithAvoidance(
		const FVector& TargetLocation,
		float DeltaTime,
		float InterpSpeed
	);
	FVector FindSimpleAvoidanceTarget(
		const FVector& CurrentLocation,
		const FVector& TargetLocation,
		const FHitResult& Hit
	);
	bool IsPathClear(const FVector& From, const FVector& To) const;

	UPROPERTY()
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> ShooterCharacter;
};
