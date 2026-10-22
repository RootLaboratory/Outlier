#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElevatorShaftWall.generated.h"

class USceneComponent;

// 벽 패널과 조명을 배치하는 BP의 공통 기준. 실제 문은 별도 Child Actor로 보존한다.
UCLASS(Blueprintable)
class OUTLIER_API AElevatorShaftWall : public AActor
{
	GENERATED_BODY()

public:
	AElevatorShaftWall();
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elevator|Wall")
	TObjectPtr<USceneComponent> SceneRoot;

	// Harvest로 가져온 이 액터 소유의 시각 컴포넌트에만 적용한다. Child Actor의 문 충돌은 유지한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Wall")
	bool bApplyPresentationDefaults = true;

private:
	void ApplyPresentationDefaults();
};
