#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialComponent.h"
#include "PartnerTutorialComponent.generated.h"

UCLASS(Blueprintable, ClassGroup = (Tutorial), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerTutorialComponent : public UTutorialComponent
{
	GENERATED_BODY()

protected:
	virtual bool SupportsOwner(const APawn* Pawn) const override;
};
