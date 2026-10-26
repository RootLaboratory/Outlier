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

	// 중첩 Child Actor의 메시/라이트까지 적용하되, 문과 전투 차단막 계층은 제외한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Wall")
	bool bApplyPresentationDefaults = true;

private:
	void ApplyPresentationDefaults();
};
