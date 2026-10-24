#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ActorBasedWidgetInterface.generated.h"

UINTERFACE(BlueprintType)
class OUTLIER_API UActorBasedWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by actors or components that own gameplay widgets outside the UI layer stack. */
class OUTLIER_API IActorBasedWidgetInterface
{
	GENERATED_BODY()

public:
	/** Dismiss presentation only; do not complete, cancel, or reset gameplay progress. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI|Actor Based Widgets")
	void DismissActorBasedWidgets();
};
