#include "Impact/ImpactTestEnemy.h"

#include "Impact/ImpactFieldSubsystem.h"
#include "Engine/World.h"

void AImpactTestEnemy::HandleDeath()
{
	Super::HandleDeath();

	UWorld* W = GetWorld();
	UImpactFieldSubsystem* Impact = W ? W->GetSubsystem<UImpactFieldSubsystem>() : nullptr;

	if (!Impact)
	{
		return;
	}

	const FVector Pos = GetActorLocation();
	const FVector ScatterDir = GetActorTransform().TransformVectorNoScale(LocalScatterDir);
	const FVector Normal = FVector::UpVector; // 바닥 노멀 알면 대입 (지금은 위로)

	Impact->RegisterImpact(Pos, ScatterDir, Normal, ImpactRadius, ImpactStrength, ImpactDuration);
}
