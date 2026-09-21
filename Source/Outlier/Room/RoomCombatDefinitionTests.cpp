#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/EnemyBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoomCombatSpawnPointTemplateValidationTest,
	"Outlier.Room.SpawnPoint.TemplateValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombatSpawnPointTemplateValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// BP 컴파일도 클래스 기본 객체를 검증한다. 미배치 템플릿에는 RoomTag가 없어도 된다.
	const UObject* Template = GetDefault<ARoomCombatSpawnPoint>();
	FDataValidationContext Context;
	TestEqual(TEXT("An unassigned SpawnPoint template is valid"),
		Template->IsDataValid(Context), EDataValidationResult::Valid);
	return true;
}

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

	FGameplayTag GetTestRoomTag()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.1")));
	}

	FRoomCombatRoomDefinition& AddTestRoom(URoomCombatDefinition& Definition)
	{
		FRoomCombatRoomDefinition& Room = Definition.RoomDefinitions.AddDefaulted_GetRef();
		Room.RoomTag = GetTestRoomTag();
		return Room;
	}

	URoomCombatDefinition* MakeValidInitialDetectionDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();

		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();
		Phase.Waves.Add(MakeSpawnWave());

		AddTestRoom(*Definition).CombatPhases.Add(Phase);
		return Definition;
	}

	URoomCombatDefinition* MakeValidHackTriggerDefinition()
	{
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>();

		FRoomCombatPhaseDefinition Phase;
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
		Phase.Waves.Add(MakeSpawnWave());

		AddTestRoom(*Definition).CombatPhases.Add(Phase);
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
		FRoomCombatRoomDefinition& Room = Definition->RoomDefinitions[0];
		FRoomCombatPhaseDefinition Automatic = Room.CombatPhases[0];
		Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Room.CombatPhases.Add(Automatic);
		Room.CombatPhases.Add(Automatic);
		FDataValidationContext Context;
		TestEqual(TEXT("A hack followed by automatic phases is valid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestTrue(TEXT("A valid sequence can start at runtime"), Room.CanStartTriggeredSequence(0));
		TestFalse(TEXT("An automatic phase cannot be externally triggered"), Room.CanStartTriggeredSequence(1));

		Room.CombatPhases[2].Waves[0].Enemies[0].Count = 0;
		TestFalse(TEXT("Invalid later rosters reject the sequence before locking exits"),
			Room.CanStartTriggeredSequence(0));
	}
	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		FDataValidationContext Context;
		TestEqual(TEXT("An automatic phase cannot be first"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}
	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		FRoomCombatPhaseDefinition Automatic;
		Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Automatic.Waves.Add(MakeSpawnWave());
		Definition->RoomDefinitions[0].CombatPhases.Add(Automatic);
		FDataValidationContext Context;
		TestEqual(TEXT("Automatic progression requires a preceding hack"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}
	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		const FRoomCombatPhaseDefinition DuplicateHack = Definition->RoomDefinitions[0].CombatPhases[0];
		Definition->RoomDefinitions[0].CombatPhases.Add(DuplicateHack);
		FDataValidationContext Context;
		TestEqual(TEXT("A sequence cannot require a second hack"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}
	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		FRoomCombatPhaseDefinition Automatic;
		Automatic.StartPolicy = ERoomCombatPhaseStartPolicy::Automatic;
		Automatic.Waves.AddDefaulted();
		Definition->RoomDefinitions[0].CombatPhases.Add(Automatic);
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
		Definition->RoomDefinitions[0].CombatPhases.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A definition without a combat phase is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A combat phase without a Wave is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].SpawnMode = ERoomCombatWaveSpawnMode::SpawnFromObjects;
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].Enemies.Add(MakeValidEnemyEntry());
		FDataValidationContext Context;
		TestEqual(TEXT("An initial detection phase must start with a preplaced Wave"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].SpawnMode = ERoomCombatWaveSpawnMode::Preplaced;
		FDataValidationContext Context;
		TestEqual(TEXT("A hack trigger phase must start from spawn objects"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[1].SpawnMode = ERoomCombatWaveSpawnMode::Preplaced;
		FDataValidationContext Context;
		TestEqual(TEXT("A Wave after the first must spawn from objects"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].Enemies.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("A spawned Wave requires an Enemy roster or turret hatches"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		FRoomCombatWaveDefinition& Wave = Definition->RoomDefinitions[0].CombatPhases[0].Waves[0];
		Wave.Enemies.Reset();
		Wave.ExpectedTurretHatchCount = 2;
		FDataValidationContext Context;
		TestEqual(TEXT("A spawned Wave may use only preplaced turret hatches"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestTrue(TEXT("A turret-only sequence passes runtime prevalidation"),
			Definition->RoomDefinitions[0].CanStartTriggeredSequence(0));
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].ExpectedTurretHatchCount = -1;
		FDataValidationContext Context;
		TestEqual(TEXT("A negative turret hatch count is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].ExpectedTurretHatchCount = 1;
		FDataValidationContext Context;
		TestEqual(TEXT("A preplaced Wave cannot declare turret hatch activations"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].NextWaveRemainingRatio = -0.1f;
		FDataValidationContext Context;
		TestEqual(TEXT("A negative next Wave ratio is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].NextWaveRemainingRatio = 1.1f;
		FDataValidationContext Context;
		TestEqual(TEXT("A next Wave ratio above one is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].Enemies[0].EnemyClass.Reset();
		FDataValidationContext Context;
		TestEqual(TEXT("An Enemy entry without a class is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidHackTriggerDefinition();
		Definition->RoomDefinitions[0].CombatPhases[0].Waves[0].Enemies[0].Count = 0;
		FDataValidationContext Context;
		TestEqual(TEXT("A non-positive Enemy count is invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		FRoomCombatRoomDefinition Duplicate = Definition->RoomDefinitions[0];
		Duplicate.CombatPhases[0].StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
		Duplicate.CombatPhases[0].Waves[0] = MakeSpawnWave();
		Definition->RoomDefinitions.Add(Duplicate);
		FDataValidationContext Context;
		TestEqual(TEXT("Duplicate RoomTags are invalid"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Invalid);
	}

	{
		URoomCombatDefinition* Definition = MakeValidInitialDetectionDefinition();
		FRoomCombatRoomDefinition& SecondRoom = Definition->RoomDefinitions.AddDefaulted_GetRef();
		SecondRoom.RoomTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01.2")));
		FRoomCombatPhaseDefinition& Phase = SecondRoom.CombatPhases.AddDefaulted_GetRef();
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::HackTrigger;
		Phase.Waves.Add(MakeSpawnWave());
		FDataValidationContext Context;
		TestEqual(TEXT("Different RoomTags can use different start policies"),
			ValidateDefinition(Definition, Context), EDataValidationResult::Valid);
		TestNotNull(TEXT("Exact RoomTag lookup finds the second room"),
			Definition->FindRoomDefinition(SecondRoom.RoomTag));
		TestNull(TEXT("A parent RoomTag does not select a child room"),
			Definition->FindRoomDefinition(FGameplayTag::RequestGameplayTag(FName(TEXT("Room.Level01")))));
	}

	return true;
}

#endif
