#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Room/RoomCombatSubsystem.h"
#include "Level/Elevator/ElevatorShaftMotionProfile.h"
#include "ElevatorShaftController.generated.h"

class AElevatorShaftSection;
class AInteractableDoor;
class AFirstPersonPlayerController;
class AFirstPersonCharacter;
class AOutlierPlayerState;
class AOutlierGameMode;
class ARoomVolume;
class UBoxComponent;
class UOutlierArenaSubsystem;
class AGameStateBase;
class UCurveFloat;
class UAudioComponent;

UENUM(BlueprintType)
enum class EElevatorShaftState : uint8
{
	WaitingForPair,
	ClosingEntrance,
	Intro,
	AwaitingSpawn,
	RisingCombat,
	TransitionOut,
	Completed,
	FinishingCurrentSection,
	PreparingArrival,
	Arriving,
	Decelerating
};

// 위치 대신 실행 기준만 복제한다. 늦게 보이는 클라이언트도 같은 서버 시각에서 복원한다.
USTRUCT()
struct FElevatorShaftMotion
{
	GENERATED_BODY()

	UPROPERTY()
	EElevatorShaftState State = EElevatorShaftState::WaitingForPair;

	UPROPERTY()
	FGuid ExecutionId;

	UPROPERTY()
	uint32 Generation = 0;

	UPROPERTY()
	double StartServerTime = 0;

	UPROPERTY()
	double StoppedDistance = 0;

	UPROPERTY()
	float Speed = 250;

	UPROPERTY()
	bool bStarted = false;

	UPROPERTY()
	bool bArrival = false;

	UPROPERTY()
	bool bLanded = false;

	UPROPERTY()
	double AccelerationSeconds = 3.5;

	UPROPERTY()
	double DecelerationSeconds = 3.5;

	UPROPERTY()
	double FinishDistance = 0;

	UPROPERTY()
	double ArrivalStartTime = 0;

	UPROPERTY()
	double DepartureElapsed = 0;

	UPROPERTY()
	double ArrivalDistance = 0;

	UPROPERTY()
	double BrakeStartSeconds = 0;

	UPROPERTY()
	FTransform ArrivalTransform;

	UPROPERTY()
	FTransform EntranceTransform;
};

UCLASS()
class OUTLIER_API AElevatorShaftController : public AActor
{
	GENERATED_BODY()

public:
	AElevatorShaftController();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버가 소유한 시작 연출만 이 토큰으로 완료한다. 이전 재시도의 완료는 무시한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Elevator")
	void FinishIntro(FGuid ExecutionId);

	bool AreArrivalVisualsReady(const FGuid& ExecutionId);
	bool IsArrivalPawnReady(const APawn* Pawn) const;
	void NotifyClientArrivalReady(AFirstPersonPlayerController* Player, const FGuid& ExecutionId);

	// BP에서 생성한 운행음을 등록하면 완료/취소 시 이 실행의 소리만 정리한다.
	UFUNCTION(BlueprintCallable, Category = "Elevator|Audio")
	void RegisterMotionAudioComponent(UAudioComponent* Component);

	void NotifyClientFadeFinished(AFirstPersonPlayerController* Player, const FGuid& ExecutionId, bool bReveal);

	UFUNCTION(BlueprintPure, Category = "Elevator")
	EElevatorShaftState GetShaftState() const { return Motion.State; }

	// 큰 시간 간격에도 층 간격을 유지하며, 첫 입구 슬롯에는 반복 벽을 겹쳐 넣지 않는다.
	static double CalculateLoopOffset(double Distance, int32 Slot, int32 Count, double Height);

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Setup")
	TObjectPtr<ARoomVolume> EntryRoom;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Setup")
	FGameplayTag CombatRoomTag;

	// 내부 문 BP를 공유해도 저장 ID는 레벨에 배치한 엘리베이터마다 고유하게 지정한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Setup")
	FName EntranceDoorId = NAME_None;

	// Controller BP에 배치한 Section Child Actor와 그 안의 문에서 자동 연결한다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Elevator|Setup")
	TObjectPtr<AElevatorShaftSection> EntranceSection;

