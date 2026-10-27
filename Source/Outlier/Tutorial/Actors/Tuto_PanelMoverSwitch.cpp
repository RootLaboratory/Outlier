#include "Tutorial/Actors/Tuto_PanelMoverSwitch.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "OutlierPlayerState.h"
#include "Interaction/PanelMover.h"
#include "Outlier.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_PanelMoverSwitch::ATuto_PanelMoverSwitch()
{
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_PanelMoverSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	ActivatingPlayer = IsValid(Interactor) ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bAccepted = Super::Interact(Interactor);
	ActivatingPlayer.Reset();
	return bAccepted;
}

void ATuto_PanelMoverSwitch::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;
	if (ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(TargetPanel))
	{
		BoundPanel = TargetPanel;
		Progress->OnProgressCompleted.AddUObject(this, &ThisClass::HandlePanelCompleted);
		if (Progress->IsProgressCompleted()) HandlePanelCompleted(TargetPanel, nullptr);
	}
	else UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s requires a Tuto progress-source TargetPanel."), *GetName());
}

void ATuto_PanelMoverSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(BoundPanel.Get()))
		Progress->OnProgressCompleted.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

bool ATuto_PanelMoverSwitch::ActivateTarget()
{
	ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(TargetPanel);
	if (!Progress || BoundPanel.Get() != TargetPanel) return false;
	Progress->SetProgressInstigator(ActivatingPlayer.Get());
	return Super::ActivateTarget();
}

void ATuto_PanelMoverSwitch::HandlePanelCompleted(AActor* Source, AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || bEffectCompleted || Source != BoundPanel.Get()) return;
	const ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Source);
	if (!Progress || !Progress->IsProgressCompleted()) return;
	bEffectCompleted = true;
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	CompleteProgress(SourcePlayerState);
}
