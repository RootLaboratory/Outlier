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

/** Owner-only transport created on demand. Uses existing controllers and PlayerStates unchanged. */
UCLASS(Transient, NotBlueprintable)
class OUTLIER_API ATuto_EventRelay : public AActor
{
	GENERATED_BODY()

public:
	ATuto_EventRelay();
	static ATuto_EventRelay* FindOrCreate(APlayerController* Controller);
	static ATuto_EventRelay* Find(const APlayerController* Controller);
	void QueueEvent(FGameplayTag EventTag, const FString& Source);
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
	/** Initial replication creates the relay before delivering events, even if the source pickup was destroyed. */
	UPROPERTY(ReplicatedUsing = OnRep_PendingEvents)
	TArray<FTuto_EventDelivery> PendingEvents;

	UFUNCTION()
	void OnRep_PendingEvents();

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeEvents(uint32 ThroughId);

	uint32 NextEventId = 1;
	uint32 LastProcessedId = 0;
};
