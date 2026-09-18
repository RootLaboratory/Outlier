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

	if (CombatPhases.IsEmpty())
	{
		AddValidationError(TEXT("Room Combat Definition requires at least one CombatPhase."));
	}

	for (int32 PhaseIndex = 0; PhaseIndex < CombatPhases.Num(); ++PhaseIndex)
	{
		const FRoomCombatPhaseDefinition& Phase = CombatPhases[PhaseIndex];
		if (Phase.Waves.IsEmpty())
		{
			AddValidationError(FString::Printf(
				TEXT("CombatPhases[%d] requires at least one Wave."),
				PhaseIndex));
			continue;
		}

		const ERoomCombatWaveSpawnMode RequiredFirstWaveMode =
			Phase.StartPolicy == ERoomCombatPhaseStartPolicy::InitialDetection
				? ERoomCombatWaveSpawnMode::Preplaced
				: ERoomCombatWaveSpawnMode::SpawnFromObjects;
		if (Phase.Waves[0].SpawnMode != RequiredFirstWaveMode)
		{
			AddValidationError(FString::Printf(
				TEXT("CombatPhases[%d].Waves[0] has a SpawnMode that does not match its StartPolicy."),
				PhaseIndex));
		}

		for (int32 WaveIndex = 0; WaveIndex < Phase.Waves.Num(); ++WaveIndex)
		{
			const FRoomCombatWaveDefinition& Wave = Phase.Waves[WaveIndex];
			if (WaveIndex > 0 && Wave.SpawnMode != ERoomCombatWaveSpawnMode::SpawnFromObjects)
			{
				AddValidationError(FString::Printf(
					TEXT("CombatPhases[%d].Waves[%d] must use SpawnFromObjects after the first Wave."),
					PhaseIndex,
					WaveIndex));
			}

			if (Wave.SpawnMode == ERoomCombatWaveSpawnMode::SpawnFromObjects && Wave.Enemies.IsEmpty())
			{
				AddValidationError(FString::Printf(
					TEXT("CombatPhases[%d].Waves[%d] requires at least one Enemy when using SpawnFromObjects."),
					PhaseIndex,
					WaveIndex));
			}

			if (!FMath::IsFinite(Wave.NextWaveRemainingRatio)
				|| Wave.NextWaveRemainingRatio < 0.0f
				|| Wave.NextWaveRemainingRatio > 1.0f)
			{
				AddValidationError(FString::Printf(
					TEXT("CombatPhases[%d].Waves[%d] has NextWaveRemainingRatio outside 0.0 to 1.0."),
					PhaseIndex,
					WaveIndex));
			}

			for (int32 EnemyIndex = 0; EnemyIndex < Wave.Enemies.Num(); ++EnemyIndex)
			{
				const FRoomCombatEnemyEntry& Enemy = Wave.Enemies[EnemyIndex];
				if (Enemy.EnemyClass.IsNull())
				{
					AddValidationError(FString::Printf(
						TEXT("CombatPhases[%d].Waves[%d].Enemies[%d] has no EnemyClass."),
						PhaseIndex,
						WaveIndex,
						EnemyIndex));
				}

				if (Enemy.Count <= 0)
				{
					AddValidationError(FString::Printf(
						TEXT("CombatPhases[%d].Waves[%d].Enemies[%d] has a non-positive Count."),
						PhaseIndex,
						WaveIndex,
						EnemyIndex));
				}
			}
		}
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
