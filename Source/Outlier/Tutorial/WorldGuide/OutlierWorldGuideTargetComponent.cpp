#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

#include "UI/TargetPointHudWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/ActorBasedWidgetSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideWidget.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideDebug.h"

UOutlierWorldGuideTargetComponent::UOutlierWorldGuideTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// 그룹 안내의 타깃별 완료만 복제한다. 소유 액터가 복제되지 않으면 서버 표시만 바뀐다.
	SetIsReplicatedByDefault(true);
}

void UOutlierWorldGuideTargetComponent::SetTargetCompleted(bool bCompleted)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bTargetCompleted == bCompleted) return;
	bTargetCompleted = bCompleted;
	GetOwner()->ForceNetUpdate();
	OnRep_TargetCompleted();
}

void UOutlierWorldGuideTargetComponent::OnRep_TargetCompleted()
{
	if (AOutlierWorldGuideManager* Manager = GuideManager.Get()) Manager->RefreshTarget(this);
	else if (bTargetCompleted) ClearGuide();
}

void UOutlierWorldGuideTargetComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UOutlierWorldGuideTargetComponent, bTargetCompleted);
}

void UOutlierWorldGuideTargetComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->RegisterWidgetOwner(this);
	}
	if (AOutlierWorldGuideManager* Manager = AOutlierWorldGuideManager::Find(GetWorld())) Manager->RegisterTarget(this);
	else if (WidgetID.IsValid()) OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(
		TEXT("TARGET %s ID=%s has no manager at BeginPlay; check the level Manager."), *GetPathName(), *WidgetID.ToString()));
}

void UOutlierWorldGuideTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->UnregisterWidgetOwner(this);
	}
	if (AOutlierWorldGuideManager* Manager = GuideManager.Get()) Manager->UnregisterTarget(this);
	ClearGuide();
	Super::EndPlay(EndPlayReason);
}

void UOutlierWorldGuideTargetComponent::SetGuideManager(AOutlierWorldGuideManager* Manager)
{
	GuideManager = Manager;
}

void UOutlierWorldGuideTargetComponent::ApplyGuide(const FOutlierWorldGuideContext& Context)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer || Context.WidgetID != WidgetID) return;
	bGuideVisible = true;
	APlayerController* Controller = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (!DisplayWidget && Controller && WidgetClass && !WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		if (!HudLayer) HudLayer = UTargetPointHudWidget::CreateForPlayer(Controller, this);
		if (HudLayer)
		{
			DisplayWidget = CreateWidget<UUserWidget>(Controller, WidgetClass);
			if (DisplayWidget) HudLayer->AddContent(DisplayWidget);
		}
	}
	if (UOutlierWorldGuideWidget* Widget = Cast<UOutlierWorldGuideWidget>(DisplayWidget))
		Widget->SetGuideContext(Context);
	UpdateDisplayPlacement();
	SetComponentTickEnabled(DisplayWidget != nullptr);
}

void UOutlierWorldGuideTargetComponent::DismissActorBasedWidgets_Implementation()
{
	// Only the local display is removed. The manager retains its active guide and chain history.
	ClearGuide();
}

void UOutlierWorldGuideTargetComponent::ClearGuide()
{
	if (bGuideVisible) OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("HIDE %s ID=%s"), *GetPathName(), *WidgetID.ToString()));
	bGuideVisible = false;
	SetComponentTickEnabled(false);
	if (DisplayWidget)
	{
		DisplayWidget->RemoveFromParent();
		DisplayWidget = nullptr;
	}
	if (HudLayer)
	{
		HudLayer->Release();
		HudLayer = nullptr;
	}
}

void UOutlierWorldGuideTargetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bGuideVisible && DisplayWidget) UpdateDisplayPlacement();
}

void UOutlierWorldGuideTargetComponent::UpdateDisplayPlacement()
{
	if (!DisplayWidget || !HudLayer) return;
	const FVector2D Size(FMath::Max(1, FMath::RoundToInt(DrawSize.X)), FMath::Max(1, FMath::RoundToInt(DrawSize.Y)));
	FTargetPointHudSettings Settings;
	Settings.MinScale = WidgetPresentation.MinScale;
	Settings.MaxScale = WidgetPresentation.MaxScale;
	Settings.ReferenceDistance = WidgetPresentation.ReferenceDistance;
	// Preserve existing BP size, pivot and offsets with the interaction HUD projection.
	HudLayer->UpdateContent(DisplayWidget, bGuideVisible,
		GetComponentLocation() + WorldOffset + WidgetPresentation.WidgetOffset, Size, Settings, Pivot);
}

bool UOutlierWorldGuideTargetComponent::CompleteGuide(FGameplayTag NextWidgetID)
{
	AOutlierWorldGuideManager* Manager = GuideManager.Get();
	if (!Manager) Manager = AOutlierWorldGuideManager::Find(GetWorld());
	const bool bAccepted = GetOwner() && GetOwner()->HasAuthority() && Manager && Manager->CompleteTarget(this, NextWidgetID);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("TARGET COMPLETE %s ID=%s Next=%s accepted=%d"),
		*GetPathName(), *WidgetID.ToString(), *NextWidgetID.ToString(), bAccepted));
	return bAccepted;
}

bool UOutlierWorldGuideTargetComponent::NotifyGuideOverlap()
{
	AOutlierWorldGuideManager* Manager = GuideManager.Get();
	if (!Manager) Manager = AOutlierWorldGuideManager::Find(GetWorld());
	const bool bAccepted = GetOwner() && GetOwner()->HasAuthority() && Manager && Manager->NotifyWidgetOverlap(WidgetID, OverlapNextWidgetID);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("TARGET OVERLAP %s ID=%s Next=%s accepted=%d"),
		*GetPathName(), *WidgetID.ToString(), *OverlapNextWidgetID.ToString(), bAccepted));
	return bAccepted;
}

bool UOutlierWorldGuideTargetComponent::CompleteOnActor(AActor* Actor, FGameplayTag NextWidgetID)
{
	if (!IsValid(Actor) || !Actor->HasAuthority()) return false;
	TArray<UOutlierWorldGuideTargetComponent*> Components;
	Actor->GetComponents<UOutlierWorldGuideTargetComponent>(Components);
	bool bCompleted = false;
	for (UOutlierWorldGuideTargetComponent* Component : Components) bCompleted |= Component->CompleteGuide(NextWidgetID);
	return bCompleted;
}

bool UOutlierWorldGuideTargetComponent::NotifyOverlapOnActor(AActor* Actor)
{
	if (!IsValid(Actor) || !Actor->HasAuthority()) return false;
	TArray<UOutlierWorldGuideTargetComponent*> Components;
	Actor->GetComponents<UOutlierWorldGuideTargetComponent>(Components);
	bool bCompleted = false;
	for (UOutlierWorldGuideTargetComponent* Component : Components) bCompleted |= Component->NotifyGuideOverlap();
	return bCompleted;
}
