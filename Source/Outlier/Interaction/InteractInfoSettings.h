#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InteractInfoSettings.generated.h"

class UDataTable;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Interact Info Settings"))
class OUTLIER_API UInteractInfoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI")
	TSoftObjectPtr<UDataTable> InteractInfoTable;
};
