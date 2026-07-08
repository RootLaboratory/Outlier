#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Enemy/EnemyStat.h"
#include "Interface/HackableInterface.h"
#include "EnemyBase.generated.h"

class UStateTreeComponent;
class UCameraComponent;
class UHackableComponent;
class UInputAction;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EEnemyCombatState : uint8
{
	NonCombat,
	Alert,
	Combat,
	Stun
};

UCLASS()
class OUTLIER_API AEnemyBase : public ACharacter, public IHackableInterface
{
	GENERATED_BODY()

public:
	AEnemyBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Camera")
	TObjectPtr<UCameraComponent> EnemyCameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Hack")
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Data")
	FDataTableRowHandle EnemyStatRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Input")
	TObjectPtr<UInputAction> ReleasePossessionAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_RuntimeStat, Category = "Enemy|Data")
	FEnemyStat RuntimeStat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|Data")
	float CurrentHealth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	uint8 bInCombat : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	FVector LastKnownPlayerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	FVector PatternStartPlayerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	EEnemyCombatState CombatState = EEnemyCombatState::NonCombat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	uint8 bIsPossessed : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	uint8 bPlayerCurrentlyVisible : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|Room", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Room")
	int32 LastKnownArenaId = INDEX_NONE;

	UPROPERTY()
	TWeakObjectPtr<AController> CachedAIController;

	UPROPERTY()
	EEnemyCombatState PreStunCombatState = EEnemyCombatState::NonCombat;

public:
	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void SetEnemyPossessed(bool bNewIsPossessed);

	void ClearPossessedPlayerState();

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool IsEnemyPossessed() const { return bIsPossessed; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool IsInCombat() const { return bInCombat; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	EEnemyCombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	UStateTreeComponent* GetStateTreeComponent() const { return StateTreeComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Camera")
	UCameraComponent* GetEnemyCameraComponent() const { return EnemyCameraComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Data")
	const FEnemyStat& GetRuntimeStat() const { return RuntimeStat; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Data")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintCallable, Category = "Enemy|Data")
	void InitializeFromEnemyStatRow();

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void UpdateLastKnownPlayerLocation(const FVector& NewLocation);

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void SetPatternStartPlayerLocation(const FVector& NewLocation);

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void SetPlayerCurrentlyVisible(bool bNewVisible);

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool IsPlayerCurrentlyVisible() const { return bPlayerCurrentlyVisible; }

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void EnterCombat(const FVector& PlayerLocation);

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void EnterAlert(const FVector& PlayerLocation);

	void EnterCombatInArena(const FVector& PlayerLocation, int32 ArenaId, bool bPropagateToRoom);
	void EnterAlertInArena(const FVector& PlayerLocation, int32 ArenaId);

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void EnterStun();

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void RestoreStateAfterStun();

	UFUNCTION(BlueprintPure, Category = "Enemy|Possession")
	AController* GetCachedAIController() const { return CachedAIController.IsValid() ? CachedAIController.Get() : nullptr; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Room")
	FGameplayTag GetRoomTag() const { return RoomTag; }

	UFUNCTION(BlueprintCallable, Category = "Enemy|Damage")
	void ApplyDamageInternal(float DamageAmount);

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

protected:
	UFUNCTION()
	void OnRep_RuntimeStat();

	void SetDefaultEnemyType(EEnemyType EnemyType);
	virtual void ApplyClassStatOverrides();
	virtual void ApplyMovementFromRuntimeStat();
	void PromotePreStunState(EEnemyCombatState DetectedState);
	void HandleDeath();
	void HandleReleasePossessionInput(const FInputActionValue& Value);
};
