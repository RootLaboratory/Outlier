// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Weapon/WeaponBase.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Interface/RoomTagInterface.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "PostProcess/OutlierStealthVisualTarget.h"
#include "Interaction/InteractableComponent.h"
#include "FirstPersonCharacter.generated.h"

class USkeletalMeshComponent;
class UCameraComponent;
class USceneComponent;
class UPrimitiveComponent;
class UFirstPersonInputConfig;
class UInputAction;
struct FInputActionValue;
class URoomTagComponent;
class ULocalPlayerSettingsSubsystem;
class AFirstPersonCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponChanged, EWeaponType, NewWeaponType);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSuitTransitionParticipantInvalidated, AFirstPersonCharacter*);

UENUM(BlueprintType)
enum class EInteractionTraceMode : uint8
{
	LineTrace,
	SphereTrace
};

UCLASS()
class OUTLIER_API AFirstPersonCharacter : public ACharacter, public IGameplayTagProviderInterface, public IGenericTeamAgentInterface, public IRoomTagInterface, public IOutlierStealthVisualTarget
{
	GENERATED_BODY()
	friend class FInteractionNodePresentationTest;
	friend class FInteractionNodeHoldAttemptTest;
	// 상호작용 세션은 캐릭터의 기존 입력 가능 판정을 재사용한다.
	friend class UInteractableComponent;

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

	/** Sphere overlap radius used to show nearby interaction widgets. */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction", meta = (DisplayName = "Interaction Widget Exposed Range", ClampMin = "0.0", Units = "cm"))
	float InteractionWidgetExposedRange = 100.0f;

	/** Outer detection radius; always at least 1 cm larger than the widget sphere. */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float OutlineSphereRange = 200.0f;

	/** Visible, available or hack-locked actors in the outer sphere, excluding the inner cache. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Interaction")
	TArray<TObjectPtr<AActor>> OutlineInteractables;

	/** 카메라에서 끝점까지의 거리. SphereTrace는 구 중심을 이 거리만큼 이동시키며, 구 반경은 끝점 바깥까지 확장된다. */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction", meta = (DisplayName = "Interaction Available Range", ClampMin = "0.0", Units = "cm"))
	float InteractionAvailableRange = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay Tags")
	FGameplayTagContainer OwnedQueryTags;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, EditAnywhere, Category = Weapon)
	AWeaponBase* CurrentWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	UPROPERTY(Transient)
	TObjectPtr<AWeaponBase> LastReplicatedWeapon;

	UFUNCTION()
	void OnRep_CurrentWeapon();

public:
	/** Sets default values for this character's properties */
	AFirstPersonCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual FGenericTeamId GetGenericTeamId() const override;

	virtual URoomTagComponent* GetRoomTagComp() const override;

	void TryInteract();
	void EndInteract();
	void NotifyHoldInteractCompleted(AActor* CompletedActor);
	void NotifyHoldInteractInvalidated(AActor* TargetActor);
	void CompleteDeferredInteraction(AActor* TargetActor, bool bSucceeded);
	bool IsSuitTransitionBlocked() const { return SuitTransitionBlockId.IsValid(); }
	bool AcquireSuitTransitionBlock(const FGuid& TransitionId);
	void ReleaseSuitTransitionBlock(const FGuid& TransitionId);
	FOnSuitTransitionParticipantInvalidated OnSuitTransitionParticipantInvalidated;

	/** Builds and submits the owning-client world request bound to InteractionAudioEventTag. */
	UFUNCTION(BlueprintCallable, Category = "Outlier|Audio")
	bool PlayInteractionRelevantAtLocationAudio();

