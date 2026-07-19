// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Weapon/WeaponBase.h"
#include "GameplayTagContainer.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "FirstPersonCharacter.generated.h"

class USkeletalMeshComponent;
class UCameraComponent;
class USceneComponent;
class UFirstPersonInputConfig;
class UInputAction;
class USceneCaptureComponent2D;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponChanged, EWeaponType, NewWeaponType);

UENUM(BlueprintType)
enum class EInteractionTraceMode : uint8
{
	LineTrace,
	SphereTrace
};

UCLASS()
class OUTLIER_API AFirstPersonCharacter : public ACharacter, public IGameplayTagProviderInterface
{
	GENERATED_BODY()

protected:
	/** Pawn Mesh : first person view(arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First Person Camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCamera;

	/** First Person Camera Root */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USceneComponent* FirstPersonCameraRoot;

	/** Root used to keep first-person arms and weapon in camera space */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USceneComponent* FirstPersonViewModelRoot;

	/** Input Config */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UFirstPersonInputConfig> InputConfig;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractRange = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay Tags")
	FGameplayTagContainer OwnedQueryTags;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, EditAnywhere, Category = Weapon)
	AWeaponBase* CurrentWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	UPROPERTY(Transient)
	TObjectPtr<AWeaponBase> LastReplicatedWeapon;

	// Components / Owned Objects
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> CaptureComponent;

	UFUNCTION()
	void OnRep_CurrentWeapon();

public:
	/** Sets default values for this character's properties */
	AFirstPersonCharacter();

	void TryInteract();
	void EndInteract();
	void NotifyHoldInteractCompleted(AActor* CompletedActor);

protected:

	virtual void TryStartAttack();

	virtual void TryStopAttack();

	void MoveInput(const FInputActionValue& Value);

	virtual void LookInput(const FInputActionValue& Value);

	virtual void DoMove(float Right, float Forward);

	void DoAim(float Yaw, float Pitch);

	void TryCamToggle();

	virtual bool CanInteract() const;

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* TargetActor);

	UFUNCTION(Client, Reliable)
	void ClientOnInteractSucceeded(AActor* TargetActor);

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void BeginPlay() override;


public:
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCamera; }

	USceneComponent* GetFirstPersonCameraRoot() const { return FirstPersonCameraRoot; }

	USceneComponent* GetFirstPersonViewModelRoot() const { return FirstPersonViewModelRoot; }

	virtual void EquipWeapon(AWeaponBase* Weapon);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual FGameplayTagContainer GetOwnedGameplayTagsForQuery() const override;

	EWeaponType GetWeaponType() const;

	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	virtual void OnMoveInputUpdated(const FVector2D& MoveValue);

	void CaptureComponentWeaponNotIncluded(AWeaponBase* Weapon);

public:
	UPROPERTY(BlueprintAssignable)
	FOnWeaponChanged OnWeaponChanged;

private:
	float InteractionTraceInterval = 0.5f;

private:
	void UpdateInteractableFocus();

	void SetPartnerCameraCaptureUpdating(bool bEnabled);

	void SyncInteractableKeyWidgets(const TArray<AActor*>& CurrentInteractables);

	void GetInteractablesInRange(TArray<AActor*>& OutInteractables) const;

	AActor* FindInteractTargetByTrace() const;

	bool IsInteractTargetByTrace(AActor* TargetActor) const;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> InteractionTraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
	EInteractionTraceMode InteractionTraceMode = EInteractionTraceMode::LineTrace;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true", EditCondition = "InteractionTraceMode == EInteractionTraceMode::SphereTrace", ClampMin = "0.0"))
	float InteractionSphereTraceRadius = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Debug")
	bool bDrawInteractionTrace = false;

	UPROPERTY()
	AActor* FocusedInteractable = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> HoldingInteractActor;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> NearbyInteractables;

	uint8 bPartnerCameraCaptureActive : 1 = true;

	FTimerHandle InteractionTraceTimerHandle;

};
