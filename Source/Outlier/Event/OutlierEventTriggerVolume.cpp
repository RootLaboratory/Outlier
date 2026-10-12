#include "Event/OutlierEventTriggerVolume.h"

#include "Components/BoxComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"
#include "Tutorial/TutorialComponent.h"

namespace
{
#if !UE_BUILD_SHIPPING
	void SendOutlierEventFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		const FGameplayTag EventTag = Args.Num() > 0
			? FGameplayTag::RequestGameplayTag(FName(*Args[0]), false) : FGameplayTag();
		if (!EventTag.IsValid())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] Usage: Outlier.Event.Send <EventTag> (unknown tag '%s')."),
				Args.Num() > 0 ? *Args[0] : TEXT(""));
			return;
		}
		// The console runs in the typing window's world, so each PIE client fires for its own pawn.
		APlayerController* Controller = GEngine && World ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		UTutorialComponent::SendTriggerEvent(Controller ? Controller->GetPawn() : nullptr, EventTag, TEXT("Console"));
	}

	FAutoConsoleCommandWithWorldAndArgs GOutlierEventSendCommand(
		TEXT("Outlier.Event.Send"),
		TEXT("Outlier.Event.Send <EventTag>: sends the event for this window's local pawn, like an event trigger volume."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SendOutlierEventFromConsole));
#endif
}

AOutlierEventTriggerVolume::AOutlierEventTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AOutlierEventTriggerVolume::HandleBeginOverlap);
	SetRootComponent(TriggerBox);

	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
}

bool AOutlierEventTriggerVolume::IsTargetPawn(const AActor* Actor) const
{
	switch (TargetRole)
	{
	case EOutlierEventTriggerRole::Shooter: return Actor && Actor->IsA<AShooterCharacter>();
	case EOutlierEventTriggerRole::Partner: return Actor && Actor->IsA<APartnerCharacter>();
	case EOutlierEventTriggerRole::Both: return Actor && Actor->IsA<AFirstPersonCharacter>();
	default: return false;
	}
}

void AOutlierEventTriggerVolume::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if ((bTriggerOnce && bTriggered) || !IsTargetPawn(OtherActor))
	{
		return;
	}
	APawn* Pawn = CastChecked<APawn>(OtherActor);
	// Only the root capsule counts, so attached primitives cannot fire it again.
	// Dedicated servers, simulated proxies and the other player's pawn on a listen host stop here.
	if (OtherComp != Pawn->GetRootComponent() || !Pawn->IsLocallyControlled())
	{
		UE_LOG(LogOutlier, Verbose, TEXT("[EventTrigger] %s ignored %s (not the local pawn's root)."),
			*GetName(), *GetNameSafe(Pawn));
		return;
	}
	if (UTutorialComponent::SendTriggerEvent(Pawn, EventTag, GetName()))
	{
		bTriggered = true;
	}
}
