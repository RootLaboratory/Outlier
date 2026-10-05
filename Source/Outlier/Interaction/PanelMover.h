#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PanelMover.generated.h"

class UCurveFloat;
class UStaticMeshComponent;

/**
 * 스위치가 MoveToTarget을 부르면 TargetLocationActor 위치까지 한 번 이동하는 판넬(편도, 회전 없음).
 * 이동량은 MoveCurve를 MoveDuration 길이로 늘려 샘플링한다.
 * 복제는 문과 같다. 서버 Multicast로 각 머신이 로컬 보간을 시작하고, 놓친 경우는 bIsMoved RepNotify가 받는다.
 */
UCLASS()
class OUTLIER_API APanelMover : public AActor
{
	GENERATED_BODY()

public:
	APanelMover();

	/** 서버 전용. 이미 이동했거나 목표가 없으면 false. */
	bool MoveToTarget();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, Category = "Component")
	TObjectPtr<UStaticMeshComponent> Supporter;

	UPROPERTY(VisibleAnywhere, Category = "Component")
	TObjectPtr<UStaticMeshComponent> PanelMesh;

	/** 도착 지점. 위치만 쓰므로 TargetPoint든 볼륨이든 상관없다. */
	UPROPERTY(EditInstanceOnly, Category = "Panel")
	TObjectPtr<AActor> TargetLocationActor;

	/** 체크포인트 진행 ID. 비워두면 저장/복원하지 않는다. */
	UPROPERTY(EditInstanceOnly, Category = "Panel")
	FName PanelId = NAME_None;

	/**
	 * X: 시간(커브의 시간 범위 전체가 MoveDuration으로 늘어난다), Y: 진행도(0 출발, 1 도착).
	 * 1을 넘는 값은 오버슈트로 쓰인다. 비우면 선형.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Panel|Motion")
	TObjectPtr<UCurveFloat> MoveCurve;

	/** 0이면 즉시 도착. */
	UPROPERTY(EditDefaultsOnly, Category = "Panel|Motion", meta = (ClampMin = "0.0", Units = "s"))
	float MoveDuration = 1.0f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_IsMoved)
	bool bIsMoved = false;

	UFUNCTION()
	void OnRep_IsMoved();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartMove();

	void StartMove();
	void SnapToTarget();
	void FinishMove();
	float EvaluateProgress(float TimeRatio) const;

	FVector StartLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	float MoveElapsed = 0.0f;
	bool bMoveStarted = false;
	bool bProgressIdRegistered = false;
};