protected:

	virtual void TryStartAttack();

	virtual void TryStopAttack();

	void MoveInput(const FInputActionValue& Value);

	virtual void LookInput(const FInputActionValue& Value);

	UFUNCTION()
	void HandleMouseSensitivityChanged(float NewValue);

	virtual void DoMove(float Right, float Forward);

	void DoAim(float Yaw, float Pitch);

	void HandleInteractionInputStarted();

	virtual bool CanInteract() const;
	virtual void PrepareForSuitTransition();
	virtual void OnSuitTransitionBlockChanged() {}
	virtual bool CanResumeMovementAfterSuitTransition() const { return true; }
	virtual void UnPossessed() override;

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* TargetActor, int32 HoldAttemptId = 0);

	UFUNCTION(Server, Reliable)
	void ServerBeginHoldInteract(AActor* TargetActor, int32 HoldAttemptId = 0);

	UFUNCTION(Server, Reliable)
	void ServerCancelHoldInteract(AActor* TargetActor, int32 HoldAttemptId = 0);

	UFUNCTION(Client, Reliable)
	void ClientOnInteractSucceeded(
		AActor* TargetActor,
		EInteractionFlowResult FlowResult,
		int32 HoldAttemptId = 0);

	UFUNCTION(Client, Reliable)
	void ClientOnHoldInteractFailed(AActor* TargetActor, int32 HoldAttemptId = 0);

	// 서버에서 적용된 피해를 로컬 HUD에 방향 정보와 함께 전달한다.
	UFUNCTION(Client, Reliable)
	void ClientShowDamageFeedback(FVector_NetQuantize DamageOrigin);

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif


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

	// IOutlierStealthVisualTarget : 은신 적용 대상 메시만 알려준다 ( 적용/복구는 UMaterialPostProcessSubsystem 담당 ).
	virtual void CollectStealthMeshes(
		TArray<UMeshComponent*>& OutFirstPersonMeshes,
		TArray<UMeshComponent*>& OutThirdPersonMeshes) const override;
	/** 부착 트리 밖에 생성한 시각 효과. 시퀀서 숨김/복원은 로컬 서브시스템이 담당한다. */
	virtual void CollectDetachedCinematicVisuals(TArray<USceneComponent*>& OutComponents) const {}

	virtual void OnMoveInputUpdated(const FVector2D& MoveValue);

public:
	UPROPERTY(BlueprintAssignable)
	FOnWeaponChanged OnWeaponChanged;

private:
	float InteractionTraceInterval = 0.1f;

private:
	void CancelLocalHoldInteract(bool bNotifyServer);
	UFUNCTION()
	void OnRep_SuitTransitionBlockId();
	UPROPERTY(ReplicatedUsing = OnRep_SuitTransitionBlockId)
	FGuid SuitTransitionBlockId;
	bool bSuitTransitionOwnsMovementStop = false;
	TEnumAsByte<EMovementMode> MovementModeBeforeSuitTransition = MOVE_None;
	uint8 CustomMovementModeBeforeSuitTransition = 0;

	void UpdateInteractableFocus();

	void SyncInteractableKeyWidgets(const TArray<AActor*>& CurrentInteractables);
	void SyncOutlineInteractables(const TArray<AActor*>& CurrentInteractables);
	void ClearInteractionDetection();

	void GetInteractablesInRange(TArray<AActor*>& OutInteractables) const;
	void GetInteractablesInSphere(float Range, bool bOutlineSphere, TArray<AActor*>& OutInteractables) const;
	void NormalizeInteractionSphereRanges();

	AActor* FindInteractTargetByTrace() const;

	bool IsInteractTargetByTrace(AActor* TargetActor) const;
	bool HasInteractionLineOfSight(AActor* TargetActor, const UPrimitiveComponent* TargetComponent) const;

	// void ArenaReload();

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> InteractionTraceChannel = ECC_Visibility;

	/** Wall/occluder query, independent of the interactable detection channel. */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> InteractionVisibilityTraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
	EInteractionTraceMode InteractionTraceMode = EInteractionTraceMode::LineTrace;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true", EditCondition = "InteractionTraceMode == EInteractionTraceMode::SphereTrace", ClampMin = "0.0"))
	float InteractionSphereTraceRadius = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Debug")
	bool bDrawInteractionTrace = false;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Debug")
	bool bDrawOutlineSphere = false;

	UPROPERTY()
	AActor* FocusedInteractable = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> HoldingInteractActor;

	bool bAwaitingHoldInteractResult = false;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> NearbyInteractables;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	UPROPERTY(Transient)
	float MouseSensitivity = 1.0f;

	FTimerHandle InteractionTraceTimerHandle;

};
