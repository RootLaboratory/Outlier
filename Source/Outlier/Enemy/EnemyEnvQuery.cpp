#include "Enemy/EnemyEnvQuery.h"

#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"

namespace
{
bool CanProvideVisibleTarget(const AEnemyBase* Enemy)
{
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->CanUseEnemyPerception())
	{
		return false;
	}
	return !Enemy->IsDead() && Enemy->GetCombatState() != EEnemyCombatState::Stun
		&& Enemy->IsPlayerCurrentlyVisible();
}

AActor* GetVisibleEnvQueryTarget(const AEnemyBase* Enemy)
{
	if (!CanProvideVisibleTarget(Enemy))
	{
		return nullptr;
	}
	const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	return Controller ? Controller->GetCachedVisibleTarget() : nullptr;
}

bool CanCheckFlightDestination(const AEnemyBase* Enemy, const UWorld* World, const UCapsuleComponent* Capsule)
{
	if (!Enemy || !Enemy->HasAuthority() || !World || !Capsule)
	{
		return false;
	}
	return !Enemy->IsDead() && Enemy->CanUseEnemyPerception();
}
}

void UEnemyEnvQueryContext_VisibleTarget::ProvideContext(
	FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	if (AActor* Target = GetVisibleEnvQueryTarget(Enemy))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, Target);
	}
}

UEnemyEnvQueryTest_FlightDestination::UEnemyEnvQueryTest_FlightDestination()
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(false);
	TestPurpose = EEnvTestPurpose::Filter;
	BoolValue.DefaultValue = true;
}

void UEnemyEnvQueryTest_FlightDestination::RunTest(FEnvQueryInstance& QueryInstance) const
{
	// 한 검사 실행에서만 참조를 조회한다. 후보마다 Subsystem/Controller를 다시 검색하지 않는다.
	AEnemyBase* Enemy = Cast<AEnemyBase>(QueryInstance.Owner.Get());
	UWorld* World = QueryInstance.World;
	UCapsuleComponent* Capsule = Enemy ? Enemy->GetCapsuleComponent() : nullptr;
	URoomCombatSubsystem* Rooms = World ? World->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	FFlightPathRequest Request;
	Request.Clearance = Clearance;
	Request.bRequireRoom = true;
	Request.Room = Rooms && Enemy ? Rooms->GetRegisteredRoomVolume(Enemy->GetDefaultRoomTag()) : nullptr;
	const FVector CapsuleOffset = Capsule && Enemy
		? Capsule->GetComponentLocation() - Enemy->GetActorLocation() : FVector::ZeroVector;
	BoolValue.BindData(QueryInstance.Owner.Get(), QueryInstance.QueryID);
	const bool bWantsValidDestination = BoolValue.GetValue();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		bool bClear = false;
		if (CanCheckFlightDestination(Enemy, World, Capsule))
		{
			Request.Start = Capsule->GetComponentLocation();
			Request.Goal = GetItemLocation(QueryInstance, It.GetIndex()) + CapsuleOffset;
			Request.SearchBounds = FBox(Request.Start, Request.Start);
			Request.SearchBounds += Request.Goal;
			bClear = FlightPathfinding::CheckDestination(*World, *Capsule, Request) == EFlightPathStatus::Success;
		}
		It.SetScore(TestPurpose, FilterType, bClear, bWantsValidDestination);
	}
}
