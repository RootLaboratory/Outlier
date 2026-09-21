#include "Enemy/EnemyAdaptationSubsystem.h"

#include "Enemy/EnemyAdaptationDefinition.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Outlier.h"
#include "OutlierArenaSettings.h"
#include "Subsystems/SubsystemCollection.h"
#include "Team/OutlierTeamIds.h"

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
		SynchronizeEnemyState(Enemy, *Existing);
		return true;
	}

	ActiveEnemies.Add(EnemyPtr, Registration);
	SynchronizeEnemyState(Enemy, Registration);
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Registered Enemy=%s Target=%s PoolManaged=%s Generation=%d Lease=%d Stack=%d State=%s ActiveCount=%d"),
		*GetNameSafe(Enemy),
		Enemy->HasEnemyTrait(OutlierGameplayTags::Enemy::Adaptation::Target())
			? TEXT("true")
			: TEXT("false"),
		Enemy->IsPoolManaged() ? TEXT("true") : TEXT("false"),
		Registration.GameplayGeneration,
		Registration.PoolLeaseSerial,
		CurrentGunAdaptationStack,
		*UEnum::GetValueAsString(Enemy->GetAdaptationState()),
		ActiveEnemies.Num());
	return true;
}

void UEnemyAdaptationSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	if (Enemy)
	{
		const int32 RemovedCount = ActiveEnemies.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
		Enemy->ApplyAdaptationState(EEnemyAdaptationState::Normal);
		if (RemovedCount > 0)
		{
			UE_LOG(
				LogOutlier,
				Display,
				TEXT("[EnemyAdaptation] Unregistered Enemy=%s ActiveCount=%d"),
				*GetNameSafe(Enemy),
				ActiveEnemies.Num());
		}
	}
}

