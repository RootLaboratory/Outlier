#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialComponent.h"
#include "ShooterTutorialComponent.generated.h"

UCLASS(Blueprintable, ClassGroup = (Tutorial), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UShooterTutorialComponent : public UTutorialComponent
{
	GENERATED_BODY()

protected:
	virtual bool SupportsOwner(const APawn* Pawn) const override;
};
