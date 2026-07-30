#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Enemy/EnemyStat.h"
#include "Interface/EmpableInterface.h"
#include "Interface/ScannableInterface.h"
#include "Interface/HackableInterface.h"
#include "Interface/RoomTagInterface.h"
#include "StateTreeReference.h"
#include "EnemyBase.generated.h"

class UStateTreeComponent;
class UCameraComponent;
class UHackableComponent;
class UInputAction;
class USphereComponent;
struct FInputActionValue;
class URoomTagComponent;
class ARangedWeaponBase;
class APartnerCharacter;

UENUM(BlueprintType)
enum class EEnemyCombatState : uint8
{
	NonCombat,
	Alert,
	Combat,
	Stun
};

UENUM(BlueprintType)
enum class EEnemyNonCombatBehavior : uint8
{
	Stationary,
	PatrolRoute
};

UENUM(BlueprintType)
enum class EEnemyAttackPhase : uint8
{
	Idle,
	Telegraph,
	Firing,
	Recover
};

UCLASS()
class OUTLIER_API AEnemyBase : public ACharacter, public IHackableInterface, public IEMPableInterface, public IScannableInterface, public IGenericTeamAgentInterface, public IRoomTagInterface
{
	GENERATED_BODY()

public:
	AEnemyBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void SendEnemyStateTreeEvent(FGameplayTag Tag);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	// 공용 StateTree의 Enemy.StateTree.Battle Linked Asset을 개체 유형별 전투 Tree로 교체한다.
	// Enemy BP에서 StateTree 에셋과 노출 파라미터를 함께 지정할 수 있다.
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "Enemy|AI",
		meta = (Schema = "/Script/Outlier.EnemyStateTreeSchema", SchemaCanBeOverriden))
	FStateTreeReference BattleStateTreeReference;

	// Enemy BP에서 비전투 시 제자리 경계와 경로 순찰 중 사용할 행동을 선택한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|AI")
	EEnemyNonCombatBehavior NonCombatBehavior = EEnemyNonCombatBehavior::Stationary;

	// PatrolPointA는 스폰 위치를 사용하고, B는 이 로컬 오프셋을 적용한 월드 위치로 계산한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI|Patrol", meta = (MakeEditWidget))
	FVector PatrolPointBLocalOffset = FVector(1000.0f, 0.0f, 0.0f);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|AI|Patrol")
	FVector PatrolPointA = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy|AI|Patrol")
	FVector PatrolPointB = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Camera")
	TObjectPtr<UCameraComponent> EnemyCameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Hack")
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|EMP")
	TObjectPtr<UEMPableComponent> EmpableComponent;

	// Physics Asset 바디 대신 이 전용 컴포넌트로 코어 크리티컬을 판정함 — CoreBone 바디가 BodyBone 콜리전
	// 안쪽에 겹쳐 있으면 같은 컴포넌트 안에서 가려져서 Multi 트레이스로도 검출이 안 되는 문제가 있었음
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Damage")
	TObjectPtr<USphereComponent> CoreHitboxComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Data")
	FDataTableRowHandle EnemyStatRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Input")
	TObjectPtr<UInputAction> ReleasePossessionAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	URoomTagComponent* RoomTagComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_RuntimeStat, Category = "Enemy|Data")
	FEnemyStat RuntimeStat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentHealth, Category = "Enemy|Data")
	float CurrentHealth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Data")
	float CoreCriticalMultiplier = 2.0f;

	// CoreHitboxComponent를 붙일 소켓/본 이름
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Damage")
	FName CoreBoneName = TEXT("CoreBone");

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
	uint8 bPossessionInProgress : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	uint8 bPlayerCurrentlyVisible : 1 = false;

	// 같은 방의 다른 Enemy가 직접 Sight로 보고한 전투 대상 좌표.
	// 이 값만 수신한 Enemy는 직접 관측자로 취급하지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	uint8 bHasSharedTargetContact : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|State")
	FVector SharedTargetLocation = FVector::ZeroVector;

	// 서버의 공격 사이클 상태만 복제하고 실제 사운드/VFX 선택은 Enemy BP가 담당한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_AttackPhase, Category = "Enemy|Weapon")
	EEnemyAttackPhase AttackPhase = EEnemyAttackPhase::Idle;

	// Enemy BP에서 ARangedWeaponBase 파생 무기 BP를 지정한다.
	// 서버 BeginPlay에서 한 번 스폰하며 CurrentWeapon으로 복제한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon")
	TSubclassOf<ARangedWeaponBase> DefaultWeaponClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy|Weapon")
	TObjectPtr<ARangedWeaponBase> CurrentWeapon;

	// 스폰된 무기 Actor를 Enemy Mesh에 부착할 소켓. 무기 내부 1P/3P 메시 표현은 무기 BP가 담당한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon")
	FName WeaponSocketName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Room")
	int32 LastKnownArenaId = INDEX_NONE;

	UPROPERTY()
	TWeakObjectPtr<AController> CachedAIController;

	UPROPERTY()
	TWeakObjectPtr<APartnerCharacter> PossessionInstigatorPartner;

	uint8 bPossessedAttackHeld : 1 = false;
	uint8 bPossessedAttackQueued : 1 = false;

	UPROPERTY()
	EEnemyCombatState PreStunCombatState = EEnemyCombatState::NonCombat;

