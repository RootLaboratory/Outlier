#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableDoor.h"
#include "Level1SuitUpgradeDoor.generated.h"

class AInteractableDoor;
class AOutlierPlayerState;
class ARoomVolume;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLevel1DoorOpened, AActor* /*Door*/, uint32 /*GameplayGeneration*/);

UCLASS()
class OUTLIER_API ALevel1SuitUpgradeDoor : public AInteractableDoor
{
	GENERATED_BODY()

public:
	ALevel1SuitUpgradeDoor();

	// 다음 Slice가 이 서버 전용 이벤트로 ExternalTrigger 전투를 시작한다.
	FOnLevel1DoorOpened OnLevel1DoorOpened;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Level 1 Door")
	TObjectPtr<ARoomVolume> TargetRoomVolume;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ObservePlayerState(AActor* Actor);
	void OnPlayerStateChanged(AOutlierPlayerState* PlayerState);
	void OnUICompleted(AOutlierPlayerState* PlayerState, uint32 CompletedGeneration);
	void OnRoomOverlapChanged(AActor* Actor, bool bEntered);
	void HandleDoorMotionFinished(AInteractableDoor* Door, bool bOpen);
	void OnArenaReloadStarted(uint32 NewGeneration);
	void EvaluateEntry();
	void EvaluateReopen();
	bool FindPair(AOutlierPlayerState*& OutShooter, AOutlierPlayerState*& OutPartner) const;
	bool IsInsideRoom(const AActor* Character) const;

	TArray<TWeakObjectPtr<AOutlierPlayerState>> ObservedPlayerStates;
	FDelegateHandle ActorSpawnedHandle;
	uint32 GameplayGeneration = 0;
	bool bEntrySealed = false;
	bool bCloseFinished = false;
	bool bReopenRequested = false;
	bool bOpenFinished = false;
};
