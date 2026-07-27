#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "Interface/WeaponMuzzleProvider.h"
#include "VECDrone.generated.h"

class UDroneInputConfig;
class UInputComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UVECDroneMovementComponent;
struct FInputActionValue;

UCLASS()
class OUTLIER_API AVECDrone : public AEnemyBase, public IWeaponMuzzleProvider
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
	USceneComponent* GetAIFacingPitchRoot() const { return AIFacingPitchRoot; }
	USceneComponent* GetThirdPersonTiltRoot() const { return ThirdPersonTiltRoot; }
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	virtual USkeletalMeshComponent* GetWeaponMuzzleComponent(bool bFirstPerson) const override
	{
		return bFirstPerson ? FirstPersonMesh.Get() : GetMesh();
	}

	virtual FName GetWeaponMuzzleSocketName(bool bFirstPerson) const override
	{
		return bFirstPerson ? FirstPersonWeaponMuzzleSocketName : ThirdPersonWeaponMuzzleSocketName;
	}

	virtual void GetWeaponMuzzleSocketNames(bool bFirstPerson, TArray<FName>& OutSocketNames) const override;

protected:
	virtual void BeginPlay() override;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Visual")
	TObjectPtr<USceneComponent> AIFacingPitchRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Visual")
	TObjectPtr<USceneComponent> ThirdPersonTiltRoot;

	// VEC 무기 외형은 본체 메시 안에 있으므로 BP에서 지정한 본체 소켓을 총구 기준으로 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon|Presentation")
	FName FirstPersonWeaponMuzzleSocketName = TEXT("Muzzle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon|Presentation")
	FName ThirdPersonWeaponMuzzleSocketName = TEXT("Muzzle");

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
	void StartAttackInput();
	void StopAttackInput();
};
