#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OutlierShieldMaterialCommandlet.generated.h"

// M_Master_Shield_Octagon_01에 내성 단계 색상 차오름 노드를 추가한다.
// Base_Color를 쓰던 입력을 Base_Color_Previous → Base_Color 아래→위 채움 결과로 바꾼다.
// 이미 적용된 머티리얼은 건드리지 않으므로 여러 번 실행해도 안전하다.
// 실행: UnrealEditor-Cmd.exe Outlier.uproject -run=OutlierShieldMaterial -unattended -nullrhi
UCLASS()
class UOutlierShieldMaterialCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOutlierShieldMaterialCommandlet();

	virtual int32 Main(const FString& Params) override;
};
