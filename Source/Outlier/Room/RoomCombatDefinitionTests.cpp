#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Room/RoomCombatDefinition.h"

namespace
{
	FRoomCombatEnemyEntry MakeValidEnemyEntry()
	{
		FRoomCombatEnemyEntry Enemy;
		Enemy.EnemyClass = AEnemyBase::StaticClass();
		Enemy.Count = 2;
		return Enemy;
	}

	FRoomCombatWaveDefinition MakeSpawnWave()
	{
		FRoomCombatWaveDefinition Wave;
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Wave.NextWaveRemainingRatio = 0.5f;
		Wave.Enemies.Add(MakeValidEnemyEntry());
		return Wave;
	}

	URoomCombatDefinition* MakeValidInitialDetectionDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();

		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();
		Phase.Waves.Add(MakeSpawnWave());

		Definition->CombatPhases.Add(Phase);
		return Definition;
	}

	URoomCombatDefinition* MakeValidHackTriggerDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();

		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
		Phase.Waves.Add(MakeSpawnWave());

		Definition->CombatPhases.Add(Phase);
		return Definition;
	}

	EDataValidationResult ValidateDefinition(
		const URoomCombatDefinition* Definition,
		FDataValidationContext& Context)
	{
		return Definition->IsDataValid(Context);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatDefinitionValidationTest,
	"Outlier.Room.CombatDefinition.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatDefinitionValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		FRoomCombatPhaseDefinition Automatic = Definition->CombatPhases[0];
		Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Definition->CombatPhases.Add(Automatic);
		Definition->CombatPhases.Add(Automatic);
		FDataValidationContext Context;
		TestEqual(TEXT("A hack followed by automatic phases is valid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestTrue(TEXT("A valid sequence can start at runtime"), Definition->CanStartTriggeredSequence(0));
		TestFalse(TEXT("An automatic phase cannot be externally triggered"), Definition->CanStartTriggeredSequence(1));

		Definition->CombatPhases[2].Waves[0].Enemies[0].Count = 0;
		TestFalse(TEXT("Invalid later rosters reject the sequence before locking exits"),
			Definition->CanStartTriggeredSequence(0));
	}
	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->CombatPhases[0].StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		FDataValidationContext Context;
		TestEqual(TEXT("An automatic phase cannot be first"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}
	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		FRoomCombatPhaseDefinition Automatic;
		Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Automatic.Waves.Add(MakeSpawnWave());
		Definition->CombatPhases.Add(Automatic);
		FDataValidationContext Context;
		TestEqual(TEXT("Automatic progression requires a preceding hack"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}
	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		const FRoomCombatPhaseDefinition DuplicateHack = Definition->CombatPhases[0];
		Definition->CombatPhases.Add(DuplicateHack);
		FDataValidationContext Context;
		TestEqual(TEXT("A sequence cannot require a second hack"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}
	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		FRoomCombatPhaseDefinition Automatic;
		Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Automatic.Waves.AddDefaulted();
		Definition->CombatPhases.Add(Automatic);
		FDataValidationContext Context;
		TestEqual(TEXT("An automatic phase must start with a spawned Wave"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		const URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		FDataValidationContext Context;
		TestEqual(TEXT("An initial detection definition is valid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestEqual(TEXT("A valid definition reports no errors"), Context.GetNumErrors(), uint32(0));
	}

	{
		const URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		FDataValidationContext Context;
		TestEqual(TEXT("A hack trigger definition is valid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestEqual(TEXT("A valid hack definition reports no errors"), Context.GetNumErrors(), uint32(0));
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->CombatPhases.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A definition without a combat phase is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->CombatPhases[0].Waves.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A combat phase without a Wave is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->CombatPhases[0].Waves[0].SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Definition->CombatPhases[0].Waves[0].Enemies.Add(MakeValidEnemyEntry());
		FDataValidationContext Context;
		TestEqual(TEXT("An initial detection phase must start with a preplaced Wave"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->CombatPhases[0].Waves[0].SpawnMode = ERoomCombatWaveSpawnMode::Preplaced;
		FDataValidationContext Context;
		TestEqual(TEXT("A hack trigger phase must start from spawn objects"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->CombatPhases[0].Waves[1].SpawnMode = ERoomCombatWaveSpawnMode::Preplaced;
		FDataValidationContext Context;
		TestEqual(TEXT("A Wave after the first must spawn from objects"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->CombatPhases[0].Waves[0].Enemies.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A spawned Wave requires an Enemy roster"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->CombatPhases[0].Waves[0].NextWaveRemainingRatio = -0.1f;
		FDataValidationContext Context;
		TestEqual(TEXT("A negative next Wave ratio is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->CombatPhases[0].Waves[0].NextWaveRemainingRatio = 1.1f;
		FDataValidationContext Context;
		TestEqual(TEXT("A next Wave ratio above one is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->CombatPhases[0].Waves[0].Enemies[0].EnemyClass.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("An Enemy entry without a class is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->CombatPhases[0].Waves[0].Enemies[0].Count = 0;
		FDataValidationContext Context;
		TestEqual(TEXT("A non-positive Enemy count is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	return true;
}

#endif
