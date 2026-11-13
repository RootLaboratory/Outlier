#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyCombatTestHelpers.h"
#include "Enemy/EnemyStateTreeAttackTasks.h"
#include "Enemy/EnemyStateTreeSelfDestructTasks.h"
#include "Enemy/EnemyStateTreeTacticalTasks.h"
#include "Enemy/EnemyStateTreeTurretTasks.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Weapon/LaserWeapon.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
struct FCombatAssetWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;

	FCombatAssetWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("CombatAssetIntegration")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->InitializeActorsForPlay(FURL());
	}

	~FCombatAssetWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};

const FStateTreeReference* ReadTreeReference(FAutomationTestBase& Test,
	const AEnemyBase& Enemy, const FName PropertyName)
{
	const FStructProperty* Property = FindFProperty<FStructProperty>(Enemy.GetClass(), PropertyName);
	if (!Test.TestNotNull(PropertyName.ToString() + TEXT(" reflected property"), Property))
	{
		return nullptr;
	}
	return Property->ContainerPtrToValuePtr<FStateTreeReference>(&Enemy);
}

void CheckRuntimeRow(FAutomationTestBase& Test, const AEnemyBase& Enemy, const FEnemyStat& Row)
{
	// 클래스별 Type과 배치 인스턴스의 감지 거리만 별도 정책이다.
	// 그 외 필드는 실제 DT 복사 결과를 비교해 새 필드의 기본값만 검사하는 오류를 피한다.
	for (TFieldIterator<FProperty> It(FEnemyStat::StaticStruct()); It; ++It)
	{
		const FName Name = It->GetFName();
		if (Name == TEXT("Type"))
		{
			continue;
		}
		if (Name == TEXT("MoveSpeed") && Enemy.IsA<AAutoTurret>())
		{
			// 고정 터렛은 DT 재적용 뒤에도 클래스 정책으로 이동 속도를 0으로 고정한다.
			Test.TestEqual(TEXT("Fixed turret preserves its stationary class policy"), Enemy.GetRuntimeStat().MoveSpeed, 0.0f);
			continue;
		}
		const bool bDistance = Name == TEXT("SightRadius") || Name == TEXT("LoseSightRadius")
			|| Name == TEXT("HearingRange") || Name == TEXT("BattleHearingRange");
		const FFloatProperty* Override = bDistance
			? FindFProperty<FFloatProperty>(Enemy.GetClass(), FName(Name.ToString() + TEXT("Override"))) : nullptr;
		const float OverrideValue = Override ? Override->GetPropertyValue_InContainer(&Enemy) : -1.0f;
		if (OverrideValue >= 0.0f)
		{
			const FFloatProperty* Distance = CastField<FFloatProperty>(*It);
			if (Test.TestNotNull(Name.ToString() + TEXT(" distance property"), Distance))
			{
				Test.TestEqual(Name.ToString() + TEXT(" uses the actual instance override"),
					Distance->GetPropertyValue_InContainer(&Enemy.GetRuntimeStat()), OverrideValue);
			}
		}
		else
		{
			Test.TestTrue(Name.ToString() + TEXT(" preserves the configured DT row"),
				It->Identical_InContainer(&Enemy.GetRuntimeStat(), &Row));
		}
	}
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatEffectiveAssetGraphTest,
	"Outlier.Enemy.Combat.AssetIntegration.EffectiveBlueprintGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatEffectiveAssetGraphTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : { TEXT("Gun"), TEXT("Gun_Patrol"), TEXT("Explosion"), TEXT("Explosion_Patrol") })
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FEnemyCombatEffectiveAssetGraphTest::RunTest(const FString& Parameters)
{
	const FString ClassPath = FString::Printf(
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_%s.BP_VECDrone_%s_C"), *Parameters, *Parameters);
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, *ClassPath);
	if (!TestNotNull(TEXT("Actual configured Blueprint class loads"), EnemyClass))
	{
		return false;
	}
	const bool bOriginallyDirty = EnemyClass->GetOutermost()->IsDirty();
	FCombatAssetWorld Fixture;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = Fixture.World->SpawnActor<AEnemyBase>(EnemyClass, FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Actual Blueprint spawns in an initialized server world"), Enemy))
	{
		return false;
	}
	TestTrue(TEXT("DT application and linked overrides belong to authority"), Enemy->HasAuthority());
	const auto* Component = Cast<UEnemyStateTreeComponent>(Enemy->GetStateTreeComponent());
	if (!TestNotNull(TEXT("Spawned BP uses the Enemy StateTree component"), Component))
	{
		return false;
	}

	const FStructProperty* RowProperty = FindFProperty<FStructProperty>(EnemyClass, TEXT("EnemyStatRow"));
	if (!TestNotNull(TEXT("Actual Blueprint stat row property"), RowProperty))
	{
		return false;
	}
	const auto& Handle = *RowProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Enemy);
	if (!TestNotNull(TEXT("Actual Blueprint stat DataTable is assigned"), Handle.DataTable.Get())
		|| !TestFalse(TEXT("Actual Blueprint stat row name is assigned"), Handle.RowName.IsNone()))
	{
		return false;
	}
	const FEnemyStat* Row = Handle.GetRow<FEnemyStat>(TEXT("EnemyCombatAssetIntegration"));
	if (!TestNotNull(TEXT("Configured stat row exists and has the EnemyStat type"), Row))
	{
		return false;
	}
	Enemy->InitializeFromEnemyStatRow();
	CheckRuntimeRow(*this, *Enemy, *Row);
	const bool bGun = Parameters.StartsWith(TEXT("Gun"));
	TestEqual(TEXT("Runtime type matches the actual Blueprint combat family"),
		Enemy->GetRuntimeStat().Type, bGun ? EEnemyType::Gun : EEnemyType::Melee);
	TestTrue(TEXT("Configured health and movement are usable"), Row->Health > 0.0f && Row->MoveSpeed > 0.0f);
	AddInfo(FString::Printf(TEXT("[CombatAssetIntegration] BP=%s Row=%s:%s Type=%d"),
		*ClassPath, *Handle.DataTable->GetPathName(), *Handle.RowName.ToString(), int32(Enemy->GetRuntimeStat().Type)));

	const UStateTree* Common = Component->GetConfiguredStateTreeReference().GetStateTree();
	if (!TestNotNull(TEXT("Spawned BP has its configured common tree"), Common))
	{
		return false;
	}
	TestEqual(TEXT("Spawned BP uses the shared lifecycle tree"), Common->GetFName(), FName(TEXT("ST_VECDrone_Common")));
	TArray<const UStateTree*> Trees;
	EnemyCombatTesting::CollectTrees(Common, *Component, Trees);
	for (const FName PropertyName : { FName(TEXT("BattleStateTreeReference")), FName(TEXT("PossessedAttackStateTreeReference")) })
	{
		const FStateTreeReference* Configured = ReadTreeReference(*this, *Enemy, PropertyName);
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(PropertyName == TEXT("BattleStateTreeReference")
			? FName(TEXT("Enemy.StateTree.Battle")) : FName(TEXT("Enemy.StateTree.PossessedAttack")));
		const FStateTreeReference* Registered = Component->FindLinkedStateTreeOverride(Tag);
		if (!TestNotNull(PropertyName.ToString() + TEXT(" configured reference"), Configured)
			|| !TestNotNull(PropertyName.ToString() + TEXT(" PostInitialize registered override"), Registered))
		{
			continue;
		}
		const UStateTree* Tree = Configured->GetStateTree();
		TestNotNull(PropertyName.ToString() + TEXT(" configured tree"), Tree);
		TestTrue(PropertyName.ToString() + TEXT(" registered override preserves the BP reference"), Registered->GetStateTree() == Tree);
		TestTrue(PropertyName.ToString() + TEXT(" is reachable from the effective common graph"), Tree && Trees.Contains(Tree));
		if (PropertyName == TEXT("BattleStateTreeReference") && Tree)
		{
			TestEqual(TEXT("Effective battle override matches the Blueprint family"), Tree->GetFName(),
				FName(bGun ? TEXT("ST_VECDroneBattle_Gun") : TEXT("ST_VECDroneBattle_Melee")));
		}
	}

	int32 QueryCount = 0;
	int32 AttackTargetCount = 0;
	int32 SelfDestructAssessmentCount = 0;
	for (const UStateTree* Tree : Trees)
	{
		AddInfo(FString::Printf(TEXT("[CombatAssetIntegration] EffectiveTree=%s"), *Tree->GetPathName()));
		TestTrue(Tree->GetPathName() + TEXT(" compiled data is ready"), Tree->IsReadyToRun());
		// CollectTrees는 BP override와 병렬 공격 Tree를 모두 해석한다.
		// 독립적으로 로드한 정본이 아니라 실제 BP에서 도달하는 노드의 EQS만 검사한다.
		for (int32 Index = 0; Index < Tree->GetNodes().Num(); ++Index)
		{
			const FConstStructView Node = Tree->GetNode(Index);
			const FStateTreeTaskBase* Task = Node.GetPtr<const FStateTreeTaskBase>();
			if (!Task || !Task->bTaskEnabled)
			{
				continue;
			}
			AttackTargetCount += Node.GetPtr<const FEnemyAttackTargetTask>() != nullptr ? 1 : 0;
			SelfDestructAssessmentCount += Node.GetPtr<const FEnemySelfDestructAssessmentTask>() != nullptr ? 1 : 0;
			const FEnemyRunEnvQueryTask* QueryTask = Node.GetPtr<const FEnemyRunEnvQueryTask>();
			if (!QueryTask)
			{
				continue;
			}
			++QueryCount;
			const auto* Data = EnemyCombatTesting::GetDefaultData(*Tree, QueryTask);
			if (!TestNotNull(Tree->GetPathName() + TEXT(" query instance data"), Data)
				|| !TestNotNull(Tree->GetPathName() + TEXT(" reachable QueryTemplate"), Data->QueryTemplate.Get()))
			{
				continue;
			}
			const UEnvQuery* Query = Data->QueryTemplate.Get();
			AddInfo(FString::Printf(TEXT("[CombatAssetIntegration] Tree=%s Query=%s"), *Tree->GetPathName(), *Query->GetPathName()));
			TestTrue(Query->GetPathName() + TEXT(" has runtime options"), !Query->GetOptions().IsEmpty());
			for (const UEnvQueryOption* Option : Query->GetOptions())
			{
				if (TestNotNull(Query->GetPathName() + TEXT(" runtime option"), Option))
				{
					TestNotNull(Query->GetPathName() + TEXT(" runtime generator"), Option->Generator.Get());
				}
			}
		}
	}
	TestTrue(TEXT("Actual effective graph reaches movement/search EQS"), QueryCount > 0);
	TestTrue(TEXT("Actual effective graph reaches its family attack implementation"),
		bGun ? AttackTargetCount > 0 : SelfDestructAssessmentCount > 0);

	// 배치 인스턴스의 네 가지 감지 override를 DT 재적용과 함께 확인한다.
	// 테스트 인스턴스만 변경하며 BP 기본값이나 저장 에셋에는 기록하지 않는다.
	int32 OverrideIndex = 0;
	for (const TCHAR* Name : { TEXT("SightRadiusOverride"), TEXT("LoseSightRadiusOverride"),
		TEXT("HearingRangeOverride"), TEXT("BattleHearingRangeOverride") })
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(EnemyClass, Name);
		if (TestNotNull(FString(Name) + TEXT(" instance property"), Property))
		{
			Property->SetPropertyValue_InContainer(Enemy, 600.0f + 100.0f * OverrideIndex);
		}
		++OverrideIndex;
	}
	Enemy->InitializeFromEnemyStatRow();
	CheckRuntimeRow(*this, *Enemy, *Row);
	TestEqual(TEXT("Asset audit does not dirty the source Blueprint package"), EnemyClass->GetOutermost()->IsDirty(), bOriginallyDirty);
	AddInfo(TEXT("[CombatAssetIntegration] Scope=SpawnedBlueprintAndEffectiveReferences; PlacedLevelOverridesAndNetworkCombat=SeparateValidation"));
	return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEnemyCombatMapPlacementAuditTest,
	"Outlier.Enemy.Combat.AssetIntegration.MapPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEnemyCombatMapPlacementAuditTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : { TEXT("L_Stage01_Gameplay"), TEXT("L_Stage02_Gameplay"),
		TEXT("L_Stage03_Gameplay"), TEXT("L_Stage04_Gameplay") })
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FEnemyCombatMapPlacementAuditTest::RunTest(const FString& Parameters)
{
	const FString Path = FString::Printf(TEXT("/Game/Maps/Arena/%s.%s"), *Parameters, *Parameters);
	UWorld* Map = LoadObject<UWorld>(nullptr, *Path);
	if (!TestNotNull(TEXT("Actual saved gameplay map loads without starting play"), Map)
		|| !TestNotNull(TEXT("Actual map has its serialized persistent level"), Map->PersistentLevel.Get()))
	{
		return false;
	}
	// 외부 Actor를 로드하지 않은 World Partition의 빈 목록을 배치 감사 합격으로 취급하지 않는다.
	if (!TestTrue(TEXT("Map audit requires a complete non-World-Partition actor list"), Map->GetWorldPartition() == nullptr))
	{
		AddError(TEXT("External World Partition actors were not audited; map placement validation is incomplete"));
		return false;
	}
	const bool bOriginallyDirty = Map->GetOutermost()->IsDirty();
	int32 EnemyCount = 0;
	TSet<const UStateTree*> AuditedTrees;
	for (const AActor* Actor : Map->PersistentLevel->Actors)
	{
		const AEnemyBase* Enemy = Cast<AEnemyBase>(Actor);
		if (!Enemy)
		{
			continue;
		}
		++EnemyCount;
		const FString Label = Enemy->GetPathName();
		const FStructProperty* RowProperty = FindFProperty<FStructProperty>(Enemy->GetClass(), TEXT("EnemyStatRow"));
		if (!TestNotNull(Label + TEXT(" stat row property"), RowProperty))
		{
			continue;
		}
		const auto& Handle = *RowProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Enemy);
		if (!TestNotNull(Label + TEXT(" configured DataTable"), Handle.DataTable.Get())
			|| !TestFalse(Label + TEXT(" configured row name"), Handle.RowName.IsNone()))
		{
			continue;
		}
		const FEnemyStat* Row = Handle.GetRow<FEnemyStat>(TEXT("EnemyCombatMapPlacement"));
		if (!TestNotNull(Label + TEXT(" configured EnemyStat row exists"), Row))
		{
			continue;
		}
		TestTrue(Label + TEXT(" configured health is usable"), FMath::IsFinite(Row->Health) && Row->Health > 0.0f);
		AddInfo(FString::Printf(TEXT("[CombatMapPlacement] Actor=%s Class=%s Row=%s:%s"),
			*Label, *Enemy->GetClass()->GetPathName(), *Handle.DataTable->GetPathName(), *Handle.RowName.ToString()));
		for (const TCHAR* Name : { TEXT("SightRadiusOverride"), TEXT("LoseSightRadiusOverride"),
			TEXT("HearingRangeOverride"), TEXT("BattleHearingRangeOverride") })
		{
			const FFloatProperty* Override = FindFProperty<FFloatProperty>(Enemy->GetClass(), Name);
			if (!TestNotNull(Label + TEXT(" / ") + Name, Override))
			{
				continue;
			}
			const float Value = Override->GetPropertyValue_InContainer(Enemy);
			TestTrue(Label + TEXT(" / ") + Name + TEXT(" is finite and uses -1 or a nonnegative distance"),
				FMath::IsFinite(Value) && (Value == -1.0f || Value >= 0.0f));
			AddInfo(FString::Printf(TEXT("[CombatMapPlacement] Actor=%s Override=%s Value=%.3f"), *Label, Name, Value));
		}
		const auto* Component = Cast<UEnemyStateTreeComponent>(Enemy->GetStateTreeComponent());
		if (!TestNotNull(Label + TEXT(" serialized Enemy StateTree component"), Component))
		{
			continue;
		}
		const UStateTree* Common = Component->GetConfiguredStateTreeReference().GetStateTree();
		if (!TestNotNull(Label + TEXT(" serialized common tree"), Common))
		{
			continue;
		}
		TArray<const UStateTree*> Trees;
		EnemyCombatTesting::CollectTrees(Common, *Component, Trees);
		for (const FName PropertyName : { FName(TEXT("BattleStateTreeReference")), FName(TEXT("PossessedAttackStateTreeReference")) })
		{
			const FStateTreeReference* Reference = ReadTreeReference(*this, *Enemy, PropertyName);
			const UStateTree* Tree = Reference ? Reference->GetStateTree() : nullptr;
			if (PropertyName == TEXT("BattleStateTreeReference"))
			{
				TestNotNull(Label + TEXT(" configured battle tree"), Tree);
			}
			// 저장된 Actor에서는 PostInitialize override가 아직 등록되지 않는다.
			// 값을 주입하지 않고 초기화 시 등록될 참조 자체와 하위 그래프만 읽는다.
			if (Tree)
			{
				EnemyCombatTesting::CollectTrees(Tree, *Component, Trees);
				AddInfo(FString::Printf(TEXT("[CombatMapPlacement] Actor=%s Reference=%s Tree=%s"),
					*Label, *PropertyName.ToString(), *Tree->GetPathName()));
			}
		}
		for (const UStateTree* Tree : Trees)
		{
			if (AuditedTrees.Contains(Tree))
			{
				continue;
			}
			AuditedTrees.Add(Tree);
			TestTrue(Tree->GetPathName() + TEXT(" saved compiled graph is ready"), Tree->IsReadyToRun());
			for (int32 Index = 0; Index < Tree->GetNodes().Num(); ++Index)
			{
				const auto* Task = Tree->GetNode(Index).GetPtr<const FEnemyRunEnvQueryTask>();
				if (!Task || !Task->bTaskEnabled)
				{
					continue;
				}
				const auto* Data = EnemyCombatTesting::GetDefaultData(*Tree, Task);
				if (TestNotNull(Tree->GetPathName() + TEXT(" query data"), Data))
				{
					TestNotNull(Tree->GetPathName() + TEXT(" saved query reference"), Data->QueryTemplate.Get());
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("[CombatMapPlacement] Map=%s PlacedEnemyCount=%d Trees=%d ActorAudit=%s RuntimeCombat=NotExecuted"),
		*Path, EnemyCount, AuditedTrees.Num(), EnemyCount > 0 ? TEXT("Inspected") : TEXT("NotApplicable")));
	TestEqual(TEXT("Read-only map audit does not dirty the saved map"), Map->GetOutermost()->IsDirty(), bOriginallyDirty);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyF6TurretEffectiveAssetGraphTest,
	"Outlier.Enemy.Combat.F6.AssetIntegration.RifleTurretEffectiveGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyF6TurretEffectiveAssetGraphTest::RunTest(const FString& Parameters)
{
	UClass* TurretClass = LoadClass<AAutoTurret>(nullptr,
		TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C"));
	if (!TestNotNull(TEXT("Saved bullet turret BP loads"), TurretClass))
	{
		return false;
	}
	const bool bOriginallyDirty = TurretClass->GetOutermost()->IsDirty();
	FCombatAssetWorld Fixture;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAutoTurret* Turret = Fixture.World->SpawnActor<AAutoTurret>(TurretClass, FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Saved turret initializes its actual BP components"), Turret))
	{
		return false;
	}
	// Wave 활성화나 전개를 흉내 내지 않는다. 이 검사는 PostInitialize에서 확정된 저장 참조만 감사한다.
	const auto* Component = Cast<UEnemyStateTreeComponent>(Turret->GetStateTreeComponent());
	const FStructProperty* RowProperty = FindFProperty<FStructProperty>(TurretClass, TEXT("EnemyStatRow"));
	const FClassProperty* WeaponProperty = FindFProperty<FClassProperty>(TurretClass, TEXT("DefaultWeaponClass"));
	if (!TestNotNull(TEXT("Turret uses the production StateTree component"), Component)
		|| !TestNotNull(TEXT("Turret has the saved row property"), RowProperty)
		|| !TestNotNull(TEXT("Turret has the saved weapon class property"), WeaponProperty))
	{
		return false;
	}
	const auto& Handle = *RowProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Turret);
	const FEnemyStat* Row = Handle.GetRow<FEnemyStat>(TEXT("F6TurretAssetIntegration"));
	if (!TestNotNull(TEXT("Saved turret DT row resolves"), Row))
	{
		return false;
	}
	Turret->InitializeFromEnemyStatRow();
	CheckRuntimeRow(*this, *Turret, *Row);
	TestEqual(TEXT("Saved rifle turret retains its class family"), Turret->GetRuntimeStat().Type, EEnemyType::Turret);
	UClass* WeaponClass = Cast<UClass>(WeaponProperty->GetObjectPropertyValue_InContainer(Turret));
	const ARangedWeaponBase* Weapon = WeaponClass ? Cast<ARangedWeaponBase>(WeaponClass->GetDefaultObject()) : nullptr;
	if (TestNotNull(TEXT("Saved rifle turret has a ranged weapon"), Weapon))
	{
		TestFalse(TEXT("Bullet turret does not silently select the committed laser policy"), Weapon->IsA<ALaserWeapon>());
		TestTrue(TEXT("Saved bullet weapon has usable range"), Weapon->GetEffectiveRange() > 0.0f);
	}

	const UStateTree* Root = Component->GetConfiguredStateTreeReference().GetStateTree();
	if (!TestNotNull(TEXT("Saved turret root StateTree resolves"), Root))
	{
		return false;
	}
	TArray<const UStateTree*> Trees;
	EnemyCombatTesting::CollectTrees(Root, *Component, Trees);
	int32 BurstTasks = 0;
	int32 MobileQueries = 0;
	for (const UStateTree* Tree : Trees)
	{
		TestTrue(Tree->GetPathName() + TEXT(" effective compiled graph is ready"), Tree->IsReadyToRun());
		for (int32 Index = 0; Index < Tree->GetNodes().Num(); ++Index)
		{
			const FConstStructView Node = Tree->GetNode(Index);
			const FStateTreeTaskBase* Task = Node.GetPtr<const FStateTreeTaskBase>();
			if (!Task || !Task->bTaskEnabled)
			{
				continue;
			}
			BurstTasks += Node.GetPtr<const FEnemyTurretBurstFireTask>() != nullptr ? 1 : 0;
			MobileQueries += Node.GetPtr<const FEnemyRunEnvQueryTask>() != nullptr ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("[F6TurretAsset] EffectiveTree=%s"), *Tree->GetPathName()));
	}
	TestTrue(TEXT("Effective saved graph reaches ordinary turret burst policy"), BurstTasks > 0);
	TestEqual(TEXT("Fixed turret graph does not request mobile reposition/search EQS"), MobileQueries, 0);
	TestEqual(TEXT("Read-only turret audit preserves the saved BP dirty flag"), TurretClass->GetOutermost()->IsDirty(), bOriginallyDirty);
	AddInfo(FString::Printf(TEXT("[F6TurretAsset] BP=%s Row=%s:%s Weapon=%s Trees=%d RuntimeCombat=SeparateValidation"),
		*TurretClass->GetPathName(), *GetPathNameSafe(Handle.DataTable), *Handle.RowName.ToString(),
		*GetPathNameSafe(WeaponClass), Trees.Num()));
	return !HasAnyErrors();
}

#endif