	// 실제 문은 서버 Actor다. 중첩 Child Actor보다 늦게 복제돼도 동일한 문을 연결한다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_Motion, Category = "Elevator|Setup")
	TObjectPtr<AInteractableDoor> EntranceDoor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Setup")
	TSubclassOf<AElevatorShaftSection> LoopSectionClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Setup", meta = (ClampMin = "3"))
	int32 LoopSectionCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Setup", meta = (ClampMin = "1", Units = "cm/s"))
	float RiseSpeed = 250;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Elevator|Motion")
	TObjectPtr<UCurveFloat> AccelerationCurve;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Elevator|Motion")
	TObjectPtr<UCurveFloat> DecelerationCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Motion", meta = (ClampMin = "0.01", Units = "s"))
	float AccelerationDuration = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Motion", meta = (ClampMin = "0.01", Units = "s"))
	float DecelerationDuration = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Motion", meta = (ClampMin = "0.1", Units = "s"))
	float ArrivalVisibleDuration = 0.5f;

	// 이 Box는 입구만 막는다. 샤프트 전체의 고정 외벽은 별도 단순 충돌로 제작한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elevator|Setup")
	TObjectPtr<UBoxComponent> EntranceBlocker;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Exit")
	TObjectPtr<AActor> ShooterExitTarget;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Exit")
	TObjectPtr<AActor> PartnerExitTarget;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Exit")
	TObjectPtr<ARoomVolume> ExitRoom;

	// 레벨에 최종 착층 위치로 배치한 별도 Section. 출발 Child Actor와 분리한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Motion, Category = "Elevator|Exit")
	TObjectPtr<AElevatorShaftSection> ArrivalSection;

