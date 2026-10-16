#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/VECDrone.h"
#include "Animation/AnimMontage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

// 테스트 관측은 연출 요청과 기존 피해 경로에 한정한다. 테스트용 게임플레이 상태는 추가하지 않는다.
struct FDroneHitReactionTestAccess
{
	static void Initialize(AVECDrone& Drone)
	{
		Drone.RefreshAbilitySystemActorInfo();
		Drone.GetOutlierAbilitySystemComponent()->InitializeVitalityToSelf(100.0f);
		Drone.BindGasVitalityObservers();
		Drone.HitReactionMontage = NewObject<UAnimMontage>(&Drone);
		Drone.AttackPhase = EEnemyAttackPhase::Firing;
		Drone.SetPoolPresentationAutoCompleteForTesting(false);
	}
	static int64 Serial(const AVECDrone& Drone) { return Drone.HitReactionSerial; }
	static bool CanPlay(const AVECDrone& Drone) { return Drone.CanPlayHitReaction(); }
	static void SetMontage(AVECDrone& Drone, UAnimMontage* Montage) { Drone.HitReactionMontage = Montage; }
	static void SetImpact(AVECDrone& Drone, bool bActive) { Drone.bImpactReactionActive = bActive; }
	static void SetLease(AVECDrone& Drone, int32 Generation, int32 Lease, EEnemyPoolState State)
	{
		Drone.PoolGameplayGeneration = Generation;
		Drone.PoolLeaseSerial = Lease;
		Drone.PoolState = State;
	}
	static bool Accept(AVECDrone& Drone, int32 Generation, int32 Lease, int64 Serial)
	{
		return Drone.AcceptHitReactionMessage(Generation, Lease, Serial);
	}
	static void ResetPresentation(AVECDrone& Drone) { Drone.ResetPoolPresentationState(); }
	static double NextTime(const AVECDrone& Drone) { return Drone.NextHitReactionTime; }
};

