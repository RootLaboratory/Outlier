#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyAIController.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(TurretRoom, "Room.TurretAllyRegression01.1.1");

struct FTurretRoomWorld
{
	UWorld* World = nullptr;
	UGameInstance* GameInstance = nullptr;
	URoomCombatSubsystem* Rooms = nullptr;

	FTurretRoomWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("TurretRoomRegression")));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GameInstance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(GameInstance);
		GameInstance->Init();
		World->SetGameMode(FURL());
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
		Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		URoomCombatDefinition* Definition = NewObject<URoomCombatDefinition>(World);
		FRoomCombatRoomDefinition& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = TurretRoom;
		FRoomCombatPhaseDefinition& Phase = RoomDefinition.CombatPhases.AddDefaulted_GetRef();
		Phase.StartPolicy = ERoomCombatPhaseStartPolicy::InitialDetection;
		Phase.Waves.AddDefaulted();
		Rooms->SetCombatDefinitionForTesting(Definition);
		ARoomVolume* Volume = World->SpawnActor<ARoomVolume>();
		*FindFProperty<FStructProperty>(ARoomVolume::StaticClass(), TEXT("RoomTag"))
			->ContainerPtrToValuePtr<FGameplayTag>(Volume) = TurretRoom;
		Rooms->RegisterRoom(Volume, TurretRoom);
	}

	~FTurretRoomWorld()
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(false);
		GameInstance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	AAutoTurret* AddTurret(UClass* Class, FVector Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AAutoTurret* Turret = World->SpawnActor<AAutoTurret>(Class, Location, FRotator::ZeroRotator, Spawn);
		if (!Turret) return nullptr;
		Turret->GetOutlierAbilitySystemComponent()->InitializeForPawn(Turret);
		Turret->GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.f);
		// BeginPlay 없는 fixture의 전개 완료 조건만 준비한다. 해킹/Room 전환은 실제 API를 사용한다.
		FEnumProperty* Lifecycle = FindFProperty<FEnumProperty>(AAutoTurret::StaticClass(), TEXT("TurretLifecycleState"));
		Lifecycle->GetUnderlyingProperty()->SetIntPropertyValue(Lifecycle->ContainerPtrToValuePtr<void>(Turret),
			static_cast<int64>(EAutoTurretLifecycleState::Active));
		Turret->GetRoomTagComp()->AssignDefaultRoomTag(TurretRoom);
		Rooms->RegisterPreplacedEnemy(Turret);
		return Turret;
	}

	void Hack(AAutoTurret* Turret)
	{
		// 최종 성공 효과를 그대로 실행해 팀 변경과 실제 Room 아군 제외 등록을 함께 검사한다.
		FHackResultContext Result;
		Result.TargetActor = Turret;
		Result.Result = EHackResult::Success;
		CastChecked<IHackableInterface>(Turret)->HandleHackEffect(HackGameplayTags::Effect::ChangeTeam(), Result);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoTurretRoomAwarenessTest,
	"Outlier.Enemy.Turret.RoomCombat.AwarenessWithoutEnemyRoomStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutoTurretRoomAwarenessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTurretRoomWorld Fixture;
	AAutoTurret* Allied = Fixture.AddTurret(AAutoTurret::StaticClass(), FVector::ZeroVector);
	AAutoTurret* Enemy = Fixture.AddTurret(AAutoTurret::StaticClass(), FVector(0.f, 1000.f, 0.f));
	if (!TestNotNull(TEXT("Registered allied fixture"), Allied)
		|| !TestNotNull(TEXT("Registered enemy control"), Enemy)) return false;
	Fixture.Hack(Allied);
	if (!TestTrue(TEXT("Real hack effect commits Player team"), Allied->IsHackedToPlayerTeam())) return false;
	const FVector Contact(1200.f, 0.f, 0.f);
	Allied->EnterAlertFromPerception(Contact);
	Enemy->EnterAlertFromPerception(Contact);
	// Arena가 없어서 적의 방 시작은 거절되는 동일 조건에서도 아군 자신의 전투는 허용한다.
	TestFalse(TEXT("Enemy still requires its room combat startup"), Enemy->CommitAlertToCombat());
	TestTrue(TEXT("Allied awareness does not request enemy room startup"), Allied->CommitAlertToCombat());
	TestTrue(TEXT("Allied turret reaches Combat"), Allied->GetCombatState() == EEnemyCombatState::Combat);
	TestTrue(TEXT("Enemy remains Alert"), Enemy->GetCombatState() == EEnemyCombatState::Alert);
	TestTrue(TEXT("Allied combat does not start the enemy encounter"), Fixture.Rooms->GetRoomState(TurretRoom) == ERoomCombatState::Dormant);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FAutoTurretRoomRifleAttackTest,
	"Outlier.Enemy.Turret.RoomCombat.RifleAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FAutoTurretRoomRifleAttackTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : { TEXT("StartHeld"), TEXT("Preparing") })
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FAutoTurretRoomRifleAttackTest::RunTest(const FString& Parameters)
{
	FTurretRoomWorld Fixture;
	UClass* TurretClass = LoadClass<AAutoTurret>(nullptr,
		TEXT("/Game/Blueprints/Enemy/Turret/BP_RifleTurret.BP_RifleTurret_C"));
	UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/Turret/Weapon/BP_TurretRifle.BP_TurretRifle_C"));
	if (!TestNotNull(TEXT("Actual rifle turret Blueprint"), TurretClass)
		|| !TestNotNull(TEXT("Actual turret rifle Blueprint"), WeaponClass)) return false;
	AAutoTurret* Allied = Fixture.AddTurret(TurretClass, FVector::ZeroVector);
	AAutoTurret* Enemy = Fixture.AddTurret(TurretClass, FVector(0.f, 1000.f, 0.f));
	if (!Allied || !Enemy) return false;
	Fixture.Hack(Allied);
	if (!TestTrue(TEXT("Registered rifle receives real hack effect"), Allied->IsHackedToPlayerTeam())) return false;
	AActor* Anchor = Fixture.World->SpawnActor<AActor>();
	FRoomCombatPreparationContext Preparation;
	if (Parameters == TEXT("Preparing"))
	{
		if (!TestTrue(TEXT("Enemy encounter enters actual preparation"),
			Fixture.Rooms->BeginInitialDetectionPreparation(TurretRoom, Anchor, Preparation))) return false;
	}
	else if (!TestTrue(TEXT("Actual room start hold acquired"),
		Fixture.Rooms->AcquireRoomStartHold(TurretRoom, FGuid::NewGuid(), Anchor))) return false;
	TestTrue(TEXT("Enemy control remains blocked by room preparation"), Fixture.Rooms->IsEnemyAttackBlocked(Enemy));
	TestFalse(TEXT("Hacked registered rifle ignores enemy room preparation"), Fixture.Rooms->IsEnemyAttackBlocked(Allied));

	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	Controller->Possess(Allied);
	ARangedWeaponBase* Weapon = Fixture.World->SpawnActor<ARangedWeaponBase>(WeaponClass);
	if (!TestNotNull(TEXT("Real rifle weapon"), Weapon)) return false;
	FindFProperty<FObjectPropertyBase>(AEnemyBase::StaticClass(), TEXT("CurrentWeapon"))
		->SetObjectPropertyValue_InContainer(Allied, Weapon);
	Weapon->OnEquipped(Allied);
	// 무한 탄약 설정과 무관하게 실제 첫 발 소비를 관측한다. 무기/발사 경로는 교체하지 않는다.
	FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"))
		->SetPropertyValue_InContainer(Weapon, false);
	const int32 AmmoBefore = Weapon->GetCurrentAmmo();
	if (!TestTrue(TEXT("Actual rifle starts with ammunition"), AmmoBefore > 0)) return false;
	Allied->EnterCombatFromRoom(FVector(1200.f, 0.f, 0.f), false);
	TestTrue(TEXT("Real rifle can begin before room gate"), Weapon->CanAttack());
	TestTrue(TEXT("Hacked rifle starts real attack while enemy room is blocked"),
		Allied->StartAttackLocation(Allied->GetPawnViewLocation() + Allied->GetViewRotation().Vector() * 1200.f));
	TestTrue(TEXT("Accepted allied attack actually consumes rifle ammunition"), Weapon->GetCurrentAmmo() < AmmoBefore);
	Weapon->CancelEnemyAttackCycle();
	TestTrue(TEXT("Allied firing preserves the enemy room block"), Fixture.Rooms->IsEnemyAttackBlocked(Enemy));
	return true;
}

#endif