void UEnemyAdaptationSubsystem::ResetActiveEnemies()
{
	for (const TPair<TWeakObjectPtr<AEnemyBase>, FEnemyRegistration>& Entry : ActiveEnemies)
	{
		if (AEnemyBase* Enemy = Entry.Key.Get())
		{
			Enemy->ApplyAdaptationState(EEnemyAdaptationState::Normal);
		}
	}
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

float UEnemyAdaptationSubsystem::GetCurrentGunDamageMultiplier() const
{
	return ActiveDefinition
		? ActiveDefinition->ResolveGunDamageMultiplier(CurrentGunAdaptationStack)
		: 1.0f;
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
	FinalizeAdaptationUpdate(Result);
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
	FEnemyRegistration* Registration = nullptr;
	if (!ResolveCurrentRegistration(
		Enemy,
		GameplayGeneration,
		PoolLeaseSerial,
		Registration))
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
			ApplyAdaptationBreak(OutResult);
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

	FinalizeAdaptationUpdate(OutResult);
	return true;
}

bool UEnemyAdaptationSubsystem::ReportPistolHit(
	AEnemyBase* Enemy,
	int32 GameplayGeneration,
	int32 PoolLeaseSerial,
	bool bConsumeDefeat,
	FEnemyAdaptationUpdateResult& OutResult)
{
	OutResult = FEnemyAdaptationUpdateResult();
	FEnemyRegistration* Registration = nullptr;
	if (!ResolveCurrentRegistration(
		Enemy,
		GameplayGeneration,
		PoolLeaseSerial,
		Registration)
		|| CurrentGunAdaptationStack < ActiveDefinition->ResistanceLevel1Threshold)
	{
		return false;
	}

	// 치명 권총 명중은 내성 파괴와 처치 소비를 한 번에 완료한다. 비치명 명중은
	// Enemy 수명을 유지해 이후 다른 공격의 최종 처치를 정상 보고할 수 있게 한다.
	if (bConsumeDefeat)
	{
		Registration->bDefeatReported = true;
	}

	OutResult.KillCategory = EEnemyFinalKillCategory::Ignore;
	OutResult.PreviousStack = CurrentGunAdaptationStack;
	OutResult.PreviousState = CurrentAdaptationState;
	ApplyAdaptationBreak(OutResult);
	FinalizeAdaptationUpdate(OutResult);
	return true;
}

bool UEnemyAdaptationSubsystem::ResolveCurrentRegistration(
	AEnemyBase* Enemy,
	int32 GameplayGeneration,
	int32 PoolLeaseSerial,
	FEnemyRegistration*& OutRegistration)
{
	OutRegistration = nullptr;
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

	OutRegistration = Registration;
	return true;
}

void UEnemyAdaptationSubsystem::ApplyAdaptationBreak(
	FEnemyAdaptationUpdateResult& OutResult)
{
	CurrentGunAdaptationStack = 0;
	OutResult.bAdaptationBroken = true;
	OutResult.BreakStunSeconds = ActiveDefinition->ResolveBreakStunSeconds(
		OutResult.PreviousStack);
	OutResult.BreakDamage = ActiveDefinition->ShieldBreakDamage;
}

void UEnemyAdaptationSubsystem::FinalizeAdaptationUpdate(
	FEnemyAdaptationUpdateResult& OutResult)
{
	CurrentAdaptationState = ActiveDefinition->ResolveState(CurrentGunAdaptationStack);
	OutResult.CurrentStack = CurrentGunAdaptationStack;
	OutResult.CurrentState = CurrentAdaptationState;

	// Enemy 표현 상태를 먼저 확정해야 이 갱신을 구독하는 후속 파열 처리도
	// 이미 갱신된 공용 단계와 같은 결과를 관찰한다.
	SynchronizeActiveEnemyStates();
	if (OutResult.bAdaptationBroken)
	{
		ApplyAdaptationBreakToCombatField(OutResult);
	}
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Stack updated PreviousStack=%d CurrentStack=%d PreviousState=%s CurrentState=%s Category=%s Broken=%s Registered=%d"),
		OutResult.PreviousStack,
		OutResult.CurrentStack,
		*UEnum::GetValueAsString(OutResult.PreviousState),
		*UEnum::GetValueAsString(OutResult.CurrentState),
		*UEnum::GetValueAsString(OutResult.KillCategory),
		OutResult.bAdaptationBroken ? TEXT("true") : TEXT("false"),
		GetRegisteredEnemyCount());
	OnAdaptationUpdated.Broadcast(OutResult);
}

void UEnemyAdaptationSubsystem::ApplyAdaptationBreakToCombatField(
	const FEnemyAdaptationUpdateResult& Result)
{
	TArray<TWeakObjectPtr<AEnemyBase>> Targets;
	CollectAdaptationBreakTargets(Targets);

	int32 AppliedCount = 0;
	int32 DamagedCount = 0;
	int32 StunnedCount = 0;
	int32 KilledCount = 0;
	for (const TWeakObjectPtr<AEnemyBase>& TargetPtr : Targets)
	{
		AEnemyBase* Target = TargetPtr.Get();
		if (!IsValid(Target) || Target->IsDead())
		{
			continue;
		}

		// 파괴 피해로 사망하면 Enemy가 즉시 등록 목록에서 빠질 수 있다. 위에서 복사한
		// 약한 참조만 순회하므로 다음 대상은 등록부 변경과 무관하게 안전하게 처리된다.
		const FEnemyAdaptationBreakApplicationResult Application =
			Target->ApplyAdaptationBreakEffects(
				Result.BreakDamage,
				Result.BreakStunSeconds,
				this);
		++AppliedCount;
		DamagedCount += Application.AppliedDamage > 0.0f ? 1 : 0;
		StunnedCount += Application.bStunApplied ? 1 : 0;
		KilledCount += Application.bKilled ? 1 : 0;

		UE_LOG(
			LogOutlier,
			Display,
			TEXT("[EnemyAdaptation] Break target applied Enemy=%s Damage=%.2f Stun=%.2f Killed=%s"),
			*GetNameSafe(Target),
			Application.AppliedDamage,
			Application.bStunApplied ? Result.BreakStunSeconds : 0.0f,
			Application.bKilled ? TEXT("true") : TEXT("false"));
	}

	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Break field applied PreviousStack=%d Targets=%d Applied=%d Damaged=%d Stunned=%d Killed=%d Damage=%.2f Stun=%.2f"),
		Result.PreviousStack,
		Targets.Num(),
		AppliedCount,
		DamagedCount,
		StunnedCount,
		KilledCount,
		Result.BreakDamage,
		Result.BreakStunSeconds);
}

