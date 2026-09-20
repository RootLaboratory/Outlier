#include "Room/RoomCombatDefinition.h"
#include "Enemy/EnemyBase.h"

namespace
{
	bool IsValidRemainingRatio(float Ratio)
	{
		return FMath::IsFinite(Ratio) && Ratio >= 0.0f && Ratio <= 1.0f;
	}
}

bool FRoomCombatRoomDefinition::HasValidPhaseOrder() const
{
	if (CombatPhases.IsEmpty())
	{
		return false;
	}
	// 배치 전투는 맨 앞에만 둘 수 있다. 해킹 이후 차수는 추가 해킹 없이 자동으로 이어진다.
	bool bSeenHackTrigger = false;
	for (int32 Index = 0; Index < CombatPhases.Num(); ++Index)
	{
		switch (CombatPhases[Index].StartPolicy)
		{
		case ERoomCombatPhaseStartPolicy::InitialDetection:
			if (Index != 0)
			{
				return false;
			}
			break;
		case ERoomCombatPhaseStartPolicy::HackTrigger:
			if (bSeenHackTrigger)
			{
				return false;
			}
			bSeenHackTrigger = true;
			break;
		case ERoomCombatPhaseStartPolicy::Automatic:
			if (!bSeenHackTrigger)
			{
				return false;
			}
			break;
		default:
			return false;
		}
	}
	return true;
}

bool FRoomCombatRoomDefinition::CanStartTriggeredSequence(int32 PhaseIndex) const
{
	if (!HasValidPhaseOrder() || !CombatPhases.IsValidIndex(PhaseIndex)
		|| CombatPhases[PhaseIndex].StartPolicy != ERoomCombatPhaseStartPolicy::HackTrigger)
	{
		return false;
	}
	// 뒤 차수의 잘못된 명단 때문에 출입을 막은 뒤 진행할 수 없게 되는 것을 시작 전에 걸러낸다.
	for (int32 Index = PhaseIndex; Index < CombatPhases.Num(); ++Index)
	{
		const FRoomCombatPhaseDefinition& Phase = CombatPhases[Index];
		if (Phase.Waves.IsEmpty())
		{
			return false;
		}
		for (const FRoomCombatWaveDefinition& Wave : Phase.Waves)
		{
			if (Wave.SpawnMode != ERoomCombatWaveSpawnMode::SpawnFromObjects
				|| Wave.Enemies.IsEmpty() || !IsValidRemainingRatio(Wave.NextWaveRemainingRatio))
			{
				return false;
			}
			for (const FRoomCombatEnemyEntry& Entry : Wave.Enemies)
			{
				UClass* EnemyClass = Entry.EnemyClass.LoadSynchronous();
				if (Entry.Count < 1 || !EnemyClass || !EnemyClass->IsChildOf(AEnemyBase::StaticClass())
					|| EnemyClass->HasAnyClassFlags(CLASS_Abstract))
				{
					return false;
				}
			}
		}
	}
	return true;
}

