#pragma once

#include "CoreMinimal.h"
#include "UI/UpgradeMachineWidget.h"
#include "UpgradeMachineTestWidget.generated.h"

// 실제 화면 에셋 없이 대기 상태와 입력 경계를 검증하는 구체 위젯이다.
UCLASS(Transient, NotBlueprintable)
class UUpgradeMachineTestWidget : public UUpgradeMachineWidget
{
	GENERATED_BODY()
};