#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "Enemy/EnemySightAwareness.h"
#include "EnemyAIController.generated.h"

class AEnemyBase;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;

UCLASS()
class OUTLIER_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

	void SetEnemyPerceptionEnabled(bool bEnabled);
	void RefreshPerceptionConfigFromPawn();
	void RefreshTeamAndPerceptionFromPawn();
	void BeginTaskDrivenControlPitch();
	void EndTaskDrivenControlPitch();
	// 발사 시작부터 중단까지 공격 경로가 조준 회전을 소유한다.
	void SetAttackAimActive(bool bActive);
	bool IsAttackAimActive() const { return bAttackAimActive; }
	// 사망 또는 리스폰으로 교체되는 액터를 감지 캐시에서 제거하고 남은 타겟을 즉시 재평가한다.
	void ForgetDetectionTarget(AActor* TargetActor);

	// 현재 Sight 대상 중 해킹 터렛, 빙의 드론, Shooter, Partner 순으로 선택한다.
	// 같은 우선순위 안에서는 가장 가까운 대상을 반환한다.
	// StateTree AttackTarget에 명시적 TargetActor 바인딩이 없을 때 사용하는 서버용 폴백이다.
	AActor* GetPreferredVisibleTarget() const;
	// Perception 이벤트/공유 갱신이 선택한 타깃이다. EQS 대기 중에는 감지 목록을 매 프레임 재검색하지 않는다.
	AActor* GetCachedVisibleTarget() const { return LastVisibleTarget.Get(); }
	bool IsTargetDirectlyVisible(const AActor* Target) const;
	bool IsValidTrackingTarget(const AActor* Target) const { return IsValidDetectionTarget(Target); }
	// 최초 시각·청각 발견을 확정한다. 진행 중인 조사나 공유 경계는 수치 0만으로 취소하지 않는다.
	bool ResolveInitialAwarenessAlert();

#if WITH_DEV_AUTOMATION_TESTS
	void SetCachedVisibleTargetForTesting(AActor* TargetActor) { LastVisibleTarget = TargetActor; }

	friend class FEnemyHearingCombatJoinTest;
	friend class FEnemyHearingMemoryTest;
	friend class FEnemyHearingNotifyTest;
	friend class FEnemySightCombatJoinTest;
	friend class FRoomGroupPursuitDetectionTest;
	friend class FEnemyChaseIntegrationTest;
	friend class FEnemySearchWorldLifecycleTest;
	friend class FEnemySightAlertIntegrationTest;
	friend class FEnemyHearingAwarenessIntegrationTest;
#endif

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<UAIPerceptionComponent> EnemyPerceptionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	void HandleSightStimulus(AEnemyBase* Enemy, AActor* Actor, const FAIStimulus& Stimulus);
	void HandleHearingStimulus(AEnemyBase* Enemy, AActor* Actor, const FAIStimulus& Stimulus);
	void UpdateVisibleTarget(AEnemyBase* Enemy, AActor* PreferredTarget);
	void StartSharedTargetReporting();
	void StopSharedTargetReporting(bool bRemoveObserver);
	void RefreshSharedTargetContact();
	void ReportSharedTargetContact(AEnemyBase* Enemy, AActor* TargetActor);
	AActor* ForgetStealthedPerceivedActors();
	AActor* SelectPreferredVisibleTarget(const TArray<AActor*>& VisibleActors) const;

	void ConfigureSightFromEnemy(AEnemyBase* Enemy);
	void ConfigureHearingFromEnemy(AEnemyBase* Enemy);
	bool IsValidDetectionTarget(const AActor* TargetActor) const;
	bool IsStealthedDetectionTarget(const AActor* TargetActor) const;
	void ObserveInitialSight(AEnemyBase& Enemy, AActor& Target, bool bVisible);
	void ObserveInitialHearing(AEnemyBase& Enemy, AActor& Target, const FVector& Location, double OccurredAt);
	void ResetInitialAwareness();

	struct FAwarenessTarget
	{
		FEnemySightAwareness Progress;
		FVector LastSeenLocation = FVector::ZeroVector;
		FVector LastHeardLocation = FVector::ZeroVector;
		double LastSeenAt = -1.0;
		double LastHeardAt = -1.0;
		bool bVisible = false;
	};
	void AdvanceInitialAwareness(AEnemyBase& Enemy, FAwarenessTarget& State);
	TMap<TWeakObjectPtr<AActor>, FAwarenessTarget> AwarenessTargets;
	bool bInitialAwarenessAlert = false;

	// 직접 Sight가 유지되는 동안에만 방 공유 좌표를 저빈도로 갱신한다.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|AI", meta = (ClampMin = "0.05"))
	float SharedTargetReportInterval = 0.25f;

	FTimerHandle SharedTargetReportTimerHandle;
	TSet<TWeakObjectPtr<AActor>> ProcessedStealthedTargets;
	TWeakObjectPtr<AActor> LastVisibleTarget;
	mutable TSet<TWeakObjectPtr<AActor>> DirectlyVisibleTargets;
	mutable TArray<AActor*> PerceivedActorScratch;
	int32 TaskDrivenControlPitchCount = 0;
	bool bAttackAimActive = false;
};