const FRoomCombatRoomDefinition* URoomCombatDefinition::FindRoomDefinition(FGameplayTag RoomTag) const
{
	if (!RoomTag.IsValid())
	{
		return nullptr;
	}

	return RoomDefinitions.FindByPredicate([RoomTag](const FRoomCombatRoomDefinition& Definition)
	{
		// Room 소유권은 부모 Tag까지 넓히지 않는다. 한 RoomTag는 정확히 한 설정만 가져야 한다.
		return Definition.RoomTag == RoomTag;
	});
}

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

	if (RoomDefinitions.IsEmpty())
	{
		AddValidationError(TEXT("Room Combat Definition requires at least one RoomDefinition."));
	}

	TSet<FGameplayTag> SeenRoomTags;
	for (int32 RoomIndex = 0; RoomIndex < RoomDefinitions.Num(); ++RoomIndex)
	{
		const FRoomCombatRoomDefinition& RoomDefinition = RoomDefinitions[RoomIndex];
		if (!RoomDefinition.RoomTag.IsValid())
		{
			AddValidationError(FString::Printf(
				TEXT("RoomDefinitions[%d] requires a valid RoomTag."),
				RoomIndex));
		}
		else if (SeenRoomTags.Contains(RoomDefinition.RoomTag))
		{
			AddValidationError(FString::Printf(
				TEXT("RoomDefinitions[%d] has duplicate RoomTag %s."),
				RoomIndex,
				*RoomDefinition.RoomTag.ToString()));
		}
		else
		{
			SeenRoomTags.Add(RoomDefinition.RoomTag);
		}

		if (RoomDefinition.CombatPhases.IsEmpty())
		{
			AddValidationError(FString::Printf(
				TEXT("RoomDefinitions[%d] requires at least one CombatPhase."),
				RoomIndex));
			continue;
		}
		if (!RoomDefinition.HasValidPhaseOrder())
		{
			AddValidationError(FString::Printf(
				TEXT("RoomDefinitions[%d].CombatPhases must use optional InitialDetection, then HackTrigger followed only by Automatic phases."),
				RoomIndex));
		}

		for (int32 PhaseIndex = 0; PhaseIndex < RoomDefinition.CombatPhases.Num(); ++PhaseIndex)
		{
			const FRoomCombatPhaseDefinition& Phase = RoomDefinition.CombatPhases[PhaseIndex];
			if (Phase.Waves.IsEmpty())
			{
				AddValidationError(FString::Printf(
					TEXT("RoomDefinitions[%d].CombatPhases[%d] requires at least one Wave."),
					RoomIndex,
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
					TEXT("RoomDefinitions[%d].CombatPhases[%d].Waves[0] has a SpawnMode that does not match its StartPolicy."),
					RoomIndex,
					PhaseIndex));
			}

			for (int32 WaveIndex = 0; WaveIndex < Phase.Waves.Num(); ++WaveIndex)
			{
				const FRoomCombatWaveDefinition& Wave = Phase.Waves[WaveIndex];
				if (WaveIndex > 0 && Wave.SpawnMode != ERoomCombatWaveSpawnMode::SpawnFromObjects)
				{
					AddValidationError(FString::Printf(
						TEXT("RoomDefinitions[%d].CombatPhases[%d].Waves[%d] must use SpawnFromObjects after the first Wave."),
						RoomIndex,
						PhaseIndex,
						WaveIndex));
				}

				if (Wave.SpawnMode == ERoomCombatWaveSpawnMode::SpawnFromObjects && Wave.Enemies.IsEmpty())
				{
					AddValidationError(FString::Printf(
						TEXT("RoomDefinitions[%d].CombatPhases[%d].Waves[%d] requires at least one Enemy when using SpawnFromObjects."),
						RoomIndex,
						PhaseIndex,
						WaveIndex));
				}

				if (!IsValidRemainingRatio(Wave.NextWaveRemainingRatio))
				{
					AddValidationError(FString::Printf(
						TEXT("RoomDefinitions[%d].CombatPhases[%d].Waves[%d] has NextWaveRemainingRatio outside 0.0 to 1.0."),
						RoomIndex,
						PhaseIndex,
						WaveIndex));
				}

				for (int32 EnemyIndex = 0; EnemyIndex < Wave.Enemies.Num(); ++EnemyIndex)
				{
					const FRoomCombatEnemyEntry& Enemy = Wave.Enemies[EnemyIndex];
					if (Enemy.EnemyClass.IsNull())
					{
						AddValidationError(FString::Printf(
							TEXT("RoomDefinitions[%d].CombatPhases[%d].Waves[%d].Enemies[%d] has no EnemyClass."),
							RoomIndex,
							PhaseIndex,
							WaveIndex,
							EnemyIndex));
					}

					if (Enemy.Count <= 0)
					{
						AddValidationError(FString::Printf(
							TEXT("RoomDefinitions[%d].CombatPhases[%d].Waves[%d].Enemies[%d] has a non-positive Count."),
							RoomIndex,
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
