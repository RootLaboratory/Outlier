#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "PanelMover.generated.h"

class UCurveFloat;
class UMaterialInterface;
class UStaticMeshComponent;

/** 이동 중에만 지정 슬롯의 Material을 바꾼다. MovingMaterial이 비면 변경하지 않는다. */
USTRUCT(BlueprintType)
struct FPanelMoverMovingMaterial
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Panel|Material", meta = (ClampMin = "0"))
	int32 MaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Panel|Material")
	TObjectPtr<UMaterialInterface> MovingMaterial;
};

/** 이동 종료까지 원래 Material을 GC로부터 보존하는 런타임 캐시. */
USTRUCT()
struct FPanelMoverCachedMaterial
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(Transient)
	int32 MaterialIndex = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OriginalMaterial;
};

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
	/** 실제 도착 후 확장 알림. 체크포인트 복원의 스냅도 이 경로를 지난다. */
	virtual void FinishMove();

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

	/** 이동 중 Supporter에 적용할 Material. 도착하면 원래 Material로 되돌린다. */
	UPROPERTY(EditAnywhere, Category = "Panel|Material")
	FPanelMoverMovingMaterial SupporterMovingMaterial;

	/** 이동 중 PanelMesh(LiftPlatform)에 적용할 Material. 도착하면 원래 Material로 되돌린다. */
	UPROPERTY(EditAnywhere, Category = "Panel|Material")
	FPanelMoverMovingMaterial LiftPlatformMovingMaterial;

	/** 이동 루프 사운드 Type. 비우면 Audio.Type.Environment. */
	UPROPERTY(EditAnywhere, Category = "Panel|Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag MovingAudioTypeTag;

	/** 이동 루프 사운드 Context. 비우면 Audio.Context.Ambience.Panel.Moving. DA의 PlaybackPolicy가 Loop여야 반복된다. */
	UPROPERTY(EditAnywhere, Category = "Panel|Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag MovingAudioContextTag;

private:
	UPROPERTY(ReplicatedUsing = OnRep_IsMoved)
	bool bIsMoved = false;

	UFUNCTION()
	void OnRep_IsMoved();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartMove();

	void StartMove();
	void SnapToTarget();
	float EvaluateProgress(float TimeRatio) const;

	void ApplyMovingMaterials();
	void RestoreOriginalMaterials();
	void SetMovingAudio(bool bEnabled);

	/** StartMove에서 바꾼 슬롯의 원래 Material. 캐시가 없으면 복원하지 않는다. */
	UPROPERTY(Transient)
	TArray<FPanelMoverCachedMaterial> CachedMaterials;
	bool bMovingAudioActive = false;

	FVector StartLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	float MoveElapsed = 0.0f;
	bool bMoveStarted = false;
	bool bProgressIdRegistered = false;
};
