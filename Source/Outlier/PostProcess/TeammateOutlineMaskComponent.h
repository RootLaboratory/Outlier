#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TeammateOutlineMaskComponent.generated.h"

/** 해치 제외 깊이만 그리는 로컬 표현이다. 원본 메시의 표시·충돌·애니메이션은 변경하지 않는다. */
UCLASS(Transient, NotBlueprintable)
class OUTLIER_API UTeammateOutlineMaskComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UTeammateOutlineMaskComponent();

	/** 등록 전에 호출한다. 섹션 가시성 설정과 RegisterComponent는 소유하는 캐시에서 처리한다. */
	bool InitializeFrom(USkeletalMeshComponent* Source);
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
};
