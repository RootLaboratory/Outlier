#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UILayerInputReceiver.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class OUTLIER_API UUILayerInputReceiver : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Input")
	bool HandleUILayerEscape();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Input")
	bool HandleUILayerConfirmed();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Input")
	bool HandleUILayerUp();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Input")
	bool HandleUILayerDown();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Input")
	bool HandleUILayerLeft();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "UI Layer|Input")
	bool HandleUILayerRight();
};
