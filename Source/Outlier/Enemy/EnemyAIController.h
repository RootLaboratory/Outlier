#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
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
	void BeginTaskDrivenControlPitch();
	void EndTaskDrivenControlPitch();

	// 현재 Sight 대상 중 해킹 터렛, 빙의 드론, Shooter, Partner 순으로 선택한다.
	// 같은 우선순위 안에서는 가장 가까운 대상을 반환한다.
	// StateTree AttackTarget에 명시적 TargetActor 바인딩이 없을 때 사용하는 서버용 폴백이다.
	AActor* GetPreferredVisibleTarget() const;

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
	void StartSharedTargetReporting();
	void StopSharedTargetReporting(bool bRemoveObserver);
	void RefreshSharedTargetContact();
	void ForgetStealthedPerceivedActors();

	bool HasAnyVisiblePlayer() const;

	void ConfigureSightFromEnemy(AEnemyBase* Enemy);
	void ConfigureHearingFromEnemy(AEnemyBase* Enemy);
	int32 ResolveArenaIdFromTarget(const AActor* TargetActor) const;
	bool IsValidDetectionTarget(const AActor* TargetActor) const;
	bool IsStealthedDetectionTarget(const AActor* TargetActor) const;

	// 직접 Sight가 유지되는 동안에만 방 공유 좌표를 저빈도로 갱신한다.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|AI", meta = (ClampMin = "0.05"))
	float SharedTargetReportInterval = 0.25f;

	FTimerHandle SharedTargetReportTimerHandle;
	TSet<TWeakObjectPtr<AActor>> ProcessedStealthedTargets;
	int32 TaskDrivenControlPitchCount = 0;
};
