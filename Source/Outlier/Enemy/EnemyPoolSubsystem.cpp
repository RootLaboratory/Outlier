#include "Enemy/EnemyPoolSubsystem.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierArenaSettings.h"
#include "Subsystems/SubsystemCollection.h"

void UEnemyPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UOutlierArenaSubsystem* ArenaSubsystem =
		Collection.InitializeDependency<UOutlierArenaSubsystem>())
	{
		ArenaSubsystem->OnArenaShown.AddUObject(this, &UEnemyPoolSubsystem::HandleArenaReady);
		ArenaSubsystem->OnArenaGameplayReady.AddUObject(
			this,
			&UEnemyPoolSubsystem::HandleArenaGameplayReady);
		ArenaSubsystem->OnArenaGameplayReloadStarted.AddUObject(
			this,
			&UEnemyPoolSubsystem::HandleArenaGameplayReloadStarted);
		ArenaSubsystem->OnArenaReleased.AddUObject(this, &UEnemyPoolSubsystem::HandleArenaReleased);
	}
}

void UEnemyPoolSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UOutlierArenaSubsystem* ArenaSubsystem = World->GetSubsystem<UOutlierArenaSubsystem>())
		{
			ArenaSubsystem->OnArenaShown.RemoveAll(this);
			ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
			ArenaSubsystem->OnArenaGameplayReloadStarted.RemoveAll(this);
			ArenaSubsystem->OnArenaReleased.RemoveAll(this);
		}
	}

	DestroyPool();
	Super::Deinitialize();
}

void UEnemyPoolSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	if (const UOutlierArenaSubsystem* ArenaSubsystem =
		InWorld.GetSubsystem<UOutlierArenaSubsystem>();
		ArenaSubsystem && ArenaSubsystem->IsArenaReady())
	{
		PrewarmConfiguredPool();
	}
}

bool UEnemyPoolSubsystem::PrewarmConfiguredPool()
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	UEnemyPoolDefinition* Definition = Settings
		? Settings->EnemyPoolDefinition.LoadSynchronous()
		: nullptr;
	if (!Definition)
	{
		return false;
	}

	return PrewarmPool(Definition);
}

bool UEnemyPoolSubsystem::PrewarmPool(UEnemyPoolDefinition* Definition)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !Definition)
	{
		return false;
	}

	if (ActiveDefinition.IsValid() && ActiveDefinition.Get() != Definition && !Buckets.IsEmpty())
	{
		DestroyPool();
	}
	ActiveDefinition = Definition;

	bool bAllCreated = true;
	for (const FEnemyPoolEntry& Entry : Definition->Entries)
	{
		UClass* LoadedClass = Entry.EnemyClass.LoadSynchronous();
		if (!LoadedClass || !LoadedClass->IsChildOf(AEnemyBase::StaticClass())
			|| Entry.MaxCount < 1 || Entry.PrewarmCount < 0
			|| Entry.PrewarmCount > Entry.MaxCount)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[EnemyPool] Invalid entry Class=%s Prewarm=%d Max=%d"),
				*GetNameSafe(LoadedClass), Entry.PrewarmCount, Entry.MaxCount);
			bAllCreated = false;
			continue;
		}

		TSubclassOf<AEnemyBase> EnemyClass = LoadedClass;
		FEnemyPoolBucket& Bucket = Buckets.FindOrAdd(EnemyClass);
		Bucket.EnemyClass = EnemyClass;
		Bucket.MaxCount = Entry.MaxCount;
		CompactBucket(Bucket);

		// Prewarm은 전체 보유량의 하한이다. 전투 중인 적도 포함해야 Ready 재통보 때 중복 생성하지 않는다.
		while (Bucket.IdleEnemies.Num() + Bucket.LeasedEnemies.Num() < Entry.PrewarmCount)
		{
			if (!CreatePoolEnemy(Bucket))
			{
				bAllCreated = false;
				break;
			}
		}
	}

	return bAllCreated;
}

AEnemyBase* UEnemyPoolSubsystem::CreatePoolEnemy(FEnemyPoolBucket& Bucket)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !Bucket.EnemyClass
		|| Bucket.IdleEnemies.Num() + Bucket.LeasedEnemies.Num() >= Bucket.MaxCount)
	{
		return nullptr;
	}

	const FTransform ParkingTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -1000000.0));
	AEnemyBase* Enemy = World->SpawnActorDeferred<AEnemyBase>(
		Bucket.EnemyClass,
		ParkingTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy)
	{
		return nullptr;
	}

	// BeginPlay 전에 표시해야 배치 적 등록과 StateTree 자동 시작 경로를 건너뛴다.
	Enemy->PrepareForPoolSpawn(this);
	UGameplayStatics::FinishSpawningActor(Enemy, ParkingTransform);
	Bucket.IdleEnemies.Add(Enemy);

	UE_LOG(LogTemp, Display,
		TEXT("[EnemyPool] Created Enemy=%s Class=%s Total=%d Max=%d"),
		*GetNameSafe(Enemy),
		*GetNameSafe(Bucket.EnemyClass),
		Bucket.IdleEnemies.Num() + Bucket.LeasedEnemies.Num(),
		Bucket.MaxCount);
	return Enemy;
}

AEnemyBase* UEnemyPoolSubsystem::AcquireIdleEnemy(FEnemyPoolBucket& Bucket)
{
	CompactBucket(Bucket);
	AEnemyBase* Enemy = nullptr;
	while (!Bucket.IdleEnemies.IsEmpty() && !Enemy)
	{
		Enemy = Bucket.IdleEnemies.Pop(EAllowShrinking::No).Get();
	}
	if (!Enemy)
	{
		Enemy = CreatePoolEnemy(Bucket);
		if (Enemy)
		{
			// 생성 경로는 먼저 Idle에 등록한다. 이번 대여에서 바로 쓸 적은 대기 목록에서 꺼낸다.
			Bucket.IdleEnemies.RemoveSingleSwap(Enemy, EAllowShrinking::No);
		}
	}
	return Enemy;
}

