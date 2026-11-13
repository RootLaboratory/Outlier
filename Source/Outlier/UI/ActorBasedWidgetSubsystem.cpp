#include "UI/ActorBasedWidgetSubsystem.h"

#include "Interface/ActorBasedWidgetInterface.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

void UActorBasedWidgetSubsystem::RegisterWidgetOwner(UObject* WidgetOwner)
{
	if (IsValid(WidgetOwner) && WidgetOwner->GetWorld() == GetWorld()
		&& WidgetOwner->GetClass()->ImplementsInterface(UActorBasedWidgetInterface::StaticClass()))
	{
		WidgetOwners.Add(WidgetOwner);
	}
}

void UActorBasedWidgetSubsystem::UnregisterWidgetOwner(UObject* WidgetOwner)
{
	WidgetOwners.Remove(WidgetOwner);
}

void UActorBasedWidgetSubsystem::DismissRegisteredWidgets()
{
	for (auto It = WidgetOwners.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) It.RemoveCurrent();
	}

	// A callback may destroy an owner or change subscriptions.
	const TArray<TWeakObjectPtr<UObject>> Snapshot = WidgetOwners.Array();
	const IConsoleVariable* DebugDismiss = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.UI.DebugInteractionDismiss"));
	if (DebugDismiss && DebugDismiss->GetInt() != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionWidgetDebug] DismissRegisteredWidgets World=%s RegisteredOwners=%d"),
			*GetNameSafe(GetWorld()), Snapshot.Num());
	}
	for (const TWeakObjectPtr<UObject>& Owner : Snapshot)
	{
		if (Owner.IsValid() && WidgetOwners.Contains(Owner))
		{
			IActorBasedWidgetInterface::Execute_DismissActorBasedWidgets(Owner.Get());
		}
	}
}

void UActorBasedWidgetSubsystem::Deinitialize()
{
	WidgetOwners.Reset();
	Super::Deinitialize();
}
