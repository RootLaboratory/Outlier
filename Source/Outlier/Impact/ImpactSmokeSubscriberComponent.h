// 연기 Niagara에 붙이는 구독자 컴포넌트.
// 매 프레임 UImpactFieldSubsystem에서 활성 임팩트를 읽어, 대상 Niagara의 Array DI User 파라미터에 push.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ImpactSmokeSubscriberComponent.generated.h"

class UNiagaraComponent;

UCLASS(ClassGroup = (FX), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UImpactSmokeSubscriberComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UImpactSmokeSubscriberComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 임팩트 배열을 받을 Niagara. 비워두면 BeginPlay에서 소유 액터의 UNiagaraComponent 자동 탐색.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TObjectPtr<UNiagaraComponent> TargetNiagara;

	// Niagara System에 노출한 Array DI User 파라미터 이름들 (에디터에서 만든 이름과 일치시킬 것)
	UPROPERTY(EditAnywhere, Category = "Impact|Param Names")
	FName PositionsParam = TEXT("ImpactPositions");

	UPROPERTY(EditAnywhere, Category = "Impact|Param Names")
	FName ScatterDirsParam = TEXT("ImpactScatterDirs");

	UPROPERTY(EditAnywhere, Category = "Impact|Param Names")
	FName NormalsParam = TEXT("ImpactNormals");

	UPROPERTY(EditAnywhere, Category = "Impact|Param Names")
	FName RadiiParam = TEXT("ImpactRadii");

	UPROPERTY(EditAnywhere, Category = "Impact|Param Names")
	FName StrengthsParam = TEXT("ImpactStrengths");

	UPROPERTY(EditAnywhere, Category = "Impact|Param Names")
	FName AgesParam = TEXT("ImpactAges");

private:
	// 직전 프레임에 push한 개수. 0 → 0이면 스킵해서 불필요한 set 방지.
	int32 LastPushedCount = -1;
};