void UEnemyAdaptationSubsystem::CollectAdaptationBreakTargets(
	TArray<TWeakObjectPtr<AEnemyBase>>& OutTargets) const
{
	OutTargets.Reset();
	OutTargets.Reserve(ActiveEnemies.Num());
	for (const TPair<TWeakObjectPtr<AEnemyBase>, FEnemyRegistration>& Entry : ActiveEnemies)
	{
		AEnemyBase* Enemy = Entry.Key.Get();
		if (const TCHAR* ExclusionReason =
			ResolveAdaptationBreakExclusionReason(Enemy, Entry.Value))
		{
			UE_LOG(
				LogOutlier,
				Display,
				TEXT("[EnemyAdaptation] Break target excluded Enemy=%s Reason=%s InCombat=%s Possessed=%s Team=%d Generation=%d Lease=%d"),
				*GetNameSafe(Enemy),
				ExclusionReason,
				Enemy && Enemy->IsInCombat() ? TEXT("true") : TEXT("false"),
				Enemy && Enemy->IsEnemyPossessed() ? TEXT("true") : TEXT("false"),
				Enemy ? Enemy->GetGenericTeamId().GetId() : INDEX_NONE,
				Entry.Value.GameplayGeneration,
				Entry.Value.PoolLeaseSerial);
		}
		else
		{
			OutTargets.Add(Entry.Key);
		}
	}
}

const TCHAR* UEnemyAdaptationSubsystem::ResolveAdaptationBreakExclusionReason(
	const AEnemyBase* Enemy,
	const FEnemyRegistration& Registration) const
{
	if (!IsValid(Enemy))
	{
		return TEXT("Invalid");
	}
	if (Enemy->IsDead())
	{
		return TEXT("Dead");
	}
	if (!Enemy->IsInCombat())
	{
		return TEXT("NonCombat");
	}
	if (Enemy->GetGenericTeamId().GetId() != OutlierTeamIds::Enemy)
	{
		return TEXT("PlayerTeam");
	}
	if (!IsCurrentRegistration(
		Enemy,
		Registration,
		Registration.GameplayGeneration,
		Registration.PoolLeaseSerial))
	{
		return TEXT("StaleRegistration");
	}

	return nullptr;
}

void UEnemyAdaptationSubsystem::SynchronizeActiveEnemyStates()
{
	for (auto It = ActiveEnemies.CreateIterator(); It; ++It)
	{
		AEnemyBase* Enemy = It.Key().Get();
		if (!IsValid(Enemy))
		{
			It.RemoveCurrent();
			continue;
		}

		SynchronizeEnemyState(Enemy, It.Value());
	}
}

void UEnemyAdaptationSubsystem::SynchronizeEnemyState(
	AEnemyBase* Enemy,
	const FEnemyRegistration& Registration) const
{
	if (IsValid(Enemy))
	{
		Enemy->ApplyAdaptationState(
			ResolveEnemyPresentationState(Enemy, Registration));
	}
}

