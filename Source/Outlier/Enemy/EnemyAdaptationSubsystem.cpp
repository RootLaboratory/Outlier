#include "Enemy/EnemyAdaptationSubsystem.h"

#include "Enemy/EnemyAdaptationDefinition.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
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
	ResetAdaptationState();
	ActiveDefinition = nullptr;
	OnAdaptationUpdated.Clear();
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

bool UEnemyAdaptationSubsystem::SetGunAdaptationStack(int32 NewStack)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !ActiveDefinition)
	{
		return false;
	}

	FEnemyAdaptationUpdateResult Result;
	Result.PreviousStack = CurrentGunAdaptationStack;
	Result.PreviousState = CurrentAdaptationState;
	CurrentGunAdaptationStack = ActiveDefinition->ClampStack(NewStack);
	CurrentAdaptationState = ActiveDefinition->ResolveState(CurrentGunAdaptationStack);
	Result.CurrentStack = CurrentGunAdaptationStack;
	Result.CurrentState = CurrentAdaptationState;
	OnAdaptationUpdated.Broadcast(Result);
	return true;
}

bool UEnemyAdaptationSubsystem::ReportEnemyDefeat(
	AEnemyBase* Enemy,
	int32 GameplayGeneration,
	int32 PoolLeaseSerial,
	EEnemyFinalKillCategory KillCategory,
	FEnemyAdaptationUpdateResult& OutResult)
{
	OutResult = FEnemyAdaptationUpdateResult();
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !ActiveDefinition || !IsValid(Enemy)
		|| !Enemy->HasAuthority()
		|| !Enemy->HasEnemyTrait(OutlierGameplayTags::Enemy::Adaptation::Target()))
	{
		return false;
	}

	FEnemyRegistration* Registration = ActiveEnemies.Find(TWeakObjectPtr<AEnemyBase>(Enemy));
	if (!Registration || Registration->bDefeatReported
		|| !IsCurrentRegistration(
			Enemy,
			*Registration,
			GameplayGeneration,
			PoolLeaseSerial))
	{
		return false;
	}

	// Ignore도 이번 수명의 최종 판정이다. Stack은 바꾸지 않지만 같은 죽음이 다른
	// 원인으로 다시 보고되어 이중 반영되지 않도록 먼저 소비 처리한다.
	Registration->bDefeatReported = true;
	OutResult.KillCategory = KillCategory;
	OutResult.PreviousStack = CurrentGunAdaptationStack;
	OutResult.PreviousState = CurrentAdaptationState;

	switch (KillCategory)
	{
	case EEnemyFinalKillCategory::Gun:
		CurrentGunAdaptationStack = ActiveDefinition->ClampStack(
			CurrentGunAdaptationStack + ActiveDefinition->GunKillIncrement);
		break;
	case EEnemyFinalKillCategory::NonGun:
		if (CurrentGunAdaptationStack >= ActiveDefinition->ResistanceLevel1Threshold)
		{
			CurrentGunAdaptationStack = 0;
			OutResult.bAdaptationBroken = true;
			OutResult.BreakStunSeconds = ActiveDefinition->ResolveBreakStunSeconds(
				OutResult.PreviousStack);
		}
		else
		{
			CurrentGunAdaptationStack = ActiveDefinition->ClampStack(
				CurrentGunAdaptationStack - ActiveDefinition->NonGunKillDecrement);
		}
		break;
	case EEnemyFinalKillCategory::Ignore:
	default:
		break;
	}

	CurrentAdaptationState = ActiveDefinition->ResolveState(CurrentGunAdaptationStack);
	OutResult.CurrentStack = CurrentGunAdaptationStack;
	OutResult.CurrentState = CurrentAdaptationState;
	OnAdaptationUpdated.Broadcast(OutResult);
	return true;
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

bool UEnemyAdaptationSubsystem::IsCurrentRegistration(
	const AEnemyBase* Enemy,
	const FEnemyRegistration& Registration,
	int32 GameplayGeneration,
	int32 PoolLeaseSerial) const
{
	if (Registration.GameplayGeneration != GameplayGeneration
		|| Registration.PoolLeaseSerial != PoolLeaseSerial)
	{
		return false;
	}

	if (!Enemy->IsPoolManaged())
	{
		return PoolLeaseSerial == 0
			&& GameplayGeneration == static_cast<int32>(ActiveGameplayGeneration);
	}

	return Enemy->GetEnemyPoolState() == EEnemyPoolState::CombatActive
		&& Enemy->MatchesPoolLease(GameplayGeneration, PoolLeaseSerial);
}

void UEnemyAdaptationSubsystem::RefreshResolvedState()
{
	if (!ActiveDefinition)
	{
		CurrentAdaptationState = EEnemyAdaptationState::Normal;
		return;
	}

	CurrentGunAdaptationStack = ActiveDefinition->ClampStack(CurrentGunAdaptationStack);
	CurrentAdaptationState = ActiveDefinition->ResolveState(CurrentGunAdaptationStack);
}

void UEnemyAdaptationSubsystem::ResetAdaptationState()
{
	CurrentGunAdaptationStack = 0;
	CurrentAdaptationState = EEnemyAdaptationState::Normal;
}

void UEnemyAdaptationSubsystem::LoadConfiguredDefinition()
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	ActiveDefinition = Settings
		? Settings->EnemyAdaptationDefinition.LoadSynchronous()
		: nullptr;
	RefreshResolvedState();
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
	ResetAdaptationState();
	ActiveDefinition = nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
void UEnemyAdaptationSubsystem::SetDefinitionForTesting(UEnemyAdaptationDefinition* Definition)
{
	ActiveDefinition = Definition;
	RefreshResolvedState();
}
#endif