public:
	virtual FGenericTeamId GetGenericTeamId() const override;

	virtual FGameplayTag GetCurrentRoomTag() const override;

	virtual FGameplayTag GetDefaultRoomTag() const override;

	virtual URoomTagComponent* GetRoomTagComp() const override;

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void SetEnemyPossessed(bool bNewIsPossessed);

	void CancelPossessionProcess();

	void ClearPossessedPlayerState();

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool IsEnemyPossessed() const { return bIsPossessed; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool IsPossessionInProgress() const { return bPossessionInProgress; }

	bool IsAIControlSuppressed() const { return bIsPossessed || bPossessionInProgress; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool IsInCombat() const { return bInCombat; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	EEnemyCombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	EEnemyNonCombatBehavior GetNonCombatBehavior() const { return NonCombatBehavior; }

	UFUNCTION(BlueprintPure, Category = "Enemy|AI|Patrol")
	FVector GetPatrolPointA() const { return PatrolPointA; }

	UFUNCTION(BlueprintPure, Category = "Enemy|AI|Patrol")
	FVector GetPatrolPointB() const { return PatrolPointB; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	FVector GetLastKnownPlayerLocation() const { return LastKnownPlayerLocation; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	FVector GetPatternStartPlayerLocation() const { return PatternStartPlayerLocation; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	int32 GetLastKnownArenaId() const { return LastKnownArenaId; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Weapon")
	ARangedWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Weapon")
	EEnemyAttackPhase GetAttackPhase() const { return AttackPhase; }

	bool IsPossessedAttackHeld() const { return bPossessedAttackHeld; }
	bool HasPossessedAttackQueued() const { return bPossessedAttackQueued; }
	bool HasPossessedAttackRequest() const
	{
		return bPossessedAttackHeld || bPossessedAttackQueued;
	}
	bool ConsumePossessedAttackRequest();

	// StateTree 공격 Task가 서버에서 호출한다. 클라이언트 BP에는 OnAttackPhaseChanged로 전달된다.
	void SetAttackPhase(EEnemyAttackPhase NewPhase);

	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Weapon")
	void OnAttackPhaseChanged(EEnemyAttackPhase PreviousPhase, EEnemyAttackPhase NewPhase);

	// AI/StateTree용 서버 권한 공격 API.
	// Target 버전은 Actor의 현재 위치를 사용하고 Location 버전은 LKP 위협 사격처럼 좌표만 조준한다.
	bool StartAttackTarget(AActor* TargetActor);
	bool StartAttackLocation(const FVector& TargetLocation);
	bool StartPossessedAttackBurst();

	// 반복 공격 중 조준 방향만 갱신한다. 무기 발사 주기 자체는 ARangedWeaponBase가 관리한다.
	bool UpdateAttackLocation(const FVector& TargetLocation);
	void StopCurrentWeaponAttack();
	void StopCurrentAttack();

	// FireCycle이 타겟 상실과 같은 틱에 실패 전이를 처리해 Battle 전환 이벤트를 놓친 경우,
	// 다음 틱에 현재 가시성/공유 접촉 상태에 맞는 전투 결정을 다시 요청한다.
	void RequestCombatDecisionRefresh();

	// 재감지, 스턴, 빙의, 사망, 방 이동 시 방 Subsystem에 보관된 수색 슬롯을 반환한다.
	void ReleaseSearchRingSlot();

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

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	bool HasSharedTargetContact() const { return bHasSharedTargetContact; }

	UFUNCTION(BlueprintPure, Category = "Enemy|State")
	FVector GetSharedTargetLocation() const { return SharedTargetLocation; }

	// RoomSubsystem만 호출하는 서버 권한 공유 접촉 API.
	void ApplySharedTargetContact(const FVector& TargetLocation);
	void ClearSharedTargetContact();

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void EnterCombat(const FVector& PlayerLocation);

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void EnterAlert(const FVector& PlayerLocation);

	// Enemy Resolve Alert Task가 제한 시간 충족 시 호출하는 서버 확정 API.
	// 상태 변경 후 Combat.Entered 또는 Combat.AlertCleared 이벤트로 StateTree 전환을 요청한다.
	bool CommitAlertToCombat();
	bool CommitAlertToNonCombat();

	void EnterCombatInArena(
		const FVector& PlayerLocation,
		int32 ArenaId,
		bool bPropagateToRoom,
		bool bDeferStateTreeEvent = false);
	void EnterAlertInArena(const FVector& PlayerLocation, int32 ArenaId);

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void EnterStun();

	UFUNCTION(BlueprintCallable, Category = "Enemy|State")
	void RestoreStateAfterStun();

	UFUNCTION(BlueprintPure, Category = "Enemy|Possession")
	AController* GetCachedAIController() const { return CachedAIController.IsValid() ? CachedAIController.Get() : nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Enemy|Damage")
	void ApplyDamageInternal(float DamageAmount, bool bIsCoreHit);

	UFUNCTION(BlueprintPure, Category = "Enemy|Damage")
	USphereComponent* GetCoreHitboxComponent() const { return CoreHitboxComponent; }

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;
	virtual void HandleHackStarted(const FHackQueryContext& Context) override;
	virtual void HandleHackCompleted(const FHackResultContext& Context) override;

	virtual UEMPableComponent* GetEMPableComponent() const override;
	virtual void HandleEMPStarted(FGameplayTag EffectTag) override;
	virtual void HandleEMPEnded(FGameplayTag EffectTag) override;

	virtual int32 GetScanStencilValue() const override;
protected:
	UFUNCTION()
	void OnRep_RuntimeStat();

	UFUNCTION()
	void OnRep_CurrentHealth(float PreviousHealth);

	void HandleCurrentHealthChanged(float PreviousHealth);

	void SendEnemyStateTreeEventNextTick(FGameplayTag Tag);
	void SetDefaultEnemyType(EEnemyType EnemyType);
	virtual void ApplyClassStatOverrides();
	virtual void ApplyMovementFromRuntimeStat();
	bool HasActiveStunTag() const;
	bool BeginPossessionProcess(APartnerCharacter* PartnerCharacter);
	void ConfirmPossessionProcess();
	void PromotePreStunState(EEnemyCombatState DetectedState);
	void RefreshPerceptionConfigForCurrentState();
	void RefreshPerceptionTeamRegistration();

	UFUNCTION()
	void OnRep_AttackPhase(EEnemyAttackPhase PreviousPhase);

	// 서버에서 기본 무기를 스폰하고 Enemy 소켓에 장착한다.
	void EquipDefaultWeapon();

	// 방이 바뀌면 이전 방 기준으로 받은 수색 슬롯을 즉시 반환한다.
	void HandleCurrentRoomTagChanged(FGameplayTag PreviousRoomTag, FGameplayTag NewRoomTag);
	void RemoveRoomTargetObserver();
	virtual void HandleDeath();

	bool bCombatDecisionRefreshPending = false;

	// 빙의된 VEC의 AttackAction 입력 진입점.
	// 소유 클라이언트는 시작/종료 상태만 RPC로 보내고 실제 발사는 서버 무기가 수행한다.
	void HandleStartAttackInput();
	void HandleStopAttackInput();
	void HandleReleasePossessionInput(const FInputActionValue& Value);
	void SetPossessedAttackHeld(bool bHeld);
	void ResetPossessedAttackInput();

	// 서버는 bIsPossessed를 다시 확인한 뒤 CurrentWeapon을 실행한다.
	UFUNCTION(Server, Reliable)
	void ServerStartWeaponAttack();

	UFUNCTION(Server, Reliable)
	void ServerStopWeaponAttack();
};
