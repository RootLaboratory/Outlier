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
	// 관리되는 스텐실 규약의 가독성 비트다. 독점 ID로 사용하지 않는다.
	static constexpr int32 DefaultReadabilityStencilValue = 16;

	/** 일시적 요청을 덮어쓰지 않고 가독성을 등록한다. 기존 인자는 유지한다. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "PostProcess|Readability")
	void InitializeReadabilityStencil(bool bPreserveExistingStencil = false);

	/** Applies only to physical world meshes, excluding widgets and owner-only view models. */
	static void ApplyReadabilityStencil(UMeshComponent* Mesh, int32 StencilValue, bool bPreserveExistingStencil);
};
