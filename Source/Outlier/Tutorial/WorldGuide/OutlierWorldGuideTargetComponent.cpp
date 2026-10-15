#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideWidget.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideDebug.h"

UOutlierWorldGuideTargetComponent::UOutlierWorldGuideTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UOutlierWorldGuideTargetComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AOutlierWorldGuideManager* Manager = AOutlierWorldGuideManager::Find(GetWorld())) Manager->RegisterTarget(this);
	else if (WidgetID.IsValid()) OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(
		TEXT("TARGET %s ID=%s has no manager at BeginPlay; check the level Manager."), *GetPathName(), *WidgetID.ToString()));
}

void UOutlierWorldGuideTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
	if (!DisplayComponent && WidgetClass && !WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		DisplayComponent = NewObject<UWidgetComponent>(GetOwner(), NAME_None, RF_Transient);
		DisplayComponent->SetupAttachment(this);
		DisplayComponent->SetWidgetSpace(EWidgetSpace::Screen);
		DisplayComponent->SetAbsolute(true, true, true);
		DisplayComponent->SetDrawAtDesiredSize(false);
		DisplayComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DisplayComponent->SetGenerateOverlapEvents(false);
		DisplayComponent->SetIsReplicated(false);
		DisplayComponent->SetWindowFocusable(false);
		DisplayComponent->SetWidgetClass(WidgetClass);
		UpdateDisplayPlacement();
		GetOwner()->AddInstanceComponent(DisplayComponent);
		DisplayComponent->RegisterComponent();
		DisplayComponent->InitWidget();
	}
	if (DisplayComponent)
	{
		UpdateDisplayPlacement();
		if (UUserWidget* Widget = DisplayComponent->GetUserWidgetObject())
			Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOutlierWorldGuideWidget* Widget = Cast<UOutlierWorldGuideWidget>(DisplayComponent->GetUserWidgetObject()))
			Widget->SetGuideContext(Context);
		DisplayComponent->SetVisibility(true);
	}
	SetComponentTickEnabled(DisplayComponent != nullptr);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString(TEXT("APPLY ")) + DescribeDebugState());
}

void UOutlierWorldGuideTargetComponent::ClearGuide()
{
	if (bGuideVisible) OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("HIDE %s ID=%s"), *GetPathName(), *WidgetID.ToString()));
	bGuideVisible = false;
	SetComponentTickEnabled(false);
	if (DisplayComponent)
	{
		DisplayComponent->DestroyComponent();
		DisplayComponent = nullptr;
	}
}

void UOutlierWorldGuideTargetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bGuideVisible && DisplayComponent) UpdateDisplayPlacement();
}

void UOutlierWorldGuideTargetComponent::UpdateDisplayPlacement()
{
	if (!DisplayComponent) return;
	// 위젯은 뷰포트에 남고, 투영 앵커만 월드를 따라간다.
	DisplayComponent->SetWorldLocation(GetComponentLocation() + WorldOffset);
	const FVector2D Size(FMath::Max(1, FMath::RoundToInt(DrawSize.X)), FMath::Max(1, FMath::RoundToInt(DrawSize.Y)));
	if (DisplayComponent->GetDrawSize() != Size) DisplayComponent->SetDrawSize(Size);
	const FVector2D Alignment(FMath::Clamp(Pivot.X, 0.0, 1.0), FMath::Clamp(Pivot.Y, 0.0, 1.0));
	if (DisplayComponent->GetPivot() != Alignment) DisplayComponent->SetPivot(Alignment);
	APlayerController* Controller = GEngine && GetWorld() ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (Controller && Controller->GetLocalPlayer())
	{
		ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
		DisplayComponent->SetOwnerPlayer(LocalPlayer);
		if (UUserWidget* Widget = DisplayComponent->GetUserWidgetObject())
			if (Widget->GetOwningLocalPlayer() != LocalPlayer) Widget->SetOwningLocalPlayer(LocalPlayer);
	}
}

bool UOutlierWorldGuideTargetComponent::CompleteGuide(FGameplayTag NextWidgetID)
{
	AOutlierWorldGuideManager* Manager = GuideManager.Get();
	if (!Manager) Manager = AOutlierWorldGuideManager::Find(GetWorld());
	const bool bAccepted = GetOwner() && GetOwner()->HasAuthority() && Manager && Manager->CompleteWidget(WidgetID, NextWidgetID);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("TARGET COMPLETE %s ID=%s Next=%s accepted=%d"),
		*GetPathName(), *WidgetID.ToString(), *NextWidgetID.ToString(), bAccepted));
	return bAccepted;
}

bool UOutlierWorldGuideTargetComponent::NotifyGuideOverlap()
{
	AOutlierWorldGuideManager* Manager = GuideManager.Get();
	if (!Manager) Manager = AOutlierWorldGuideManager::Find(GetWorld());
	const bool bAccepted = GetOwner() && GetOwner()->HasAuthority() && Manager && Manager->NotifyWidgetOverlap(WidgetID);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("TARGET OVERLAP %s ID=%s accepted=%d"), *GetPathName(), *WidgetID.ToString(), bAccepted));
	return bAccepted;
}

FString UOutlierWorldGuideTargetComponent::DescribeDebugState() const
{
	const UUserWidget* Widget = DisplayComponent ? DisplayComponent->GetUserWidgetObject() : nullptr;
	return FString::Printf(TEXT("Target=%s ID=%s Class=%s Space=Screen GuideActive=%d WidgetCreated=%d Root=%s ComponentVisible=%d OwnerHidden=%d UISize=%s Pivot=%s WorldOffset=%s Anchor=%s WidgetLocation=%s"),
		*GetPathName(), *WidgetID.ToString(), *GetPathNameSafe(WidgetClass.Get()), bGuideVisible,
		Widget != nullptr, *GetNameSafe(Widget ? Widget->GetRootWidget() : nullptr),
		DisplayComponent && DisplayComponent->IsVisible(), GetOwner() && GetOwner()->IsHidden(),
		*DrawSize.ToString(), *Pivot.ToString(), *WorldOffset.ToString(), *GetComponentLocation().ToString(),
		*(DisplayComponent ? DisplayComponent->GetComponentLocation() : GetComponentLocation() + WorldOffset).ToString());
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
