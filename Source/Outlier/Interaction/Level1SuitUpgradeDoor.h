#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableDoor.h"
#include "Room/RoomCombatSubsystem.h"
#include "Level1SuitUpgradeDoor.generated.h"

class AInteractableDoor;
class AOutlierPlayerState;
class ARoomVolume;
class UOutlierArenaSubsystem;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLevel1DoorOpened, AActor* /*Door*/, uint32 /*GameplayGeneration*/);

UCLASS()
class OUTLIER_API ALevel1SuitUpgradeDoor : public AInteractableDoor
{
	GENERATED_BODY()

public:
	ALevel1SuitUpgradeDoor();

	// 문이 실제로 열린 시점을 서버 관찰자에게 알린다. 전투 시작은 이 Actor가 수행한다.
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
	void OnRoomStartReadinessChanged(FGameplayTag ChangedRoomTag);
	UFUNCTION()
	void OnCombatEvent(FGameplayTag EventRoomTag, ERoomCombatEvent Event,
		int32 CombatPhaseIndex, int32 EventGeneration);
	void OnArenaReloadStarted(uint32 NewGeneration);
	void EvaluateEntry();
	void EvaluateReopen();
	bool TryStartCombat();
	bool FindPair(AOutlierPlayerState*& OutShooter, AOutlierPlayerState*& OutPartner) const;
	bool IsInsideRoom(const AActor* Character) const;

	TArray<TWeakObjectPtr<AOutlierPlayerState>> ObservedPlayerStates;
	TWeakObjectPtr<UOutlierArenaSubsystem> ArenaSubsystem;
	TWeakObjectPtr<URoomCombatSubsystem> CombatSubsystem;
	FDelegateHandle ActorSpawnedHandle;
	uint32 GameplayGeneration = 0;
	bool bEntrySealed = false;
	bool bCloseFinished = false;
	bool bReopenRequested = false;
	bool bOpenFinished = false;
	bool bCombatStartSucceeded = false;
	bool bCombatStartInProgress = false;
};
