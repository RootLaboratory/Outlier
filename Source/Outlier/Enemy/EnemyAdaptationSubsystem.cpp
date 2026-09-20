#include "Enemy/EnemyAdaptationSubsystem.h"

#include "Enemy/EnemyAdaptationDefinition.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierArenaSettings.h"
#include "Subsystems/SubsystemCollection.h"

void UEnemyAdaptationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UOutlierArenaSubsystem* ArenaSubsystem =
		Collection.InitializeDependency<UOutlierArenaSubsystem>())
	{
		ArenaSubsystem->OnArenaShown.AddUObject(
			this,
			&UEnemyAdaptationSubsystem::HandleArenaShown);
		ArenaSubsystem->OnArenaGameplayReloadStarted.AddUObject(
			this,
			&UEnemyAdaptationSubsystem::HandleArenaGameplayReloadStarted);
		ArenaSubsystem->OnArenaGameplayGCReady.AddUObject(
			this,
			&UEnemyAdaptationSubsystem::HandleArenaGameplayGCReady);
		ArenaSubsystem->OnArenaGameplayReady.AddUObject(
			this,
			&UEnemyAdaptationSubsystem::HandleArenaGameplayReady);
		ArenaSubsystem->OnArenaReleased.AddUObject(
			this,
			&UEnemyAdaptationSubsystem::HandleArenaReleased);
	}
}

void UEnemyAdaptationSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UOutlierArenaSubsystem* ArenaSubsystem = World->GetSubsystem<UOutlierArenaSubsystem>())
		{
			ArenaSubsystem->OnArenaShown.RemoveAll(this);
			ArenaSubsystem->OnArenaGameplayReloadStarted.RemoveAll(this);
			ArenaSubsystem->OnArenaGameplayGCReady.RemoveAll(this);
			ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
			ArenaSubsystem->OnArenaReleased.RemoveAll(this);
		}
	}

	bAcceptingRegistrations = false;
	ResetActiveEnemies();
	ActiveDefinition = nullptr;
	Super::Deinitialize();
}

void UEnemyAdaptationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	LoadConfiguredDefinition();
	if (const UOutlierArenaSubsystem* ArenaSubsystem =
		InWorld.GetSubsystem<UOutlierArenaSubsystem>())
	{
		ActiveGameplayGeneration = ArenaSubsystem->GetGameplayGeneration();
		bAcceptingRegistrations =
			ArenaSubsystem->GetGameplayReloadPhase() == EOutlierGameplayReloadPhase::Ready;
	}
	else
	{
		bAcceptingRegistrations = true;
	}
}

bool UEnemyAdaptationSubsystem::RegisterEnemy(AEnemyBase* Enemy)
{
	if (!CanRegisterEnemy(Enemy))
	{
		return false;
	}

	FEnemyRegistration Registration;
	Registration.GameplayGeneration = Enemy->IsPoolManaged()
		? Enemy->GetPoolGameplayGeneration()
		: static_cast<int32>(ActiveGameplayGeneration);
	Registration.PoolLeaseSerial = Enemy->IsPoolManaged()
		? Enemy->GetPoolLeaseSerial()
		: 0;

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	if (const FEnemyRegistration* Existing = ActiveEnemies.Find(EnemyPtr);
		Existing && Existing->GameplayGeneration == Registration.GameplayGeneration
		&& Existing->PoolLeaseSerial == Registration.PoolLeaseSerial)
	{
		return true;
	}

	ActiveEnemies.Add(EnemyPtr, Registration);
	return true;
}

void UEnemyAdaptationSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	if (Enemy)
	{
		ActiveEnemies.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
	}
}

void UEnemyAdaptationSubsystem::ResetActiveEnemies()
{
	ActiveEnemies.Reset();
}

bool UEnemyAdaptationSubsystem::IsEnemyRegistered(AEnemyBase* Enemy) const
{
	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	return Enemy && ActiveEnemies.Contains(EnemyPtr);
}

int32 UEnemyAdaptationSubsystem::GetRegisteredEnemyCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AEnemyBase>, FEnemyRegistration>& Entry : ActiveEnemies)
	{
		if (Entry.Key.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

bool UEnemyAdaptationSubsystem::CanRegisterEnemy(const AEnemyBase* Enemy) const
{
	const UWorld* World = GetWorld();
	if (!bAcceptingRegistrations || !World || World->GetNetMode() == NM_Client
		|| !IsValid(Enemy) || !Enemy->HasAuthority())
	{
		return false;
	}

	if (!Enemy->IsPoolManaged())
	{
		return true;
	}

	if (Enemy->GetEnemyPoolState() != EEnemyPoolState::CombatActive
		|| Enemy->GetPoolLeaseSerial() == 0)
	{
		return false;
	}

	// Generation 0은 초기 Arena와 독립 자동화 월드가 공유한다. 실제 리로드 세대부터는
	// 이전 Data Layer에서 늦게 도착한 Pool 활성화가 새 전투 목록에 들어오지 못하게 막는다.
	return ActiveGameplayGeneration == 0
		|| Enemy->GetPoolGameplayGeneration() == static_cast<int32>(ActiveGameplayGeneration);
}

void UEnemyAdaptationSubsystem::LoadConfiguredDefinition()
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	ActiveDefinition = Settings
		? Settings->EnemyAdaptationDefinition.LoadSynchronous()
		: nullptr;
}

void UEnemyAdaptationSubsystem::HandleArenaShown()
{
	if (UWorld* World = GetWorld(); World && World->GetNetMode() != NM_Client)
	{
		LoadConfiguredDefinition();
		if (const UOutlierArenaSubsystem* ArenaSubsystem =
			World->GetSubsystem<UOutlierArenaSubsystem>())
		{
			ActiveGameplayGeneration = ArenaSubsystem->GetGameplayGeneration();
		}
		bAcceptingRegistrations = true;
	}
}

void UEnemyAdaptationSubsystem::HandleArenaGameplayReloadStarted(uint32 GameplayGeneration)
{
	// 새 Data Layer의 BeginPlay가 오기 전까지 문을 닫아 이전 Generation의 지연 이벤트를 버린다.
	bAcceptingRegistrations = false;
	ActiveGameplayGeneration = GameplayGeneration;
	ResetActiveEnemies();
}

void UEnemyAdaptationSubsystem::HandleArenaGameplayGCReady(uint32 GameplayGeneration)
{
	// 이전 세대 Actor EndPlay와 GC가 검증된 뒤 새 Data Layer Actor의 BeginPlay 등록을 허용한다.
	ActiveGameplayGeneration = GameplayGeneration;
	bAcceptingRegistrations = true;
}

void UEnemyAdaptationSubsystem::HandleArenaGameplayReady(uint32 GameplayGeneration)
{
	ActiveGameplayGeneration = GameplayGeneration;
	LoadConfiguredDefinition();
	bAcceptingRegistrations = true;
}

void UEnemyAdaptationSubsystem::HandleArenaReleased()
{
	bAcceptingRegistrations = false;
	ActiveGameplayGeneration = 0;
	ResetActiveEnemies();
	ActiveDefinition = nullptr;
}
