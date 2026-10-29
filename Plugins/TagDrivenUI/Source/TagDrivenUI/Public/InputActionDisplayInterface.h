#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InputActionDisplayInterface.generated.h"

class UInputAction;

// Keeps shared HUD widgets independent of the game's key display implementation.
UINTERFACE(BlueprintType)
class TAGDRIVENUI_API UInputActionDisplayInterface : public UInterface
{
	GENERATED_BODY()
};

class TAGDRIVENUI_API IInputActionDisplayInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "UI|Input")
	void SetDisplayedInputAction(UInputAction* InputAction);
};
