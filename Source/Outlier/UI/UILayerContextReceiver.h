#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UILayerContextReceiver.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class OUTLIER_API UUILayerContextReceiver : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IUILayerContextReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Context")
	void InitializeUILayerContext(const TArray<AActor*>& ContextActors);
};
