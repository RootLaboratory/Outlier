#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OutlierNiagaraTrailCommandlet.generated.h"

// OutlierGunTrail 의 Lyra 트레이서( NS_Trail_*_Original )를 VisualEvent 트레일 구조( User.Start / User.End )로 맞춘다.
// -dump 는 그래프 구조만 로그로 남긴다. list / convert 는 스택 뷰모델이 Slate 를 요구해서 에디터 콘솔 명령으로 돌린다.
// 실행: UnrealEditor-Cmd.exe Outlier.uproject -run=OutlierNiagaraTrail -dump -unattended
//       UnrealEditor-Cmd.exe Outlier.uproject -ExecCmds="Outlier.NiagaraTrail convert; Quit" -unattended -RenderOffscreen
UCLASS()
class UOutlierNiagaraTrailCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOutlierNiagaraTrailCommandlet();

	virtual int32 Main(const FString& Params) override;
};
