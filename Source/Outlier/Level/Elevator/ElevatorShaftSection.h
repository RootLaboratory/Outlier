#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElevatorShaftSection.generated.h"

class AInteractableDoor;
class USceneComponent;
class UStaticMesh;

// 샤프트 중심 XY / 층 하단 Z를 원점으로 삼고 벽 BP를 Child Actor로 직접 조립한다.
UCLASS(Blueprintable)
class OUTLIER_API AElevatorShaftSection : public AActor
{
	GENERATED_BODY()

public:
	AElevatorShaftSection();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Elevator|Section")
	float GetSectionHeight() const;

	UFUNCTION(BlueprintCallable, Category = "Elevator|Section")
	void SetSectionVisible(bool bVisible);

	// 초기화 때만 조회한다. 중복 문 구성은 nullptr로 반환해 Controller가 시작 전에 거부한다.
	void RefreshChildActors();
	AInteractableDoor* GetEntranceDoor() const;
	bool HasEntranceDoor() const { return EntranceDoor.IsValid(); }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elevator|Section")
	TObjectPtr<USceneComponent> SceneRoot;

	// 층 전체 Bounds 대신 벽 한 장의 로컬 Z 높이만 사용한다. 입구/반복 BP에서 각 벽 메시를 지정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Section")
	TObjectPtr<UStaticMesh> HeightReferenceMesh = nullptr;

	// 실제 배치한 기준 벽 메시의 상대 Z 스케일과 맞춘다. Section 자체는 단위 스케일을 유지한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elevator|Section", meta = (ClampMin = "0.001", EditCondition = "HeightReferenceMesh != nullptr"))
	float HeightReferenceScaleZ = 1.0f;

private:
	TArray<TWeakObjectPtr<AActor>> SectionChildren;
	TWeakObjectPtr<AInteractableDoor> EntranceDoor;
	bool bHasMultipleDoors = false;
};
