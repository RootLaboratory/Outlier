#include "EnemyStateTreeSchema.h"

#include "Conditions/StateTreeAIConditionBase.h"
#include "EnemyBase.h"
#include "Tasks/StateTreeAITask.h"

UEnemyStateTreeSchema::UEnemyStateTreeSchema()
{
	ContextActorClass = AEnemyBase::StaticClass();
	GetContextActorDataDesc().Struct = ContextActorClass.Get();
}

bool UEnemyStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return Super::IsStructAllowed(InScriptStruct)
		|| InScriptStruct->IsChildOf(FStateTreeAITaskBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FStateTreeAIConditionBase::StaticStruct());
}
