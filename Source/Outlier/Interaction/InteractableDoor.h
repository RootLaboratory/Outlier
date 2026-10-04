#pragma once

#include "CoreMinimal.h"
#include "Components/TimelineComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "InteractableDoor.generated.h"

class UStaticMeshComponent;
class UCurveFloat;
class UBoxComponent;
class AInteractableDoor;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDoorMotionFinished, AInteractableDoor*, bool /*bOpen*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDoorSafetyReopenStarted, AInteractableDoor*);

// 방향 전환 한 건의 서버 기준점. 위치를 매 프레임 복제하지 않고 수신 시 경과 시간을 보정한다.
USTRUCT()
struct FDoorMotionState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bOpen = false;

	UPROPERTY()
	bool bMoving = false;

	UPROPERTY()
	float PlaybackPosition = 0.0f;

	UPROPERTY()
	double ServerTimeSeconds = 0.0;

	UPROPERTY()
	uint32 Revision = 0;
};

UCLASS()
class OUTLIER_API AInteractableDoor : public AActor
{
	GENERATED_BODY()

public:
	AInteractableDoor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Door")
	void SetDoorOpen(bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "Door")
	void ToggleDoor();

	UFUNCTION(BlueprintCallable, Category = "Door")
	bool TrySetDoorOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Door|Safety")
	bool HasBlockingPlayer() const;

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsDoorOpen() const { return bIsOpen; }
	bool HasMovementCurve() const;
	FOnDoorMotionFinished OnDoorMotionFinished;
	FOnDoorSafetyReopenStarted OnDoorSafetyReopenStarted;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> DoorMeshLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> DoorMeshRight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Safety")
	TObjectPtr<UBoxComponent> SafetyRegionLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Safety")
	TObjectPtr<UBoxComponent> SafetyRegionRight;

	// 문짝의 전체 이동 경로에 더할 여유 거리. 상단 높이는 점프/비행 영역을 포함한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Safety", meta = (ClampMin = "0", Units = "cm"))
	float SafetyMargin = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Safety", meta = (ClampMin = "0", Units = "cm"))
	float SafetyTopHeight = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffsetLeft = FVector(-120.0f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffsetRight = FVector(120.0f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Door")
	TObjectPtr<UCurveFloat> DoorCurve;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Door")
	FName DoorId = NAME_None;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Door")
	bool bInitiallyOpen = false;

	/** Played as server-authoritative Relevant AtLocation audio when movement starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag DoorMovementAudioEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Audio", meta = (Categories = "Audio.Context"))
	FGameplayTagContainer DoorMovementAudioContextTags;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Door")
	bool bIsOpen = false;
	void SnapDoorState(bool bOpen);
	bool TrySafetyReopen();

private:
	FTimeline DoorTimeline;
	FVector ClosedLocationLeft = FVector::ZeroVector;
	FVector ClosedLocationRight = FVector::ZeroVector;

	UFUNCTION()
	void OnDoorTimelineUpdate(float Alpha);

	UFUNCTION()
	void OnDoorTimelineFinished();

	UFUNCTION()
	void OnRep_DoorMotion();

	void ApplyDoorState(bool bOpen);
	void StartDoorMotion(bool bOpen, bool bSafetyReopen);
	void PublishDoorMotion();
	void ApplyReplicatedDoorMotion(const FDoorMotionState& State);
	void InitializeSafetyRegion(UBoxComponent* Region, UStaticMeshComponent* Mesh, const FVector& OpenOffset);
	UPROPERTY(ReplicatedUsing = OnRep_DoorMotion)
	FDoorMotionState ReplicatedDoorMotion;
	FDoorMotionState PendingInitialDoorMotion;
	mutable TArray<FOverlapResult> SafetyOverlaps;
	uint32 LastAppliedMotionRevision = 0;
	bool bDoorInitialized = false;
	bool bProgressIdRegistered = false;
	bool bMotionCompletionPending = false;
	bool bOpenProgressBeforeClosing = false;
	bool PlayDoorMovementAudio(bool bOpen);

#if WITH_DEV_AUTOMATION_TESTS
	friend class FDoorPlayerSafetyMotionTest;
	friend class FDoorPlayerSafetyReplicationTest;
#endif

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetDoorMotion(const FDoorMotionState& State);
};
