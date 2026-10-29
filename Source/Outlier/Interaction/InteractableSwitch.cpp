#include "Interaction/InteractableSwitch.h"

#include "Interaction/InteractableDoor.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void AInteractableSwitch::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshDoorPreview();
}

void AInteractableSwitch::RefreshDoorPreview()
{
	bool bPendingConstruction = false;
	ResolvedTargetDoor = ResolveTargetDoor(DoorBindingStatus, bPendingConstruction);
	if (ResolvedTargetDoor)
	{
		DoorBindingStatus = TEXT("Resolved. Preview only; interaction resolves the current door again.");
	}
}

AInteractableDoor* AInteractableSwitch::ResolveTargetDoor(FString& OutError, bool& bOutPendingConstruction) const
{
	OutError.Reset();
	bOutPendingConstruction = false;
	AInteractableDoor* Door = nullptr;
	switch (DoorSource)
	{
	case EInteractableSwitchDoorSource::DirectDoor:
		Door = TargetDoor;
		break;
	case EInteractableSwitchDoorSource::ElevatorEntrance:
	case EInteractableSwitchDoorSource::ElevatorArrival:
		if (!IsValid(TargetElevator) || TargetElevator->IsActorBeingDestroyed()
			|| TargetElevator->GetWorld() != GetWorld()
			|| (HasAuthority() && !TargetElevator->HasAuthority()))
		{
			OutError = TEXT("TargetElevator is missing, invalid, destroying, cross-world or non-authority.");
			return nullptr;
		}
		Door = TargetElevator->FindSwitchDoor(DoorSource == EInteractableSwitchDoorSource::ElevatorArrival,
			OutError, bOutPendingConstruction);
		if (!Door) return nullptr;
		break;
	case EInteractableSwitchDoorSource::ElevatorSection:
		if (!IsValid(TargetSection) || TargetSection->IsActorBeingDestroyed()
			|| TargetSection->GetWorld() != GetWorld())
		{
			OutError = TEXT("TargetSection is missing, invalid, destroying or cross-world.");
			return nullptr;
		}
		Door = TargetSection->FindCurrentDoor(OutError, bOutPendingConstruction);
		if (!Door)
		{
			OutError = FString::Printf(TEXT("Section=%s: %s"), *TargetSection->GetPathName(), *OutError);
			return nullptr;
		}
		break;
	default:
		OutError = TEXT("Unknown DoorSource value.");
		return nullptr;
	}
	if (!IsValid(Door) || Door->IsActorBeingDestroyed() || Door->GetWorld() != GetWorld()
		|| (HasAuthority() && !Door->HasAuthority()))
	{
		OutError = TEXT("Target door is missing, invalid, destroying, cross-world or non-authority.");
		return nullptr;
	}
	return Door;
}

bool AInteractableSwitch::ActivateTarget()
{
	if (!HasAuthority()) return false;

	// 비활성 방식의 참조로 대체하지 않는다. 재생성된 Child Actor는 이번 요청에서 다시 찾는다.
	FString Error;
	bool bPendingConstruction = false;
	AInteractableDoor* Door = ResolveTargetDoor(Error, bPendingConstruction);
	ResolvedTargetDoor = Door;
	DoorBindingStatus = Door ? TEXT("Resolved by server interaction.") : Error;
	if (!Door)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] Door request failed Switch=%s Source=%d Elevator=%s Section=%s: %s"),
			*GetPathName(), static_cast<int32>(DoorSource), *GetPathNameSafe(TargetElevator.Get()),
			*GetPathNameSafe(TargetSection.Get()), *Error);
		return false;
	}

	const bool bRequestedOpen = bCanToggleDoor ? !Door->IsDoorOpen() : true;
	if (!Door->TrySetDoorOpen(bRequestedOpen))
	{
		// 문이 거절한 요청은 스위치 활성화/저장/성공 연출로 이어지지 않는다.
		// 호출자의 기존 실패 경로가 Hold 상태를 정리하므로 별도 안내는 추가하지 않는다.
		UE_LOG(LogTemp, Warning, TEXT("[Switch] Door rejected request Switch=%s Source=%d Elevator=%s Section=%s Door=%s Open=%d"),
			*GetPathName(), static_cast<int32>(DoorSource), *GetPathNameSafe(TargetElevator.Get()),
			*GetPathNameSafe(TargetSection.Get()), *Door->GetPathName(), bRequestedOpen);
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult AInteractableSwitch::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	if (IsTemplate()) return Parent;

	FString Error;
	bool bPendingConstruction = false;
	if (!ResolveTargetDoor(Error, bPendingConstruction))
	{
		const FText Message = FText::FromString(FString::Printf(TEXT("Switch=%s Source=%d Elevator=%s Section=%s: %s"),
			*GetPathName(), static_cast<int32>(DoorSource), *GetPathNameSafe(TargetElevator.Get()),
			*GetPathNameSafe(TargetSection.Get()), *Error));
		if (bPendingConstruction)
		{
			Context.AddWarning(Message);
			return Parent;
		}
		Context.AddError(Message);
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Parent;
}
#endif
