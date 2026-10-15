#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_EventTriggerVolume.generated.h"

class AFirstPersonPlayerController;
class UBoxComponent;
class UOutlierWorldGuideTargetComponent;

/** 위치 하나에 EventID 하나. 로컬 진입은 소유 컨트롤러를 통해 알린다. */
UCLASS()
class OUTLIER_API ATuto_EventTriggerVolume : public AActor, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_EventTriggerVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;

	virtual FGameplayTag GetEventTag(int32 EventIndex) const override { return EventIndex == 0 ? EventTag : FGameplayTag(); }

	/** 서버 컨트롤러의 진입 RPC만 호출한다. 클라이언트는 EventID나 PlayerState를 넘기지 않는다. */
	bool ConfirmClientEntry(AFirstPersonPlayerController* Controller);
	void ReceiveEntryResult(AFirstPersonPlayerController* Controller, bool bAccepted);
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostLoad() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|Event")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	virtual bool HandlePawnEntered(AOutlierPlayerState* SourcePlayerState);

private:
	// 직렬화된 구 Tuto 볼륨 설정은 한 번 마이그레이션되며, 편집 가능한 배열은 남지 않는다.
	UPROPERTY()
	TArray<FTuto_EventTriggerData> EventTriggers_DEPRECATED;

	UPROPERTY(ReplicatedUsing = OnRep_HasTriggered)
	bool bHasTriggered = false;

	UFUNCTION()
	void OnRep_HasTriggered();

	UFUNCTION()
	void HandleTutoBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void RequestLocalEntry(APawn* Pawn);
	TWeakObjectPtr<AFirstPersonPlayerController> PendingController;
	bool bRequestPending = false;
	bool bConfirmingEntry = false;
};
