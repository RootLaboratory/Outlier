#pragma once

#include "CoreMinimal.h"
#include "Drone/FlightMovementComponentBase.h"
#include "VECDroneMovementComponent.generated.h"

class AVECDrone;
class USceneComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UVECDroneMovementComponent : public UFlightMovementComponentBase
{
	GENERATED_BODY()

public:
	UVECDroneMovementComponent();

	void ApplyDroneMoveSpeed(float NewMoveSpeed);
	void ResetPoolMovementPresentation();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual ACharacter* GetFlightOwnerCharacter() const override;
	virtual USceneComponent* GetFlightVisualTiltRoot() const override;
	virtual USceneComponent* GetFlightViewModelRoot() const override;
	virtual bool CanRunInputMovement() const override;
	virtual bool ShouldUpdateMovementFeel() const override;
	virtual bool ShouldApplyMeshPitchTilt() const override;
	virtual EFlightInputMode GetFlightInputMode() const override;
	virtual void OnAfterInputMovement(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Facing")
	float AIFacingPitchInterpSpeed = 8.0f;

private:
	AVECDrone* GetVECDroneOwner() const;
	void UpdateAIFacingPitch(float DeltaTime);
	bool IsAIAimPresentationActive() const;
	void OnDiagnosticBonesFinalized();
	void LogClientAimPresentation();

	TWeakObjectPtr<USkeletalMeshComponent> DiagnosticMesh;
	TWeakObjectPtr<class USkeletalMesh> DiagnosticMeshAsset;
	FDelegateHandle DiagnosticBoneHandle;
	int32 DiagnosticBodyBone = INDEX_NONE;
	FVector DiagnosticBodyLocalForward = FVector::ForwardVector;
	uint64 DiagnosticBoneFrame = MAX_uint64;
	uint64 DiagnosticMovementFrame = MAX_uint64;
	double NextAimDiagnosticTime = 0.0;
	uint8 LastDiagnosticPhase = MAX_uint8;

	bool bAIFacingPitchInitialized = false;
	float NoiseFacingTiltWeight = 0.0f;
	float BaseNoiseFacingTiltPitch = 0.0f;
	FRotator BaseAIFacingPitchRotation = FRotator::ZeroRotator;
};
