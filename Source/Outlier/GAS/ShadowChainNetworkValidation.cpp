// Opt-in, transient validation of real owner-only replication. Never saves assets.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "OutlierPlayerState.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace ShadowChainNetworkValidation
{
struct FProbe
{
	FString Output;
	double Started = 0.0;
	double FixtureStarted = 0.0;
	double LastSample = 0.0;
	double ClientReadyStarted = 0.0;
	int32 Stage = 0;
	TWeakObjectPtr<AShooterCharacter> Shooter;
	void Preload(UWorld* World, const UWorld::InitializationValues)
	{
		if (!World || !World->IsGameWorld()) return;
		// Load before the first network tick, so synchronous fixture package loads
		// do not contaminate the GameState clock-offset samples after connection.
		LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	}
	bool Setup(UWorld* World)
	{
		APlayerController* Owner = nullptr;
		int32 Connections = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (It->Get() && It->Get()->GetNetConnection()) { ++Connections; if (!Owner) Owner = It->Get(); }
		}
		if (Connections < 2) return false;
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
		if (!ShooterClass || !PartnerClass) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
		APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(PartnerClass, FVector(0, 100, 200), FRotator::ZeroRotator, Spawn);
		if (!Shooter.IsValid() || !Partner) return false;
		Shooter->bAlwaysRelevant = true;
		Partner->bAlwaysRelevant = true;
		Shooter->GetCharacterMovement()->DisableMovement();
		Partner->GetCharacterMovement()->DisableMovement();
		Shooter->SetPartnerCharacter(Partner);
		Partner->SetShooterCharacter(Shooter.Get());
		Owner->Possess(Shooter.Get());
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		Shooter->SetPlayerState(State);
		State->SetAcquiredSuit(true);
		UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
		ASC->AddLooseGameplayTag(OutlierGameplayTags::Ability::Shooter::Stealth());
		FOutlierShooterSuitConfig Config = ASC->GetShooterSuitConfig();
		Config.Stealth.ShadowChainRequiredKills = 5;
		ASC->UpdateShooterSuitConfig(Config);
		ASC->AddLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowChain());
		return true;
	}
	void Tick(UWorld* World, ELevelTick, float)
	{
		if (!World || !World->IsGameWorld() || !World->HasBegunPlay() || World->GetNetMode() == NM_Standalone) return;
		const double Now = FPlatformTime::Seconds();
		if (Started == 0.0) Started = Now;
		if (Now - Started > 65.0) { FPlatformMisc::RequestExit(false); return; }
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			if (!Shooter.IsValid() && !Setup(World)) return;
			// Character/animation packages can block a joining client longer than the
			// 1.5-second effect itself. Start scenarios only after both clients observed
			// their replicated Pawn for a few seconds. Files are fixture readiness only;
			// every gameplay snapshot under test still travels through Unreal replication.
			if (FixtureStarted == 0.0)
			{
				const FString Root = FPaths::GetPath(Output);
				if (!IFileManager::Get().FileExists(*(Root / TEXT("Client1/Ready.txt")))
					|| !IFileManager::Get().FileExists(*(Root / TEXT("Client2/Ready.txt")))) return;
				FixtureStarted = Now;
			}
			const double Age = Now - FixtureStarted;
			UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
			int32 Kills = 0;
			if (Stage == 0 && Age > 2.0) { Kills = 2; ++Stage; }
			else if (Stage == 1 && Age > 4.0) { Kills = 3; ++Stage; }
			else if (Stage == 2 && Age > 4.6) { Kills = 5; ++Stage; }
			else if (Stage == 3 && Age > 5.0) { Kills = 2; ++Stage; }
			for (int32 Index = 0; Index < Kills; ++Index) ASC->NotifyShooterStealthKill(nullptr);
			if (Age > 10.0) { FPlatformMisc::RequestExit(false); return; }
		}
		if (Now - LastSample < 0.05) return;
		LastSample = Now;
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			const UOutlierAbilitySystemComponent* ASC = It->GetOutlierAbilitySystemComponent();
			const auto& State = ASC->GetShadowChainPresentationState();
			if (World->GetNetMode() == NM_Client && (!It->IsLocallyControlled() || State.bInitialized))
			{
				if (ClientReadyStarted == 0.0) ClientReadyStarted = Now;
				if (Now - ClientReadyStarted > 3.0)
				{
					const FString Ready = Output / TEXT("Ready.txt");
					if (!IFileManager::Get().FileExists(*Ready)) FFileHelper::SaveStringToFile(TEXT("Ready"), *Ready);
				}
			}
			const FString Row = FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n"),
				int32(World->GetNetMode()), It->IsLocallyControlled() ? 1 : 0, State.bInitialized ? 1 : 0,
				State.bUnlocked ? 1 : 0, State.CurrentStacks, State.RequiredKills, State.bPassiveActive ? 1 : 0,
				State.ActivationSerial, ASC->GetShadowChainStealthRemaining(), State.StartServerTime, State.DurationSeconds,
				World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : -1.0,
				Now, World->GetTimeSeconds());
			FFileHelper::SaveStringToFile(Row, *(Output / TEXT("Samples.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), FILEWRITE_Append);
		}
	}
};
FProbe Probe;
struct FRegister
{
	FRegister()
	{
		if (!FParse::Value(FCommandLine::Get(), TEXT("OutlierShadowChainOutput="), Probe.Output)) return;
		IFileManager::Get().MakeDirectory(*Probe.Output, true);
		FWorldDelegates::OnPostWorldInitialization.AddRaw(&Probe, &FProbe::Preload);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::Tick);
	}
} Register;
}
#endif
