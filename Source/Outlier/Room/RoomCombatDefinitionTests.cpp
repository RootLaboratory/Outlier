#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Room/RoomCombatDefinition.h"

namespace
{
	URoomCombatDefinition* MakeValidRoomCombatDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();

		FRoomCombatEnemyEntry Enemy;
		Enemy.EnemyClass = AEnemyBase::StaticClass();
		Enemy.Count = 2;

		FRoomCombatWaveDefinition Wave;
		Wave.SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Wave.NextWaveRemainingRatio = 0.5f;
		Wave.Enemies.Add(Enemy);

		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.Add(Wave);

		FRoomCombatFloorDefinition Floor;
		Floor.FloorTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.B.1")));
		Floor.CombatPhases.Add(Phase);

		Definition->Floors.Add(Floor);
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
		const URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		FDataValidationContext Context;
		TestEqual(TEXT("A complete combat definition is valid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestEqual(TEXT("A valid definition reports no errors"), Context.GetNumErrors(), uint32(0));
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A definition without a floor is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].FloorTag = FGameplayTag();
		FDataValidationContext Context;
		TestEqual(TEXT("An invalid FloorTag is rejected"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		const FRoomCombatFloorDefinition DuplicateFloor = Definition->Floors[0];
		Definition->Floors.Add(DuplicateFloor);
		FDataValidationContext Context;
		TestEqual(TEXT("Duplicate FloorTags are invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].CombatPhases.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A floor without a combat phase is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].CombatPhases[0].Waves.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A combat phase without a Wave is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].CombatPhases[0].Waves[0].NextWaveRemainingRatio = -0.1f;
		FDataValidationContext Context;
		TestEqual(TEXT("A negative next Wave ratio is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].CombatPhases[0].Waves[0].NextWaveRemainingRatio = 1.1f;
		FDataValidationContext Context;
		TestEqual(TEXT("A next Wave ratio above one is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].CombatPhases[0].Waves[0].Enemies[0].EnemyClass.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("An Enemy entry without a class is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidRoomCombatDefinition();
		Definition->Floors[0].CombatPhases[0].Waves[0].Enemies[0].Count = 0;
		FDataValidationContext Context;
		TestEqual(TEXT("A non-positive Enemy count is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	return true;
}

#endif