namespace
{
struct FDroneHitReactionWorld
{
	UWorld* World = nullptr;
	FDroneHitReactionWorld()
	{
		if (!GEngine) return;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("DroneHitReactionTest")), GetTransientPackage());
		if (!World) return;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}
	~FDroneHitReactionWorld()
	{
		if (!World) return;
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	AVECDrone* Spawn()
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVECDrone* Drone = World
			? World->SpawnActor<AVECDrone>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters) : nullptr;
		if (Drone) FDroneHitReactionTestAccess::Initialize(*Drone);
		return Drone;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneHitReactionDamageTest,
	"Outlier.Enemy.HitReaction.DamageAndInterval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneHitReactionDamageTest::RunTest(const FString& Parameters)
{
	FDroneHitReactionWorld Fixture;
	AVECDrone* Drone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	FOutlierDamageRequest Request;
	Request.DamageTag = OutlierGameplayTags::Damage::Weapon();
	TestEqual(TEXT("Zero damage does not request a reaction"), Drone->ReceiveOutlierDamage(Request), 0.0f);
	TestEqual(TEXT("Zero damage leaves request count unchanged"), FDroneHitReactionTestAccess::Serial(*Drone), int64(0));

	Request.DamageAmount = 10.0f;
	const EEnemyAttackPhase PreviousPhase = Drone->GetAttackPhase();
	TestEqual(TEXT("First damage applies once"), Drone->ReceiveOutlierDamage(Request), 10.0f);
	TestEqual(TEXT("First accepted hit requests presentation"), FDroneHitReactionTestAccess::Serial(*Drone), int64(1));
	TestEqual(TEXT("Immediate second damage still applies"), Drone->ReceiveOutlierDamage(Request), 10.0f);
	TestEqual(TEXT("Only presentation is rate limited"), FDroneHitReactionTestAccess::Serial(*Drone), int64(1));
	Fixture.World->Tick(LEVELTICK_All, 0.21f);
	TestEqual(TEXT("Damage after the interval applies"), Drone->ReceiveOutlierDamage(Request), 10.0f);
	TestEqual(TEXT("Interval permits the next request"), FDroneHitReactionTestAccess::Serial(*Drone), int64(2));
	TestEqual(TEXT("All three hits reduce health"), Drone->GetCurrentHealth(), 70.0f);
	TestEqual(TEXT("Reaction does not change attack phase"), Drone->GetAttackPhase(), PreviousPhase);

	Drone->SetCanBeDamaged(false);
	TestEqual(TEXT("Rejected damage stays rejected"), Drone->ReceiveOutlierDamage(Request), 0.0f);
	TestEqual(TEXT("Rejected damage sends no request"), FDroneHitReactionTestAccess::Serial(*Drone), int64(2));
	Drone->SetCanBeDamaged(true);
	Fixture.World->Tick(LEVELTICK_All, 0.21f);
	FDroneHitReactionTestAccess::SetMontage(*Drone, nullptr);
	TestEqual(TEXT("Missing montage never prevents damage"), Drone->ReceiveOutlierDamage(Request), 10.0f);
	TestEqual(TEXT("Missing montage sends no request"), FDroneHitReactionTestAccess::Serial(*Drone), int64(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneHitReactionPriorityTest,
	"Outlier.Enemy.HitReaction.PriorityAndDeath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneHitReactionPriorityTest::RunTest(const FString& Parameters)
{
	FDroneHitReactionWorld Fixture;
	AVECDrone* Drone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	TestTrue(TEXT("Unmanaged living drone allows a reaction"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	FDroneHitReactionTestAccess::SetImpact(*Drone, true);
	TestFalse(TEXT("Common impact reaction has priority"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	FDroneHitReactionTestAccess::SetImpact(*Drone, false);

	UOutlierAbilitySystemComponent* ASC = Drone->GetOutlierAbilitySystemComponent();
	ASC->AddLooseGameplayTag(OutlierGameplayTags::State::PossessPending());
	TestFalse(TEXT("Possession transition blocks a reaction"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	ASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::PossessPending());
	ASC->AddLooseGameplayTag(OutlierGameplayTags::State::Stunned());
	TestFalse(TEXT("Stun blocks a reaction"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	ASC->RemoveLooseGameplayTag(OutlierGameplayTags::State::Stunned());
	TestTrue(TEXT("Clearing temporary blockers permits a reaction again"), FDroneHitReactionTestAccess::CanPlay(*Drone));

	AVECDrone* StunDrone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Weapon stun target"), StunDrone)) return false;
	FOutlierDamageRequest StunRequest;
	StunRequest.DamageAmount = 10.0f;
	StunRequest.StunDurationSeconds = 1.0f;
	StunRequest.DamageTag = OutlierGameplayTags::Damage::Weapon();
	TestEqual(TEXT("Stunning hit still applies its damage"), StunDrone->ReceiveOutlierDamage(StunRequest), 10.0f);
	TestFalse(TEXT("Damage-carried stun prevents the following hit reaction"), FDroneHitReactionTestAccess::CanPlay(*StunDrone));
	TestEqual(TEXT("Stunning hit consumes no play interval"), FDroneHitReactionTestAccess::NextTime(*StunDrone), 0.0);

	FDroneHitReactionTestAccess::SetLease(*Drone, 1, 1, EEnemyPoolState::SpawnPresentation);
	TestFalse(TEXT("Pool spawn presentation blocks a reaction"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	FDroneHitReactionTestAccess::SetLease(*Drone, 1, 1, EEnemyPoolState::CombatActive);
	TestTrue(TEXT("Active lease allows a reaction"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	FOutlierDamageRequest Lethal;
	Lethal.DamageAmount = 200.0f;
	Lethal.DamageTag = OutlierGameplayTags::Damage::Weapon();
	Drone->ReceiveOutlierDamage(Lethal);
	TestTrue(TEXT("Existing death path still runs"), Drone->IsDead());
	TestFalse(TEXT("Post-damage death blocks a reaction"), FDroneHitReactionTestAccess::CanPlay(*Drone));
	TestEqual(TEXT("Lethal damage never consumes a play interval"), FDroneHitReactionTestAccess::NextTime(*Drone), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneHitReactionLeaseTest,
	"Outlier.Enemy.HitReaction.LeaseAndMessageOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneHitReactionLeaseTest::RunTest(const FString& Parameters)
{
	FDroneHitReactionWorld Fixture;
	AVECDrone* Drone = Fixture.Spawn();
	if (!TestNotNull(TEXT("Drone"), Drone)) return false;
	FDroneHitReactionTestAccess::SetLease(*Drone, 4, 10, EEnemyPoolState::CombatActive);
	FDroneHitReactionTestAccess::ResetPresentation(*Drone);
	TestTrue(TEXT("Current lease accepts a new message"), FDroneHitReactionTestAccess::Accept(*Drone, 4, 10, 10));
	TestFalse(TEXT("Duplicate message is discarded"), FDroneHitReactionTestAccess::Accept(*Drone, 4, 10, 10));
	TestTrue(TEXT("Later stop advances the presentation serial"), FDroneHitReactionTestAccess::Accept(*Drone, 4, 10, 12));
	TestFalse(TEXT("Delayed play cannot undo the stop"), FDroneHitReactionTestAccess::Accept(*Drone, 4, 10, 11));

	FOutlierDamageRequest Request;
	Request.DamageAmount = 1.0f;
	Request.DamageTag = OutlierGameplayTags::Damage::Weapon();
	Drone->ReceiveOutlierDamage(Request);
	TestTrue(TEXT("Damage consumes the request interval"), FDroneHitReactionTestAccess::NextTime(*Drone) > 0.0);
	const double PreviousNextTime = FDroneHitReactionTestAccess::NextTime(*Drone);
	FDroneHitReactionTestAccess::ResetPresentation(*Drone);
	TestEqual(TEXT("Same lease restoration preserves the interval"), FDroneHitReactionTestAccess::NextTime(*Drone), PreviousNextTime);
	FDroneHitReactionTestAccess::SetLease(*Drone, 4, 11, EEnemyPoolState::CombatActive);
	FDroneHitReactionTestAccess::ResetPresentation(*Drone);
	TestEqual(TEXT("New lease resets the interval"), FDroneHitReactionTestAccess::NextTime(*Drone), 0.0);
	TestFalse(TEXT("Previous lease message is discarded"), FDroneHitReactionTestAccess::Accept(*Drone, 4, 10, 13));
	TestFalse(TEXT("Previous generation message is discarded"), FDroneHitReactionTestAccess::Accept(*Drone, 3, 11, 14));
	TestTrue(TEXT("New lease accepts a fresh message"), FDroneHitReactionTestAccess::Accept(*Drone, 4, 11, 15));
	return true;
}

#endif
