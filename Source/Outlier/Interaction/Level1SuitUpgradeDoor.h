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

	// 두 플레이어가 들어와 문을 닫는 구역. 기존 인스턴스 참조를 유지한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Level 1 Door", meta = (DisplayName = "Entry Room Volume"))
	TObjectPtr<ARoomVolume> TargetRoomVolume;

	// 문 열림 완료 후 ExternalTrigger 전투를 시작할 별도 Room.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Level 1 Door")
	TObjectPtr<ARoomVolume> CombatRoomVolume;

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
	void OnArenaGameplayReady(uint32 ReadyGeneration);
	void ReconcileRestoredProgress();
	void EvaluateEntry();
	void LogEntryStatus(const TCHAR* Reason, const AOutlierPlayerState* Shooter,
		const AOutlierPlayerState* Partner);
	void EvaluateReopen();
	bool TryStartCombat();
	bool FindPair(AOutlierPlayerState*& OutShooter, AOutlierPlayerState*& OutPartner) const;
	bool IsInsideRoom(const AActor* Character) const;

	TArray<TWeakObjectPtr<AOutlierPlayerState>> ObservedPlayerStates;
	TWeakObjectPtr<UOutlierArenaSubsystem> ArenaSubsystem;
	TWeakObjectPtr<URoomCombatSubsystem> CombatSubsystem;
	FDelegateHandle ActorSpawnedHandle;
	FTimerHandle EntryRecheckTimer;
	TSet<TWeakObjectPtr<AActor>> OverlappingPlayers;
	uint32 GameplayGeneration = 0;
	bool bEntrySealed = false;
	bool bCloseFinished = false;
	bool bReopenRequested = false;
	bool bOpenFinished = false;
	bool bCombatStartSucceeded = false;
	bool bCombatStartInProgress = false;
	bool bAwaitingGameplayReady = false;
	FString LastEntryStatus;
};