EEnemyAdaptationState UEnemyAdaptationSubsystem::ResolveEnemyPresentationState(
	const AEnemyBase* Enemy,
	const FEnemyRegistration& Registration) const
{
	if (!IsValid(Enemy)
		|| Enemy->IsDead()
		|| !Enemy->HasEnemyTrait(OutlierGameplayTags::Enemy::Adaptation::Target())
		|| !IsCurrentRegistration(
			Enemy,
			Registration,
			Registration.GameplayGeneration,
			Registration.PoolLeaseSerial))
	{
		return EEnemyAdaptationState::Normal;
	}

	return CurrentAdaptationState;
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
		SynchronizeActiveEnemyStates();
		return;
	}

	CurrentGunAdaptationStack = ActiveDefinition->ClampStack(CurrentGunAdaptationStack);
	CurrentAdaptationState = ActiveDefinition->ResolveState(CurrentGunAdaptationStack);
	SynchronizeActiveEnemyStates();
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
		UE_LOG(
			LogOutlier,
			Display,
			TEXT("[EnemyAdaptation] Arena lifecycle Event=Shown Generation=%u Stack=%d State=%s Registrations=%d Accepting=true"),
			ActiveGameplayGeneration,
			CurrentGunAdaptationStack,
			*UEnum::GetValueAsString(CurrentAdaptationState),
			GetRegisteredEnemyCount());
	}
}

void UEnemyAdaptationSubsystem::HandleArenaGameplayReloadStarted(uint32 GameplayGeneration)
{
	// 새 Data Layer의 BeginPlay가 오기 전까지 문을 닫아 이전 Generation의 지연 이벤트를 버린다.
	const int32 ClearedRegistrationCount = GetRegisteredEnemyCount();
	bAcceptingRegistrations = false;
	ActiveGameplayGeneration = GameplayGeneration;
	ResetActiveEnemies();
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Arena lifecycle Event=ReloadStarted Generation=%u Stack=%d State=%s ClearedRegistrations=%d Accepting=false"),
		ActiveGameplayGeneration,
		CurrentGunAdaptationStack,
		*UEnum::GetValueAsString(CurrentAdaptationState),
		ClearedRegistrationCount);
}

void UEnemyAdaptationSubsystem::HandleArenaGameplayGCReady(uint32 GameplayGeneration)
{
	// 이전 세대 Actor EndPlay와 GC가 검증된 뒤 새 Data Layer Actor의 BeginPlay 등록을 허용한다.
	ActiveGameplayGeneration = GameplayGeneration;
	bAcceptingRegistrations = true;
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Arena lifecycle Event=GCReady Generation=%u Stack=%d State=%s Registrations=%d Accepting=true"),
		ActiveGameplayGeneration,
		CurrentGunAdaptationStack,
		*UEnum::GetValueAsString(CurrentAdaptationState),
		GetRegisteredEnemyCount());
}

void UEnemyAdaptationSubsystem::HandleArenaGameplayReady(uint32 GameplayGeneration)
{
	ActiveGameplayGeneration = GameplayGeneration;
	LoadConfiguredDefinition();
	bAcceptingRegistrations = true;
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Arena lifecycle Event=GameplayReady Generation=%u Stack=%d State=%s Registrations=%d Accepting=true"),
		ActiveGameplayGeneration,
		CurrentGunAdaptationStack,
		*UEnum::GetValueAsString(CurrentAdaptationState),
		GetRegisteredEnemyCount());
}

void UEnemyAdaptationSubsystem::HandleArenaReleased()
{
	const int32 PreviousStack = CurrentGunAdaptationStack;
	const int32 ClearedRegistrationCount = GetRegisteredEnemyCount();
	bAcceptingRegistrations = false;
	ActiveGameplayGeneration = 0;
	ResetActiveEnemies();
	ResetAdaptationState();
	ActiveDefinition = nullptr;
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Arena lifecycle Event=Released Generation=0 PreviousStack=%d Stack=0 State=%s ClearedRegistrations=%d Accepting=false"),
		PreviousStack,
		*UEnum::GetValueAsString(CurrentAdaptationState),
		ClearedRegistrationCount);
}

#if WITH_DEV_AUTOMATION_TESTS
void UEnemyAdaptationSubsystem::SetDefinitionForTesting(UEnemyAdaptationDefinition* Definition)
{
	ActiveDefinition = Definition;
	RefreshResolvedState();
}
#endif
