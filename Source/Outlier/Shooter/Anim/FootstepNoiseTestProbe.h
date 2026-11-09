#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
class USkeletalMeshComponent;

namespace OutlierFootstepTesting
{
	// 실제 서버 청각 보고만 관측한다. Notify나 감지 결과를 테스트에서 대신 생성하지 않는다.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FReported, USkeletalMeshComponent*, const FVector&);
	FReported& Reported();
}
#endif
