#include "Tutorial/Trigger/Tuto_EventRelay.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Event/OutlierEventSubsystem.h"
#include "Outlier.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"

ATuto_EventRelay::ATuto_EventRelay()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

ATuto_EventRelay* ATuto_EventRelay::Find(const APlayerController* Controller)
{
	if (!IsValid(Controller) || !Controller->GetWorld()) return nullptr;
	for (TActorIterator<ATuto_EventRelay> It(Controller->GetWorld()); It; ++It)
	{
		if (IsValid(*It) && It->GetOwner() == Controller) return *It;
	}
	return nullptr;
}

ATuto_EventRelay* ATuto_EventRelay::FindOrCreate(APlayerController* Controller)
{
	if (!IsValid(Controller) || !Controller->HasAuthority() || !Controller->GetWorld()
		|| Controller->GetNetMode() == NM_Client) return nullptr;
	if (ATuto_EventRelay* Existing = Find(Controller)) return Existing;
	FActorSpawnParameters Parameters;
	Parameters.Owner = Controller;
	Parameters.OverrideLevel = Controller->GetWorld()->PersistentLevel;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return Controller->GetWorld()->SpawnActor<ATuto_EventRelay>(ATuto_EventRelay::StaticClass(),
		FTransform::Identity, Parameters);
}

void ATuto_EventRelay::QueueCommand(FGameplayTag EventTag, ETuto_EventOperation Operation, ETuto_EventRegion Region,
	const FString& Source)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || !EventTag.IsValid()) return;
	if (Region == ETuto_EventRegion::WorldGuide || Region == ETuto_EventRegion::All)
	{
		if (AOutlierWorldGuideManager* Manager = AOutlierWorldGuideManager::Find(GetWorld()))
		{
			switch (Operation)
			{
			case ETuto_EventOperation::Trigger: Manager->TriggerEvent(EventTag); break;
			case ETuto_EventOperation::Complete: Manager->CompleteEvent(EventTag); break;
			case ETuto_EventOperation::Cancel: Manager->CancelEvent(EventTag); break;
			case ETuto_EventOperation::Overlap: Manager->NotifyEventOverlap(EventTag); break;
			default: break;
			}
		}
		if (Region == ETuto_EventRegion::WorldGuide) return;
		Region = ETuto_EventRegion::Both;
	}
	FTuto_EventDelivery& Delivery = PendingEvents.AddDefaulted_GetRef();
	Delivery.Id = NextEventId++;
	Delivery.EventTag = EventTag;
	Delivery.Source = Source;
	Delivery.Operation = Operation;
	Delivery.Region = Region;
	ForceNetUpdate();
	// Listen 호스트/standalone은 같은 전달 로직을 즉시 사용하고, 원격 클라이언트는 리플리케이트된 큐를 받는다.
	ProcessPendingEvents();
}

bool ATuto_EventRelay::IsReceiverReady() const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const AOutlierPlayerState* State = IsValid(Controller) ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr;
	return Controller && Controller->IsLocalController() && Controller->GetLocalPlayer()
		&& IsValid(State)
		&& (State->GetPlayerRole() == EOutlierPlayerRole::Shooter || State->GetPlayerRole() == EOutlierPlayerRole::Partner)
		&& Controller->GetLocalPlayer()->GetSubsystem<UOutlierEventSubsystem>();
}

void ATuto_EventRelay::DeliverEvent(const FTuto_EventDelivery& Delivery)
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	ULocalPlayer* Player = IsValid(Controller) ? Controller->GetLocalPlayer() : nullptr;
	UOutlierEventSubsystem* Events = Player ? Player->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	if (!Events) return;
	bool bAccepted = false;
	if (Delivery.Operation == ETuto_EventOperation::Trigger)
	{
		bAccepted = Events->SendEvent(Delivery.EventTag);
	}
	else
	{
		const auto ApplyToRegion = [&Delivery, Events](bool bGif)
		{
			switch (Delivery.Operation)
			{
			case ETuto_EventOperation::Complete:
				return bGif ? Events->CompleteGifEvent(Delivery.EventTag) : Events->CompleteUpperEvent(Delivery.EventTag);
			case ETuto_EventOperation::Cancel:
				return bGif ? Events->CancelGifEvent(Delivery.EventTag) : Events->CancelUpperEvent(Delivery.EventTag);
			case ETuto_EventOperation::Overlap:
				return bGif ? Events->NotifyGifOverlap(Delivery.EventTag) : Events->NotifyOverlap(Delivery.EventTag);
			default: return false;
			}
		};
		// 둘을 독립적으로 평가한다. Upper 완료 성공이 GIF 완료를 단락시키면 안 된다.
		if (Delivery.Region != ETuto_EventRegion::Gif) bAccepted |= ApplyToRegion(false);
		if (Delivery.Region != ETuto_EventRegion::Upper) bAccepted |= ApplyToRegion(true);
	}
	UE_LOG(LogOutlier, Log, TEXT("[EventRelay] %s operation=%d event='%s' accepted=%s"),
		*Delivery.Source, static_cast<int32>(Delivery.Operation), *Delivery.EventTag.ToString(), bAccepted ? TEXT("O") : TEXT("X"));
}

void ATuto_EventRelay::ProcessPendingEvents()
{
	if (PendingEvents.IsEmpty() || !IsReceiverReady()) return;
	// 로컬 전달/ack가 PendingEvents를 변경할 수 있으므로 스냅샷을 순회한다.
	const TArray<FTuto_EventDelivery> Deliveries = PendingEvents;
	for (const FTuto_EventDelivery& Delivery : Deliveries)
	{
		if (Delivery.Id <= LastProcessedId) continue;
		LastProcessedId = Delivery.Id;
		DeliverEvent(Delivery);
	}
	if (LastProcessedId != 0) ServerAcknowledgeEvents(LastProcessedId);
}

void ATuto_EventRelay::ServerAcknowledgeEvents_Implementation(uint32 ThroughId)
{
	if (ThroughId == 0 || ThroughId >= NextEventId) return;
	PendingEvents.RemoveAll([ThroughId](const FTuto_EventDelivery& Delivery) { return Delivery.Id <= ThroughId; });
	ForceNetUpdate();
}

void ATuto_EventRelay::OnRep_PendingEvents()
{
	ProcessPendingEvents();
}

void ATuto_EventRelay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !IsValid(GetOwner()))
	{
		Destroy();
		return;
	}
	// Owner/PS role은 초기 큐보다 늦게 도착할 수 있다. 빙의된 폰은 필요 없다.
	ProcessPendingEvents();
}

void ATuto_EventRelay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATuto_EventRelay, PendingEvents, COND_OwnerOnly);
}
