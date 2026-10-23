#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ActorBasedWidgetSubsystem.generated.h"

/** World-scoped subscriptions; dismissal is local and does not replicate gameplay changes. */
UCLASS()
class OUTLIER_API UActorBasedWidgetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI|Actor Based Widgets")
	void RegisterWidgetOwner(UObject* WidgetOwner);

	UFUNCTION(BlueprintCallable, Category = "UI|Actor Based Widgets")
	void UnregisterWidgetOwner(UObject* WidgetOwner);

	UFUNCTION(BlueprintCallable, Category = "UI|Actor Based Widgets")
	void DismissRegisteredWidgets();

	virtual void Deinitialize() override;

private:
	TSet<TWeakObjectPtr<UObject>> WidgetOwners;
};