	// 비워두면 문 자체 ID를 사용한다. 지정하면 도착 문이 저장 등록 전에 이 값을 읽는다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Elevator|Exit")
	FName ArrivalDoorId = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_Motion, Category = "Elevator|Exit")
	TObjectPtr<AInteractableDoor> ArrivalDoor;

	// 출구 문 앞에 고정 배치한다. 연출 중 이동하는 문 대신 안전을 담당한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elevator|Exit")
	TObjectPtr<UBoxComponent> ArrivalBlocker;

	// 충돌이 꺼져도 위치를 확인할 수 있도록 박스 윤곽을 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Debug")
	bool bShowBlockers = true;

	void UpdateBlockerVisibility();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Presentation")
	bool bPlayIntro = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Presentation", meta = (ClampMin = "0"))
	float FadeOutDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Presentation", meta = (ClampMin = "0"))
	float FadeInDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Presentation", meta = (ClampMin = "1"))
	float TransitionTimeout = 10;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintImplementableEvent, Category = "Elevator")
	void PlayIntroOnServer(FGuid ExecutionId);

	UFUNCTION(BlueprintImplementableEvent, Category = "Elevator")
	void ShaftStateChanged(EElevatorShaftState State);

	UFUNCTION(BlueprintImplementableEvent, Category = "Elevator|Audio")
	void OnAccelerationStarted(FGuid ExecutionId, int32 SegmentId, double StartServerTime, double ElapsedSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Elevator|Audio")
	void OnCruiseStarted(FGuid ExecutionId, int32 SegmentId, double StartServerTime, double ElapsedSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Elevator|Audio")
	void OnDecelerationStarted(FGuid ExecutionId, int32 SegmentId, double StartServerTime, double ElapsedSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Elevator|Audio")
	void MotionPresentationStopped(FGuid ExecutionId);

private:
	// 게임플레이 상태는 서버가 확정하고, 반복 층은 각 클라이언트가 같은 기준으로 움직인다.
	UPROPERTY(ReplicatedUsing = OnRep_Motion)
	FElevatorShaftMotion Motion;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AElevatorShaftSection>> LoopSections;

	TWeakObjectPtr<URoomCombatSubsystem> Combat;
	TWeakObjectPtr<UOutlierArenaSubsystem> Arena;
	TWeakObjectPtr<AOutlierGameMode> GameMode;
	TWeakObjectPtr<AGameStateBase> Clock;
	TWeakObjectPtr<AFirstPersonPlayerController> ShooterController;
	TWeakObjectPtr<AFirstPersonPlayerController> PartnerController;
	TArray<TWeakObjectPtr<AOutlierPlayerState>> ObservedPlayers;
	FRoomCombatTriggerContext CombatContext;
	FTimerHandle RetryTimer;
	FTimerHandle FadeTimeoutTimer;
	FTimerHandle FadeMinimumTimer;
	FTimerHandle EntryCheckTimer;
	FTimerHandle VisualReadyTimer;
	FTimerHandle MotionPhaseTimer;
	FElevatorShaftSpeedProfile AccelerationProfile;
	FElevatorShaftSpeedProfile DecelerationProfile;
	bool bProfilesReady = false;
	bool bShooterArrivalReady = false;
	bool bPartnerArrivalReady = false;
	bool bRestoreCleared = false;
	bool bPairTeleported = false;
	FTransform PreviousShooterTransform;
	FTransform PreviousPartnerTransform;
	FRotator PreviousShooterView;
	FRotator PreviousPartnerView;
	FGuid PresentationExecutionId;
	uint8 PresentedSegments = 0;
	TArray<TWeakObjectPtr<UAudioComponent>> MotionAudioComponents;

	// 암전 한 건이 획득한 입력 잠금과 두 소유 클라이언트의 응답만 관리한다.
	TWeakObjectPtr<AFirstPersonCharacter> BlockedShooter;
	TWeakObjectPtr<AFirstPersonCharacter> BlockedPartner;
	FGuid InputBlockId;
	double FadeRequestTime = 0;
	bool bRevealRequested = false;
	bool bShooterFadeReady = false;
	bool bPartnerFadeReady = false;
	bool bInitialized = false;
	FTransform DepartureControllerTransform;
	FTransform ControllerSectionOffset;
	bool bControllerAtArrival = false;
	bool bConfigurationValid = false;
	bool bStartingCombat = false;
	bool bCombatRequested = false;
	bool bCloseRejected = false;

	UFUNCTION()
	void OnRep_Motion();

	UFUNCTION()
	void OnCombatEvent(FGameplayTag Room, ERoomCombatEvent Event, int32 Phase, int32 Generation);

	void OnFirstSpawn(FGameplayTag Room, int32 Phase, int32 Wave, int32 Generation, FGuid RegistrationId);
	void OnReadinessChanged(FGameplayTag Room);
	void OnPairReady(uint32 Generation);
	void OnReload(uint32 Generation);
	void OnOverlap(AActor* Actor, bool bEntered);
	void OnPlayerChanged(AOutlierPlayerState* Player);
	void OnDoorFinished(AInteractableDoor* Door, bool bOpen);
	void OnDoorReopened(AInteractableDoor* Door);

	// 현재 페어의 내부 진입과 실제 문 닫힘을 확인한 뒤에만 연출/전투를 요청한다.
	void EvaluateEntry();
	void ScheduleEntryEvaluation();
	void RecheckEntry();
	bool CachePair();
	bool IsPairInside() const;
	bool IsInside(const APawn* Pawn) const;
	AElevatorShaftSection* FindEntranceSection() const;
	void ResolveEntranceActors();
	void ResolveArrivalActors();
	bool ValidateConfiguration(FString& Error) const;
	void SealEntrance();
	void StartCombat();
	void SetState(EElevatorShaftState State);
	void SetEntranceBlocked(bool bBlocked);

	bool BuildMotionProfiles(FString& Error);
	double DepartureDistance(double Elapsed) const;
	double ArrivalTravel(double Elapsed) const;
	void UpdateMotionPhase();
	void UpdateMotionAudio();
	void StopMotionAudio();
	void PrepareArrival();
	void SetControllerAtArrival(bool bArrival);
	void StartArrival();
	void CompleteLanding();
	bool IsTransitionActive() const;
	void InitializeVisualPool();
	void UpdateVisuals();
	double ServerTime() const;
	double CurrentDistance() const;
	void ResetExecution(uint32 Generation);

	// 완료 -> 양쪽 암전 확인 -> 역할별 이동 -> 화면 복구 순서를 서버에서 진행한다.
	void BeginExit();
	void RequestFade(bool bReveal);
	void FinishFadeWhenReady();
	void OnParticipantInvalidated(AFirstPersonCharacter* Pawn);
	void TryTeleportPair();
	bool ValidateExit(AFirstPersonCharacter* Pawn, const FTransform& Target) const;
	void FinishExit();
	void RollbackPair();
	void AbortTransition();
	void ReleaseTransition();
	void RetryExit();

#if WITH_DEV_AUTOMATION_TESTS
	friend class FElevatorPresentationLifecycleTest;
	friend class FElevatorArrivalMotionTest;
	friend class FElevatorCompletionBoundaryTest;
	friend class FElevatorControllerTransportTest;
#endif
};
