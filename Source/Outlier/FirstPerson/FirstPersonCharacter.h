// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Weapon/WeaponBase.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Interface/RoomTagInterface.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "Interaction/InteractableComponent.h"
#include "FirstPersonCharacter.generated.h"

class USkeletalMeshComponent;
class UCameraComponent;
class USceneComponent;
class UFirstPersonInputConfig;
class UInputAction;
class USceneCaptureComponent2D;
class ULocalPlayerUILayerSubsystem;
struct FInputActionValue;
class URoomTagComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponChanged, EWeaponType, NewWeaponType);

UENUM(BlueprintType)
enum class EInteractionTraceMode : uint8
{
	LineTrace,
	SphereTrace
};

UCLASS()
class OUTLIER_API AFirstPersonCharacter : public ACharacter, public IGameplayTagProviderInterface, public IGenericTeamAgentInterface, public IRoomTagInterface
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	URoomTagComponent* RoomTagComponent;

	/** Input Config */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UFirstPersonInputConfig> InputConfig;

	/** Relevant AtLocation event requested by this owning client on Interaction input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Input", meta = (Categories = "Audio.Type"))
	FGameplayTag InteractionAudioEventTag;

	/** Runtime context supplied with the Interaction audio request. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Input", meta = (Categories = "Audio.Context"))
	FGameplayTagContainer InteractionAudioContextTags;

	/** Local 2D event played on Widget Escape input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Input", meta = (Categories = "Audio.Type"))
	FGameplayTag WidgetEscapeAudioEventTag;

	/** Runtime context supplied with the Widget Escape audio request. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Input", meta = (Categories = "Audio.Context"))
	FGameplayTagContainer WidgetEscapeAudioContextTags;

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

	virtual FGenericTeamId GetGenericTeamId() const override;

	virtual URoomTagComponent* GetRoomTagComp() const override;

	void TryInteract();
	void EndInteract();
	void NotifyHoldInteractCompleted(AActor* CompletedActor);
	void NotifyHoldInteractInvalidated(AActor* TargetActor);

	/** Builds and submits the owning-client world request bound to InteractionAudioEventTag. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool PlayInteractionRelevantAtLocationAudio();

	/** Builds and submits the local 2D request bound to WidgetEscapeAudioEventTag. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool PlayWidgetEscapeLocal2DAudio();

protected:

	virtual void TryStartAttack();

	virtual void TryStopAttack();

	void MoveInput(const FInputActionValue& Value);

	virtual void LookInput(const FInputActionValue& Value);

	virtual void DoMove(float Right, float Forward);

	void DoAim(float Yaw, float Pitch);

	void TryCamToggle();
	void HandleInteractionInputStarted();
	void HandleWidgetEscapeInput();
	void HandleWidgetConfirmedInput();

	virtual bool CanInteract() const;

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* TargetActor);

	UFUNCTION(Server, Reliable)
	void ServerBeginHoldInteract(AActor* TargetActor);

	UFUNCTION(Server, Reliable)
	void ServerCancelHoldInteract(AActor* TargetActor);

	UFUNCTION(Client, Reliable)
	void ClientOnInteractSucceeded(
		AActor* TargetActor,
		EInteractionFlowResult FlowResult);

	UFUNCTION(Client, Reliable)
	void ClientOnHoldInteractFailed(AActor* TargetActor);

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

	virtual FGameplayTag GetCurrentRoomTag() const override;

	virtual FGameplayTag GetDefaultRoomTag() const override;

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
	void CancelLocalHoldInteract(bool bNotifyServer);

	void UpdateInteractableFocus();

	void SetPartnerCameraCaptureUpdating(bool bEnabled);
	AFirstPersonCharacter* ResolvePartnerCameraSource() const;

	void SyncInteractableKeyWidgets(const TArray<AActor*>& CurrentInteractables);

	void GetInteractablesInRange(TArray<AActor*>& OutInteractables) const;

	AActor* FindInteractTargetByTrace() const;

	bool IsInteractTargetByTrace(AActor* TargetActor) const;

	void ArenaReload();
	ULocalPlayerUILayerSubsystem* GetUILayerSubsystem() const;

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

	bool bAwaitingHoldInteractResult = false;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> NearbyInteractables;

	uint8 bPartnerCameraCaptureActive : 1 = false;
	TWeakObjectPtr<AFirstPersonCharacter> ActivePartnerCameraSource;

	FTimerHandle InteractionTraceTimerHandle;

};
