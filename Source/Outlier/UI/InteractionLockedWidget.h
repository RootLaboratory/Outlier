#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractionLockedWidget.generated.h"

/** Inner-sphere locked prompt, independent of the interaction key widget. */
UCLASS()
class OUTLIER_API UInteractionLockedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowLockedPrompt();
	void HideLockedPrompt();
};
