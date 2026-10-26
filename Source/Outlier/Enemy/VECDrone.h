#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "Interface/WeaponMuzzleProvider.h"
#include "VECDrone.generated.h"

class UDroneInputConfig;
class UAnimMontage;
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
	virtual float ReceiveOutlierDamage(const FOutlierDamageRequest& Request) override;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostNetReceive() override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void ApplyMovementFromRuntimeStat() override;
	virtual void ApplyExplosionReactionPresentation(const FVector& Direction, float ReactionScale) override;
	virtual void InterruptDamagePresentation(float BlendOutTime) override;
	virtual void OnPoolStateApplied(EEnemyPoolState PreviousState) override;
	virtual void ResetPoolPresentationState() override;
	virtual bool ShouldResetPoolMontages() const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Hit Reaction")
	TObjectPtr<UAnimMontage> HitReactionMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Hit Reaction", meta = (ClampMin = "0.05", UIMin = "0.05", Units = "s"))
	float HitReactionMinInterval = 0.2f;

	// 베이스는 GetMesh() 만 감춘다. 드론은 1인칭 메시도 같이 감춰야 한다.
	virtual void HideSourceMeshes() override;

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
	bool CanPlayHitReaction() const;
	void StopHitReaction(float BlendOutTime);
	bool AcceptHitReactionMessage(int32 Generation, int32 Lease, int64 Serial);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayHitReaction(int32 Generation, int32 Lease, int64 Serial);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStopHitReaction(int32 Generation, int32 Lease, int64 Serial, float BlendOutTime);

	double NextHitReactionTime = 0.0;
	// 재사용 때도 순번은 유지한다. 중단보다 늦게 도착한 이전 재생 요청을 폐기하기 위한 값이다.
	int64 HitReactionSerial = 0;
	int64 LastReceivedHitReactionSerial = 0;
	int32 PresentationGeneration = INDEX_NONE;
	int32 PresentationLease = INDEX_NONE;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FDroneHitReactionTestAccess;
#endif

	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);
	void VerticalMove(const FInputActionValue& Value);
	void StopVerticalMove();
	void StartAttackInput();
	void StopAttackInput();
};
