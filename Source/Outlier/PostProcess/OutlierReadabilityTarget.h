#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OutlierReadabilityTarget.generated.h"

class UMeshComponent;

UINTERFACE(MinimalAPI, Blueprintable)
class UOutlierReadabilityTarget : public UInterface
{
	GENERATED_BODY()
};

/** Actors initialize their own world meshes. The PP volume only supplies the material. */
class OUTLIER_API IOutlierReadabilityTarget
{
	GENERATED_BODY()

public:
	// 1/2: scan/melee, 3: ADS, 8: world guide. Shared by all readability targets.
	static constexpr int32 DefaultReadabilityStencilValue = 16;

	/** Preserve an active override when refreshing an existing mesh after a presentation change. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "PostProcess|Readability")
	void InitializeReadabilityStencil(bool bPreserveExistingStencil = false);

	/** Applies only to physical world meshes, excluding widgets and owner-only view models. */
	static void ApplyReadabilityStencil(UMeshComponent* Mesh, int32 StencilValue, bool bPreserveExistingStencil);
};
