#include "Enemy/EnemyStateTreeComponent.h"

#include "Enemy/EnemyStateTreeSchema.h"

TSubclassOf<UStateTreeSchema> UEnemyStateTreeComponent::GetSchema() const
{
	return UEnemyStateTreeSchema::StaticClass();
}
