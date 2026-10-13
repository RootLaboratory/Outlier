#include "Tutorial/Actors/Tuto_EventTriggerVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Tutorial/TutorialComponent.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

namespace
{
#if !UE_BUILD_SHIPPING
	void SendTutorialEventFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		const FGameplayTag EventID = Args.Num() > 0
			? FGameplayTag::RequestGameplayTag(FName(*Args[0]), false) : FGameplayTag();
		APlayerController* Controller = GEngine && World ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		UTutorialComponent::SendTriggerEvent(Controller ? Controller->GetPawn() : nullptr, EventID, TEXT("Console"));
	}

	FAutoConsoleCommandWithWorldAndArgs GOutlierEventSendCommand(
		TEXT("Outlier.Event.Send"), TEXT("Outlier.Event.Send <EventTag>: request the event on the server for both players."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SendTutorialEventFromConsole));
#endif
}

ATuto_EventTriggerVolume::ATuto_EventTriggerVolume()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleTutoBeginOverlap);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(TriggerBox);
}

void ATuto_EventTriggerVolume::PostLoad()
{
	Super::PostLoad();
	if (!EventTag.IsValid() && !EventTriggers_DEPRECATED.IsEmpty()) EventTag = EventTriggers_DEPRECATED[0].EventTag;
	EventTriggers_DEPRECATED.Reset();
}

void ATuto_EventTriggerVolume::HandleTutoBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (IsValid(Pawn) && OtherComp == Pawn->GetRootComponent()) RequestLocalEntry(Pawn);
}

void ATuto_EventTriggerVolume::RequestLocalEntry(APawn* Pawn)
{
	if (bHasTriggered || bRequestPending || !IsValid(Pawn)
		|| !Pawn->IsLocallyControlled() || GetNetMode() == NM_DedicatedServer) return;
	AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(Pawn->GetController());
	if (!IsValid(Controller)) return;
	PendingController = Controller;
	bRequestPending = true;
	if (!Controller->NotifyTutorialVolumeEntered(this)) bRequestPending = false;
}

void ATuto_EventTriggerVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bHasTriggered) { SetActorTickEnabled(false); return; }
	if (GetNetMode() == NM_DedicatedServer) { SetActorTickEnabled(false); return; }
	APlayerController* Controller = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (bRequestPending && PendingController.Get() != Controller) bRequestPending = false;
	// Covers an initially overlapping pawn and retries until both server recipients are available.
	if (IsValid(Pawn) && TriggerBox->IsOverlappingActor(Pawn)) RequestLocalEntry(Pawn);
}

bool ATuto_EventTriggerVolume::ConfirmClientEntry(AFirstPersonPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || Controller->GetWorld() != GetWorld()) return false;
	if (bHasTriggered) return true;
	if (bConfirmingEntry) return false;
	APawn* Pawn = Controller->GetPawn();
	AOutlierPlayerState* State = Controller->GetPlayerState<AOutlierPlayerState>();
	if (!IsValid(Pawn) || !IsValid(State)
		|| Pawn->GetPlayerState<AOutlierPlayerState>() != State) return false;
	// Entry is detected locally. The server verifies the request against its current pawn/volume,
	// allowing capsule radius and a small movement replication margin, without a server overlap callback.
	const double Margin = Pawn->GetSimpleCollisionRadius() + 50.0;
	if (!TriggerBox->Bounds.GetBox().ExpandBy(Margin).IsInsideOrOn(Pawn->GetActorLocation())) return false;
	TGuardValue<bool> ConfirmGuard(bConfirmingEntry, true);
	if (!HandlePawnEntered(State)) return false;
	bHasTriggered = true;
	SetActorTickEnabled(false);
	ForceNetUpdate();
	return true;
}

void ATuto_EventTriggerVolume::ReceiveEntryResult(AFirstPersonPlayerController* Controller, bool bAccepted)
{
	if (PendingController.Get() != Controller) return;
	bRequestPending = false;
	if (bAccepted)
	{
		bHasTriggered = true;
		SetActorTickEnabled(false);
	}
}

void ATuto_EventTriggerVolume::OnRep_HasTriggered()
{
	if (bHasTriggered) { bRequestPending = false; SetActorTickEnabled(false); }
}

bool ATuto_EventTriggerVolume::HandlePawnEntered(AOutlierPlayerState* SourcePlayerState)
{
	// An event-bearing volume must not consume the arrival before transport is ready.
	// Empty EventTag supports a WorldGuide arrival-only volume.
	if (EventTag.IsValid() && !TriggerEvent(0, SourcePlayerState)) return false;
	const bool bGuideCompleted = UOutlierWorldGuideTargetComponent::NotifyOverlapOnActor(this);
	return EventTag.IsValid() || bGuideCompleted;
}

void ATuto_EventTriggerVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATuto_EventTriggerVolume, bHasTriggered);
}
