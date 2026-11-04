#include "PostProcess/OutlierReadabilityTarget.h"
#include "PostProcess/OutlierStencilSubsystem.h"
#include "Engine/World.h"

#include "Components/MeshComponent.h"

void IOutlierReadabilityTarget::ApplyReadabilityStencil(
	UMeshComponent* Mesh, int32 StencilValue, bool bPreserveExistingStencil)
{
	// 기존 인자의 직렬화 및 API 호환성을 유지한다. 가독성 표현은
	// 예약 비트를 사용하므로 갱신 시 활성 스캔 및 외곽선 요청을 덮어쓰지 않는다.
	if (!UOutlierStencilSubsystem::IsEligibleMesh(Mesh) || !Mesh->GetWorld()) return;
	if (auto* Stencils = Mesh->GetWorld()->GetSubsystem<UOutlierStencilSubsystem>())
	{
		Stencils->RegisterReadability(Mesh);
	}
}
