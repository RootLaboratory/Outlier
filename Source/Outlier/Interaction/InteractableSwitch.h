#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Interaction/InteractableSwitchBase.h"
#include "InteractableSwitch.generated.h"

class AInteractableDoor;
class AElevatorShaftController;
class AElevatorShaftSection;
class URoomCombatSubsystem;

UENUM(BlueprintType)
enum class EInteractableSwitchDoorSource : uint8
{
	DirectDoor = 0,
	ElevatorEntrance,
	ElevatorArrival,
	ElevatorSection
};

/**
 * 문 전용 스위치. 해킹 여부는 BP 태그로 나뉜다(BP_DoorSwitch: 일반, BP_LevelDoorSwitch: 해킹 잠금).
 * 클래스/프로퍼티 이름은 레벨에 배치된 TargetDoor 참조를 유지하려고 그대로 둔다.
 */
UCLASS()
class OUTLIER_API AInteractableSwitch : public AInteractableSwitchBase
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual bool ActivateTarget() override;

public:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch")
	EInteractableSwitchDoorSource DoorSource = EInteractableSwitchDoorSource::DirectDoor;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch", meta = (EditCondition = "DoorSource == EInteractableSwitchDoorSource::DirectDoor"))
	TObjectPtr<AInteractableDoor> TargetDoor;

	// 지정한 방의 출입이 봉쇄된 동안 이 스위치의 조작을 막는다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch", meta = (Categories = "Room"))
	FGameplayTag CombatRoomTag;

	// 공유 출입구에서는 주 Room과 이 목록 중 조작자가 현재 있는 방을 검사한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch", meta = (Categories = "Room"))
	FGameplayTagContainer AdditionalCombatRoomTags;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch", meta = (EditCondition = "DoorSource == EInteractableSwitchDoorSource::ElevatorEntrance || DoorSource == EInteractableSwitchDoorSource::ElevatorArrival"))
	TObjectPtr<AElevatorShaftController> TargetElevator;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch", meta = (EditCondition = "DoorSource == EInteractableSwitchDoorSource::ElevatorSection"))
	TObjectPtr<AElevatorShaftSection> TargetSection;

	// 표시용 결과다. 실제 요청은 이 참조를 사용하지 않고 현재 계층을 다시 조회한다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, Category = "Switch")
	TObjectPtr<AInteractableDoor> ResolvedTargetDoor;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, Category = "Switch")
	FString DoorBindingStatus;

	// 다른 BP의 자식 구성을 바꾼 경우에도 Tick 없이 표시를 다시 확인할 수 있다.
	UFUNCTION(CallInEditor, Category = "Switch")
	void RefreshDoorPreview();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bCanToggleDoor = true;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	AInteractableDoor* ResolveTargetDoor(FString& OutError, bool& bOutPendingConstruction) const;
	TWeakObjectPtr<URoomCombatSubsystem> RoomCombatSubsystem;
};
