#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "VisualEventSettings.generated.h"

class UProjectionMarkDefinition;
class UTrailEffectDefinition;

USTRUCT(BlueprintType)
struct VISUALEVENT_API FTaggedVisualEventDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Event")
	TSoftObjectPtr<UProjectionMarkDefinition> DecalDef;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Event")
	TSoftObjectPtr<UTrailEffectDefinition> TrailEffectDef;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Visual Event"))
class VISUALEVENT_API UVisualEventSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 키는 정확히 일치하는 Context만 사용한다. 표면/Default 우선순위는 호출자가 전달한다.
	UPROPERTY(Config, EditAnywhere, Category = "Tagged Effects")
	TMap<FGameplayTag, FTaggedVisualEventDefinition> TaggedEffects;
};
