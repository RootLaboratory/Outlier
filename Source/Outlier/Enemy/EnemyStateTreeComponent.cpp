#include "Enemy/EnemyStateTreeComponent.h"

#include "Enemy/EnemyStateTreeSchema.h"

TSubclassOf<UStateTreeSchema> UEnemyStateTreeComponent::GetSchema() const
{
	return UEnemyStateTreeSchema::StaticClass();
}

const FStateTreeReference* UEnemyStateTreeComponent::FindLinkedStateTreeOverride(const FGameplayTag StateTag) const
{
	for (const FStateTreeReferenceOverrideItem& Override : LinkedStateTreeOverrides.GetOverrideItems())
	{
		if (Override.GetStateTag() == StateTag)
		{
			return &Override.GetStateTreeReference();
		}
	}

	return nullptr;
}
