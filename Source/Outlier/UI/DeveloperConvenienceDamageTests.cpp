#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceDamageTest,
	"Outlier.UI.DeveloperConvenience.Damage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceDamageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
	if (!TestNotNull(TEXT("Damage fixture world"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UClass* ModeClass = LoadClass<AOutlierGameMode>(nullptr,
		TEXT("/Game/Blueprints/Outlier/BP_OutlierGM.BP_OutlierGM_C"));
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	if (!ModeClass || !ShooterClass || !PartnerClass)
	{
		AddError(TEXT("Project GameMode, Shooter and Partner classes are required"));
		return false;
	}
	auto* Mode = World->SpawnActor<AOutlierGameMode>(ModeClass);
	auto* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	auto* Partner = World->SpawnActor<APartnerCharacter>(PartnerClass);
	auto* ShooterController = World->SpawnActor<AFirstPersonPlayerController>();
	auto* PartnerController = World->SpawnActor<APartnerPlayerController>();
	if (!Mode || !Shooter || !Partner || !ShooterController || !PartnerController)
	{
		AddError(TEXT("Damage fixture actors must spawn"));
		return false;
	}
	const auto AddState = [World](AFirstPersonPlayerController* Controller, EOutlierPlayerRole Role)
	{
		auto* State = World->SpawnActor<AOutlierPlayerState>();
		State->SetOwner(Controller);
		State->SetPlayerRole(Role);
		State->SetPairId(7);
		Controller->PlayerState = State;
		return State;
	};
	auto* ShooterState = AddState(ShooterController, EOutlierPlayerRole::Shooter);
	auto* PartnerState = AddState(PartnerController, EOutlierPlayerRole::Partner);
	ShooterController->Possess(Shooter);
	PartnerController->Possess(Partner);
	for (auto* State : { ShooterState, PartnerState })
	{
		State->SetShooterCharacter(Shooter);
		State->SetPartnerCharacter(Partner);
	}
	Shooter->DispatchBeginPlay();
	Partner->DispatchBeginPlay();
	ShooterState->SetAcquiredSuit(true);
	UOutlierAbilitySystemComponent* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	UOutlierAbilitySystemComponent* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	ShooterASC->InitializeForPawn(Shooter);
	PartnerASC->InitializeForPawn(Partner);
	ShooterASC->InitializeVitalityToSelf(100.0f);
	PartnerASC->InitializeVitalityToSelf(100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxShieldAttribute(), 100.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetShieldAttribute(), 60.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(), 50.0f);
	ShooterASC->SetNumericAttributeBase(UOutlierShieldAttributeSet::GetPartnerShieldAttribute(), 30.0f);

	// 서버 기동/매칭을 실행하지 않고 피해 경계가 실제 서버 GameMode를 조회하도록 연결한다.
	FObjectPropertyBase* AuthorityMode = FindFProperty<FObjectPropertyBase>(UWorld::StaticClass(), TEXT("AuthorityGameMode"));
	if (!TestNotNull(TEXT("Authority mode fixture property"), AuthorityMode))
	{
		return false;
	}
	AuthorityMode->SetObjectPropertyValue_InContainer(World, Mode);
	Mode->RefreshDeveloperConveniencePair(ShooterState, PartnerState);
	TestFalse(TEXT("Default option does not protect the Shooter"), OutlierDamage::IsDeveloperInvincible(Shooter));
	Mode->DeveloperConvenienceState.Options.bInvincible = true;
	Mode->PublishDeveloperConvenienceState();
	TestTrue(TEXT("Shooter is protected"), OutlierDamage::IsDeveloperInvincible(Shooter));
	TestTrue(TEXT("Partner is protected"), OutlierDamage::IsDeveloperInvincible(Partner));

	FOutlierDamageRequest Hit;
	Hit.DamageAmount = 15.0f;
	Hit.DamageTag = OutlierGameplayTags::Damage::Ranged();
	Hit.bHasDamageOrigin = true;
	Hit.DamageOrigin = FVector::ZeroVector;
	TestEqual(TEXT("Protected Shooter reports no applied damage"), OutlierDamage::Apply(Shooter, Hit), 0.0f);
	TestEqual(TEXT("Protected Partner reports no applied damage"), OutlierDamage::Apply(Partner, Hit), 0.0f);
	TestEqual(TEXT("Shooter health stays unchanged"), Shooter->GetCurHealth(), 100.0f);
	TestEqual(TEXT("Base shield stays unchanged"), Shooter->GetCurShield(), 60.0f);
	TestEqual(TEXT("Additional shield stays unchanged"), Shooter->GetCurPartnerShield(), 30.0f);
	TestEqual(TEXT("Partner health stays unchanged"), PartnerASC->GetNumericAttribute(UOutlierVitalAttributeSet::GetHealthAttribute()), 100.0f);
	Hit.DamageTag = OutlierDamage::InstantKillTag();
	OutlierDamage::Apply(Shooter, Hit);
	OutlierDamage::Apply(Partner, Hit);
	TestFalse(TEXT("Instant kill cannot kill a protected Shooter"), Shooter->IsDead());
	TestFalse(TEXT("Instant kill cannot reboot a protected Partner"), PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()));
	TestFalse(TEXT("Direct ASC damage is rejected"), ShooterASC->ApplyDamageToSelf(200.0f, nullptr, nullptr, Hit.DamageTag));

	// ASC 헬퍼를 우회해 IncomingDamage GE를 직접 넣어도 최종 실드/체력 경계에서 막힌다.
	FGameplayEffectSpecHandle DirectDamage = ShooterASC->MakeOutgoingSpec(
		UOutlierDamageGameplayEffect::StaticClass(), 1.0f, ShooterASC->MakeEffectContext());
	if (!TestTrue(TEXT("Direct damage spec"), DirectDamage.IsValid()))
	{
		return false;
	}
	DirectDamage.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Damage(), 999.0f);
	DirectDamage.Data->AddDynamicAssetTag(OutlierDamage::InstantKillTag());
	ShooterASC->ApplyGameplayEffectSpecToSelf(*DirectDamage.Data.Get());
	TestEqual(TEXT("Direct GE preserves health"), Shooter->GetCurHealth(), 100.0f);
	TestEqual(TEXT("Direct GE preserves base shield"), Shooter->GetCurShield(), 60.0f);
	TestEqual(TEXT("Direct GE preserves additional shield"), Shooter->GetCurPartnerShield(), 30.0f);
	TestEqual(TEXT("Blocked GE clears pending IncomingDamage"), Shooter->GetVitalAttributeSet()->GetIncomingDamage(), 0.0f);

	auto* FirstEnemy = World->SpawnActor<AEnemyBase>();
	auto* SecondEnemy = World->SpawnActor<AEnemyBase>();
	if (!TestNotNull(TEXT("First possession target"), FirstEnemy) || !TestNotNull(TEXT("Second possession target"), SecondEnemy))
	{
		return false;
	}
	FirstEnemy->GetOutlierAbilitySystemComponent()->InitializeForPawn(FirstEnemy);
	SecondEnemy->GetOutlierAbilitySystemComponent()->InitializeForPawn(SecondEnemy);
	TestFalse(TEXT("Unpossessed enemy is not protected"), OutlierDamage::IsDeveloperInvincible(FirstEnemy));
	PartnerController->Possess(FirstEnemy);
	TestTrue(TEXT("Possession immediately inherits shared protection"), OutlierDamage::IsDeveloperInvincible(FirstEnemy));
	TestTrue(TEXT("Original Partner remains protected without a controller"), OutlierDamage::IsDeveloperInvincible(Partner));
	Hit.DamageTag = OutlierGameplayTags::Damage::Ranged();
	Hit.StunDurationSeconds = 0.5f;
	const float EnemyHealth = FirstEnemy->GetCurrentHealth();
	TestEqual(TEXT("Possessed enemy reports no applied damage"), OutlierDamage::Apply(FirstEnemy, Hit), 0.0f);
	TestEqual(TEXT("Possessed enemy keeps health"), FirstEnemy->GetCurrentHealth(), EnemyHealth);
	TestTrue(TEXT("Protected hit preserves weapon stun"), FirstEnemy->GetOutlierAbilitySystemComponent()->HasMatchingGameplayTag(OutlierGameplayTags::State::Stunned()));
	PartnerController->Possess(SecondEnemy);
	TestFalse(TEXT("Old possession immediately loses convenience protection"), OutlierDamage::IsDeveloperInvincible(FirstEnemy));
	TestTrue(TEXT("New possession immediately gains protection"), OutlierDamage::IsDeveloperInvincible(SecondEnemy));
	UOutlierAbilitySystemComponent* SecondEnemyASC = SecondEnemy->GetOutlierAbilitySystemComponent();
	const FActiveGameplayEffectHandle EnemyImmunity = SecondEnemyASC->ApplyDamageImmuneStateToSelf();
	OutlierDamage::Apply(SecondEnemy, Hit);
	TestFalse(TEXT("Convenience protection does not bypass another immunity's stun rejection"),
		SecondEnemyASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stunned()));
	PartnerController->UnPossess();
	TestFalse(TEXT("Released enemy keeps no convenience immunity"), OutlierDamage::IsDeveloperInvincible(SecondEnemy));
	TestNotNull(TEXT("Releasing possession preserves unrelated enemy immunity"), SecondEnemyASC->GetActiveGameplayEffect(EnemyImmunity));
	PartnerController->Possess(Partner);

	Mode->InvalidateDeveloperConvenienceSession();
	TestTrue(TEXT("Closing UI preserves protection"), OutlierDamage::IsDeveloperInvincible(Shooter));
	const FActiveGameplayEffectHandle OtherImmunity = ShooterASC->ApplyDamageImmuneStateToSelf();
	Mode->DeveloperConvenienceState.Options.bInvincible = false;
	Mode->PublishDeveloperConvenienceState();
	TestFalse(TEXT("Turning OFF removes only convenience protection"), OutlierDamage::IsDeveloperInvincible(Shooter));
	TestNotNull(TEXT("Other immunity effect survives OFF"), ShooterASC->GetActiveGameplayEffect(OtherImmunity));
	TestFalse(TEXT("Other immunity still blocks damage"), ShooterASC->ApplyDamageToSelf(10.0f, nullptr, nullptr, Hit.DamageTag));
	ShooterASC->RemoveActiveEffectFromSelf(OtherImmunity);
	Hit.StunDurationSeconds = 0.0f;
	TestEqual(TEXT("OFF restores ordinary damage"), OutlierDamage::Apply(Shooter, Hit), 15.0f);
	TestEqual(TEXT("Ordinary damage consumes additional shield first"), Shooter->GetCurPartnerShield(), 15.0f);

	Mode->DeveloperConvenienceState.Options.bInvincible = true;
	PartnerState->SetPairId(99);
	TestFalse(TEXT("Foreign pair cannot inherit protection"), OutlierDamage::IsDeveloperInvincible(Partner));
	PartnerState->SetPairId(7);
	TestTrue(TEXT("Valid pair protection is restored"), OutlierDamage::IsDeveloperInvincible(Partner));
	auto* NewShooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	if (!TestNotNull(TEXT("Replacement Shooter"), NewShooter))
	{
		return false;
	}
	ShooterController->Possess(NewShooter);
	for (auto* State : { ShooterState, PartnerState })
	{
		State->SetShooterCharacter(NewShooter);
	}
	NewShooter->GetOutlierAbilitySystemComponent()->InitializeForPawn(NewShooter);
	TestTrue(TEXT("Replacement Pawn immediately inherits the retained option"), OutlierDamage::IsDeveloperInvincible(NewShooter));
	TestFalse(TEXT("Retired Pawn cannot inherit protection"), OutlierDamage::IsDeveloperInvincible(Shooter));
	TestFalse(TEXT("Replacement ASC also rejects direct damage"),
		NewShooter->GetOutlierAbilitySystemComponent()->ApplyDamageToSelf(15.0f, nullptr, nullptr, Hit.DamageTag));
	Mode->ResetDeveloperConvenienceState();
	TestFalse(TEXT("Match reset removes convenience protection"), OutlierDamage::IsDeveloperInvincible(NewShooter));
	return true;
}

#endif