AEnemyBase* UEnemyPoolSubsystem::LeaseEnemy(
	TSubclassOf<AEnemyBase> EnemyClass,
	const FTransform& SpawnTransform,
	const FEnemyPoolLeaseContext& Context)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !EnemyClass)
	{
		return nullptr;
	}

	FEnemyPoolBucket* Bucket = Buckets.Find(EnemyClass);
	if (!Bucket)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EnemyPool] Lease rejected because class is not configured Class=%s"),
			*GetNameSafe(EnemyClass));
		return nullptr;
	}

	AEnemyBase* Enemy = AcquireIdleEnemy(*Bucket);
	if (!Enemy)
	{
		// 부족해도 사용 중인 적을 회수하거나 상한을 넘기지 않는다. Wave 쪽 Pending 재시도가 기다린다.
		const double CurrentTimeSeconds = World->GetTimeSeconds();
		if (CurrentTimeSeconds - Bucket->LastExhaustedLogSeconds >= 5.0)
		{
			Bucket->LastExhaustedLogSeconds = CurrentTimeSeconds;
			UE_LOG(LogTemp, Warning,
				TEXT("[EnemyPool] Exhausted Class=%s Leased=%d Max=%d"),
				*GetNameSafe(EnemyClass), Bucket->LeasedEnemies.Num(), Bucket->MaxCount);
		}
		return nullptr;
	}

	++NextLeaseSerial;
	if (NextLeaseSerial == 0)
	{
		++NextLeaseSerial;
	}

	// BeginPoolLease에서 연출 콜백이 즉시 올 수 있으므로 대여 목록을 먼저 확정한다.
	Bucket->LeasedEnemies.Add(Enemy);
	if (!Enemy->BeginPoolLease(Context, SpawnTransform, NextLeaseSerial))
	{
		Bucket->LeasedEnemies.Remove(Enemy);
		Bucket->IdleEnemies.Add(Enemy);
		return nullptr;
	}
	return Enemy;
}

bool UEnemyPoolSubsystem::ReturnEnemy(
	AEnemyBase* Enemy,
	int32 GameplayGeneration,
	int32 LeaseSerial)
{
	// 같은 Actor가 다시 대여됐을 수 있다. 이전 연출의 늦은 반환은 현재 대여를 건드리지 않는다.
	if (!IsValid(Enemy) || !Enemy->MatchesPoolLease(GameplayGeneration, LeaseSerial))
	{
		return false;
	}

	FEnemyPoolBucket* Bucket = Buckets.Find(Enemy->GetClass());
	if (!Bucket || !Bucket->LeasedEnemies.Remove(Enemy))
	{
		return false;
	}

	Enemy->FinishPoolReturn(GameplayGeneration, LeaseSerial);
	Bucket->IdleEnemies.AddUnique(Enemy);
	return true;
}

void UEnemyPoolSubsystem::DestroyPool()
{
	for (TPair<TSubclassOf<AEnemyBase>, FEnemyPoolBucket>& Entry : Buckets)
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Entry.Value.LeasedEnemies)
		{
			if (AEnemyBase* Enemy = EnemyPtr.Get())
			{
				Enemy->InvalidatePoolLease();
				Enemy->Destroy();
			}
		}
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Entry.Value.IdleEnemies)
		{
			if (AEnemyBase* Enemy = EnemyPtr.Get())
			{
				Enemy->InvalidatePoolLease();
				Enemy->Destroy();
			}
		}
	}

	Buckets.Reset();
	ActiveDefinition.Reset();
}

void UEnemyPoolSubsystem::CompactBucket(FEnemyPoolBucket& Bucket) const
{
	Bucket.IdleEnemies.RemoveAll(
		[](const TWeakObjectPtr<AEnemyBase>& Enemy)
		{
			return !Enemy.IsValid();
		});
	for (auto It = Bucket.LeasedEnemies.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

int32 UEnemyPoolSubsystem::GetTotalCount(TSubclassOf<AEnemyBase> EnemyClass) const
{
	const FEnemyPoolBucket* Bucket = Buckets.Find(EnemyClass);
	return Bucket ? Bucket->IdleEnemies.Num() + Bucket->LeasedEnemies.Num() : 0;
}

int32 UEnemyPoolSubsystem::GetIdleCount(TSubclassOf<AEnemyBase> EnemyClass) const
{
	const FEnemyPoolBucket* Bucket = Buckets.Find(EnemyClass);
	return Bucket ? Bucket->IdleEnemies.Num() : 0;
}

int32 UEnemyPoolSubsystem::GetLeasedCount(TSubclassOf<AEnemyBase> EnemyClass) const
{
	const FEnemyPoolBucket* Bucket = Buckets.Find(EnemyClass);
	return Bucket ? Bucket->LeasedEnemies.Num() : 0;
}

void UEnemyPoolSubsystem::HandleArenaReady()
{
	PrewarmConfiguredPool();
}

void UEnemyPoolSubsystem::HandleArenaGameplayReady(uint32 GameplayGeneration)
{
	(void)GameplayGeneration;
	PrewarmConfiguredPool();
}

void UEnemyPoolSubsystem::HandleArenaGameplayReloadStarted(uint32 GameplayGeneration)
{
	(void)GameplayGeneration;
	// 같은 Gameplay 내 반환은 재사용하지만, 리로드 경계에서는 이전 맵 수명의 적을 모두 폐기한다.
	DestroyPool();
}

void UEnemyPoolSubsystem::HandleArenaReleased()
{
	DestroyPool();
}
