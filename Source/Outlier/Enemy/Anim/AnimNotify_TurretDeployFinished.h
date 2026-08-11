#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_TurretDeployFinished.generated.h"

/** 터렛 전개 애니메이션이 끝난 시점에 서버의 Deploy 상태 완료를 요청한다. */
UCLASS()
class OUTLIER_API UAnimNotify_TurretDeployFinished : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
