#include "Impact/ImpactFieldSubsystem.h"

#include "Engine/World.h"

void UImpactFieldSubsystem::RegisterImpact(FVector Position, FVector ScatterDir, FVector Normal,
	float Radius, float Strength, float Duration)
{
	const UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}

	FImpactEvent Event;
	Event.Position = Position;

	Event.ScatterDir = ScatterDir.GetSafeNormal();
	if (Event.ScatterDir.IsNearlyZero())
	{
		Event.ScatterDir = FVector::UpVector;
	}

	Event.Normal = Normal.GetSafeNormal();
	if (Event.Normal.IsNearlyZero())
	{
		Event.Normal = FVector::UpVector;
	}

	Event.Radius = FMath::Max(Radius, 1.f);
	Event.Strength = Strength;
	Event.Duration = FMath::Max(Duration, 0.f);
	Event.SpawnTime = W->GetTimeSeconds();

	// 상한 초과 시 가장 오래된 것부터 폐기 (분산 동시 폭발이 몰려도 N 상한 유지)
	if (ActiveImpacts.Num() >= MaxImpacts)
	{
		ActiveImpacts.RemoveAt(0, 1, EAllowShrinking::No);
	}
	ActiveImpacts.Add(Event);
}

void UImpactFieldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* W = GetWorld();
	const float Now = W ? W->GetTimeSeconds() : 0.f;

	ActiveImpacts.RemoveAll([Now](const FImpactEvent& E)
	{
		return (Now - E.SpawnTime) > E.Duration;
	});
}

void UImpactFieldSubsystem::BuildArrays(
	TArray<FVector>& OutPositions,
	TArray<FVector>& OutScatterDirs,
	TArray<FVector>& OutNormals,
	TArray<float>& OutRadii,
	TArray<float>& OutStrengths,
	TArray<float>& OutAges) const
{
	const int32 Num = ActiveImpacts.Num();
	OutPositions.Reset(Num);
	OutScatterDirs.Reset(Num);
	OutNormals.Reset(Num);
	OutRadii.Reset(Num);
	OutStrengths.Reset(Num);
	OutAges.Reset(Num);

	const UWorld* W = GetWorld();
	const float Now = W ? W->GetTimeSeconds() : 0.f;

	for (const FImpactEvent& E : ActiveImpacts)
	{
		OutPositions.Add(E.Position);
		OutScatterDirs.Add(E.ScatterDir);
		OutNormals.Add(E.Normal);
		OutRadii.Add(E.Radius);
		OutStrengths.Add(E.Strength);
		OutAges.Add(Now - E.SpawnTime);
	}
}
