#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OutlierHackTargetPointCommandlet.generated.h"

// 기본 실행은 점검만 한다. -Apply로 이전하고, 새 프로세스에서 -Baseline=<Apply 보고서>로 재로드를 검증한다.
UCLASS()
class UOutlierHackTargetPointCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOutlierHackTargetPointCommandlet();
	virtual int32 Main(const FString& Params) override;
};
