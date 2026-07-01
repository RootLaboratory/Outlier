#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "EMPableInterface.generated.h"

class UEMPableComponent;

UINTERFACE(Blueprintable)
class UEMPableInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IEMPableInterface
{
	GENERATED_BODY()

public:
	virtual UEMPableComponent* GetEMPableComponent() const = 0;
	virtual void HandleEmp(FGameplayTag EffectTag) = 0;
};
