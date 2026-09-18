#include "Room/RoomCombatDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult URoomCombatDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	auto AddValidationError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	if (Floors.IsEmpty())
	{
		AddValidationError(TEXT("Room Combat Definition requires at least one Floor."));
	}

	TSet<FGameplayTag> SeenFloorTags;
	for (int32 FloorIndex = 0; FloorIndex < Floors.Num(); ++FloorIndex)
	{
		const FRoomCombatFloorDefinition& Floor = Floors[FloorIndex];
		if (!Floor.FloorTag.IsValid())
		{
			AddValidationError(FString::Printf(
				TEXT("Floors[%d] requires a valid FloorTag."),
				FloorIndex));
		}
		else if (SeenFloorTags.Contains(Floor.FloorTag))
		{
			AddValidationError(FString::Printf(
				TEXT("Floors[%d] duplicates FloorTag '%s'."),
				FloorIndex,
				*Floor.FloorTag.ToString()));
		}
		else
		{
			SeenFloorTags.Add(Floor.FloorTag);
		}

		if (Floor.CombatPhases.IsEmpty())
		{
			AddValidationError(FString::Printf(
				TEXT("Floors[%d] requires at least one CombatPhase."),
				FloorIndex));
		}

		for (int32 PhaseIndex = 0; PhaseIndex < Floor.CombatPhases.Num(); ++PhaseIndex)
		{
			const FRoomCombatPhaseDefinition& Phase = Floor.CombatPhases[PhaseIndex];
			if (Phase.Waves.IsEmpty())
			{
				AddValidationError(FString::Printf(
					TEXT("Floors[%d].CombatPhases[%d] requires at least one Wave."),
					FloorIndex,
					PhaseIndex));
			}

			for (int32 WaveIndex = 0; WaveIndex < Phase.Waves.Num(); ++WaveIndex)
			{
				const FRoomCombatWaveDefinition& Wave = Phase.Waves[WaveIndex];
				if (!FMath::IsFinite(Wave.NextWaveRemainingRatio)
					|| Wave.NextWaveRemainingRatio < 0.0f
					|| Wave.NextWaveRemainingRatio > 1.0f)
				{
					AddValidationError(FString::Printf(
						TEXT("Floors[%d].CombatPhases[%d].Waves[%d] has NextWaveRemainingRatio outside 0.0 to 1.0."),
						FloorIndex,
						PhaseIndex,
						WaveIndex));
				}

				for (int32 EnemyIndex = 0; EnemyIndex < Wave.Enemies.Num(); ++EnemyIndex)
				{
					const FRoomCombatEnemyEntry& Enemy = Wave.Enemies[EnemyIndex];
					if (Enemy.EnemyClass.IsNull())
					{
						AddValidationError(FString::Printf(
							TEXT("Floors[%d].CombatPhases[%d].Waves[%d].Enemies[%d] has no EnemyClass."),
							FloorIndex,
							PhaseIndex,
							WaveIndex,
							EnemyIndex));
					}

					if (Enemy.Count <= 0)
					{
						AddValidationError(FString::Printf(
							TEXT("Floors[%d].CombatPhases[%d].Waves[%d].Enemies[%d] has a non-positive Count."),
							FloorIndex,
							PhaseIndex,
							WaveIndex,
							EnemyIndex));
					}
				}
			}
		}
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
