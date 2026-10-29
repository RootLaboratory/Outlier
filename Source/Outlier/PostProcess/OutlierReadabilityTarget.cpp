#include "PostProcess/OutlierReadabilityTarget.h"

#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"

void IOutlierReadabilityTarget::ApplyReadabilityStencil(
	UMeshComponent* Mesh, int32 StencilValue, bool bPreserveExistingStencil)
{
	if (!IsValid(Mesh) || (!Mesh->IsA<USkinnedMeshComponent>() && !Mesh->IsA<UStaticMeshComponent>())
		|| Mesh->bOnlyOwnerSee || !Mesh->bRenderInMainPass)
	{
		return;
	}

	// A suit swaps assets on the same component. Keep temporary scan/outline values and their
	// existing restore snapshots intact; a new component with no stencil still gets initialized.
	if (bPreserveExistingStencil && Mesh->bRenderCustomDepth && Mesh->CustomDepthStencilValue != 0
		&& Mesh->CustomDepthStencilValue != StencilValue)
	{
		return;
	}

	Mesh->SetCustomDepthStencilValue(FMath::Clamp(StencilValue, 1, 255));
	Mesh->SetRenderCustomDepth(true);
}
