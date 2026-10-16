#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Tutorial/Trigger/Tuto_EventTriggerData.h"
#include "Tuto_EventRelay.generated.h"

class APlayerController;

USTRUCT()
struct FTuto_EventDelivery
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Id = 0;

	UPROPERTY()
	FGameplayTag EventTag;

	UPROPERTY()
	FString Source;

	UPROPERTY()
	ETuto_EventOperation Operation = ETuto_EventOperation::Trigger;

	UPROPERTY()
	ETuto_EventRegion Region = ETuto_EventRegion::Both;
};

/** 필요 시 생성되는 Owner 전용 transport. 기존 컨트롤러와 PlayerState를 그대로 사용한다. */
UCLASS(Transient, NotBlueprintable)
class OUTLIER_API ATuto_EventRelay : public AActor
{
	GENERATED_BODY()

public:
	ATuto_EventRelay();
	static ATuto_EventRelay* FindOrCreate(APlayerController* Controller);
	static ATuto_EventRelay* Find(const APlayerController* Controller);
	void QueueCommand(FGameplayTag EventTag, ETuto_EventOperation Operation, ETuto_EventRegion Region,
		const FString& Source);
	const TArray<FTuto_EventDelivery>& GetPendingEvents() const { return PendingEvents; }
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual bool IsReceiverReady() const;
	virtual void DeliverEvent(const FTuto_EventDelivery& Delivery);
	void ProcessPendingEvents();

private:
	/** 원본 픽업이 파괴됐더라도 초기 리플리케이션이 이벤트 전달 전에 relay를 생성한다. */
	UPROPERTY(ReplicatedUsing = OnRep_PendingEvents)
	TArray<FTuto_EventDelivery> PendingEvents;

	UFUNCTION()
	void OnRep_PendingEvents();

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeEvents(uint32 ThroughId);

	uint32 NextEventId = 1;
	uint32 LastProcessedId = 0;
};
