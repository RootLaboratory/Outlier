#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyTargetRules.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "OutlierPlayerState.h"
#include "Camera/CameraActor.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

// Opt-in inspection of real map content. Never changes or saves map/material assets.
namespace StealthMapValidation
{
struct FProbe
{
	FString Output;
	double Started = -1.0, StageStarted = -1.0;
	int32 Stage = 0;
	bool bFinished = false;
	TWeakObjectPtr<AShooterCharacter> Shooter;
	TWeakObjectPtr<APartnerCharacter> Partner;
	TWeakObjectPtr<AEnemyBase> Enemy;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<UMaterialInterface> OriginalMaterial;
	bool bSeenBefore = false, bHiddenDuring = false, bMaterialApplied = false, bRestored = false, bSeenAfter = false;

	void Note(const FString& Text)
	{
		UE_LOG(LogTemp, Display, TEXT("[StealthMapValidation] %s"), *Text);
		FFileHelper::SaveStringToFile(Text + TEXT("\n"), *(Output / TEXT("Observations.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
	void Capture(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(Output / (FString(Name) + TEXT(".png")), false, false);
	}
	void Finish(const FString& Reason)
	{
		if (bFinished) return;
		bFinished = true;
		const bool bPassed = bSeenBefore && bHiddenDuring && bMaterialApplied && bRestored && bSeenAfter;
		Note(FString::Printf(TEXT("RESULT Passed=%d BeforeSight=%d HiddenDuring=%d MaterialApplied=%d MaterialRestored=%d Reacquired=%d Reason=%s"),
			bPassed, bSeenBefore, bHiddenDuring, bMaterialApplied, bRestored, bSeenAfter, *Reason));
		FFileHelper::SaveStringToFile(bPassed ? TEXT("PASS") : TEXT("FAIL"), *(Output / TEXT("Result.txt")));
	}
	bool Setup(UWorld* World)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !PC->IsLocalController()) return false;
		AEnemyBase* Selected = nullptr;
		for (TActorIterator<AEnemyBase> It(World); It; ++It)
		{
			if (It->GetPathName().Contains(TEXT("Stage01")) && It->GetController() && !It->IsDead()
				&& It->CanUseEnemyPerception()) { Selected = *It; break; }
		}
		if (!Selected) return false;
		Enemy = Selected;
		Note(TEXT("Map=") + World->GetMapName() + TEXT(" PlacedEnemy=") + Selected->GetPathName());
		for (TActorIterator<AOutlierPostProcessVolume> It(World); It; ++It)
		{
			Note(FString::Printf(TEXT("Volume=%s FirstPerson=%s ThirdPerson=%s"), *It->GetPathName(),
				*GetPathNameSafe(It->GetFirstPersonStealthGlassMaterial()), *GetPathNameSafe(It->GetThirdPersonStealthMaterial())));
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr, TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
		if (!ShooterClass || !PartnerClass) { Finish(TEXT("Character class missing")); return false; }
		const FVector Origin = Selected->GetActorLocation();
		const FVector Forward = Selected->GetActorForwardVector().GetSafeNormal2D();
		const FVector Position = Origin + Forward * 450.0f;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Position, (-Forward).Rotation(), Spawn);
		Partner = World->SpawnActor<APartnerCharacter>(PartnerClass, Position + FVector(0, 180, 80), FRotator::ZeroRotator, Spawn);
		if (!Shooter.IsValid() || !Partner.IsValid()) { Finish(TEXT("Character spawn failed")); return false; }
		Shooter->SetPartnerCharacter(Partner.Get());
		Partner->SetShooterCharacter(Shooter.Get());
		AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
		PC->Possess(Shooter.Get());
		Shooter->SetPlayerState(State);
		State->SetAcquiredSuit(true);
		Shooter->SetCanBeDamaged(false);
		Partner->SetCanBeDamaged(false);
		Shooter->GetCharacterMovement()->DisableMovement();
		Selected->GetCharacterMovement()->DisableMovement();
		UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
		ASC->AddLooseGameplayTag(OutlierGameplayTags::Ability::Shooter::Stealth());
		OriginalMaterial = Shooter->GetMesh()->GetMaterial(0);
		Camera = World->SpawnActor<ACameraActor>();
		// Observe along the proven enemy-to-player sight line rather than from
		// behind nearby server cabinets in the authored room.
		const FVector Eye = Position - Forward * 220.0f + FVector(0, 0, 65);
		Camera->SetActorLocationAndRotation(Eye, (Position + FVector(0, 0, 20) - Eye).Rotation());
		PC->SetViewTarget(Camera.Get());
		Controller = PC;
		Note(FString::Printf(TEXT("Shooter=%s EnemyLocation=%s ShooterLocation=%s SuitUsable=%d Configured=%d"),
			*Shooter->GetPathName(), *Origin.ToCompactString(), *Position.ToCompactString(), Shooter->IsSuitUsable(), ASC->IsShooterSuitConfigured()));
		return true;
	}
	void Tick(UWorld* World, ELevelTick, float)
	{
		if (bFinished || !World || !World->IsGameWorld() || World->GetNetMode() != NM_Standalone || !World->HasBegunPlay()) return;
		const double Now = FPlatformTime::Seconds();
		if (Started < 0.0) Started = Now;
		if (Now - Started > 90.0) { Finish(TEXT("Timed out waiting for actual map/AI")); return; }
		if (!Shooter.IsValid())
		{
			if (Now - Started > 3.0 && Setup(World)) StageStarted = Now;
			return;
		}
		AEnemyAIController* AI = Enemy.IsValid() ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
		if (!AI) { Finish(TEXT("Placed enemy controller unavailable")); return; }
		UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
		const double Age = Now - StageStarted;
		// Map cinematics may restore the pawn view after possession. Keep the
		// comparison camera fixed while observing third-person material changes.
		if (Controller.IsValid() && Camera.IsValid() && Stage != 4 && Stage != 5)
		{
			Controller->SetViewTarget(Camera.Get());
		}
		if (Stage == 0 && AI->IsTargetDirectlyVisible(Shooter.Get()))
		{
			bSeenBefore = true;
			Note(TEXT("Before: actual Sight detects Shooter"));
			Capture(TEXT("01_Before"));
			Stage = 1; StageStarted = Now;
		}
		else if (Stage == 1 && Age > 1.0)
		{
			const bool bActivated = ASC->TryActivateShooterSuitAbility(OutlierGameplayTags::Ability::Shooter::Stealth());
			Note(FString::Printf(TEXT("ManualActivated=%d"), bActivated));
			if (!bActivated) { Finish(TEXT("Actual ability gate rejected")); return; }
			Stage = 2; StageStarted = Now;
		}
		else if (Stage == 2 && Age > 0.6)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Shooter->GetMesh()->GetMaterial(0));
			bMaterialApplied = MID && MID != OriginalMaterial.Get() && MID->Parent && MID->Parent->GetName().Contains(TEXT("Stealth"));
			bHiddenDuring = Shooter->IsStealthed() && Partner->GetOutlierAbilitySystemComponent()->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()) && !AI->IsTargetDirectlyVisible(Shooter.Get())
				&& AI->GetCachedVisibleTarget() != Shooter.Get() && !Enemy->CanAttackTrackedTarget(Shooter.Get());
			Note(FString::Printf(TEXT("During: PairStealth=%d/%d DirectVisible=%d CachedShooter=%d CanAttack=%d Material=%s Parent=%s Fade=%.3f Unavailable=%d"),
				Shooter->IsStealthed(), Partner->GetOutlierAbilitySystemComponent()->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()), AI->IsTargetDirectlyVisible(Shooter.Get()), AI->GetCachedVisibleTarget() == Shooter.Get(),
				Enemy->CanAttackTrackedTarget(Shooter.Get()), *GetPathNameSafe(MID), *GetPathNameSafe(MID ? MID->Parent.Get() : nullptr),
				MID ? MID->K2_GetScalarParameterValue(TEXT("Fade")) : -1.0f, OutlierEnemyTargetRules::IsUnavailable(Shooter.Get())));
			Capture(TEXT("02_ManualStealth"));
			Stage = 3; StageStarted = Now;
		}
		else if (Stage == 3 && Age > 0.6)
		{
			Controller->SetViewTarget(Shooter.Get());
			Controller->SetControlRotation((-Enemy->GetActorForwardVector()).Rotation());
			Stage = 4; StageStarted = Now;
		}
		else if (Stage == 4 && Age > 0.5)
		{
			Capture(TEXT("03_FirstPersonStealth"));
			Stage = 5; StageStarted = Now;
		}
		else if (Stage == 5 && Age > 0.5)
		{
			Controller->SetViewTarget(Camera.Get());
			ASC->EndActiveShooterStealth(true);
			Stage = 6; StageStarted = Now;
		}
		else if (Stage == 6 && Age > 0.6)
		{
			bRestored = Shooter->GetMesh()->GetMaterial(0) == OriginalMaterial.Get();
			if (AI->IsTargetDirectlyVisible(Shooter.Get()))
			{
				bSeenAfter = true;
				Note(FString::Printf(TEXT("After: DirectVisible=1 MaterialRestored=%d"), bRestored));
				Capture(TEXT("04_After"));
				Stage = 7; StageStarted = Now;
			}
		}
		else if (Stage == 7 && Age > 1.0)
		{
			ASC->AddLooseGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowChain());
			for (int32 Index = 0; Index < ASC->GetShooterSuitConfig().Stealth.ShadowChainRequiredKills; ++Index) ASC->NotifyShooterStealthKill(nullptr);
			Stage = 8; StageStarted = Now;
		}
		else if (Stage == 8 && Age > 0.5)
		{
			const bool bPassive = ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::ShooterPassiveStealth());
			Note(FString::Printf(TEXT("Passive: Active=%d DirectVisible=%d MID=%d"), bPassive, AI->IsTargetDirectlyVisible(Shooter.Get()),
				Cast<UMaterialInstanceDynamic>(Shooter->GetMesh()->GetMaterial(0)) != nullptr));
			bHiddenDuring &= bPassive && !AI->IsTargetDirectlyVisible(Shooter.Get());
			Capture(TEXT("05_PassiveStealth"));
			Stage = 9; StageStarted = Now;
		}
		else if (Stage == 9 && Age > 2.0) Finish(TEXT("Completed actual-map manual/passive inspection"));
	}
};
FProbe Probe;
struct FRegister
{
	FRegister()
	{
		if (!FParse::Value(FCommandLine::Get(), TEXT("OutlierStealthMapOutput="), Probe.Output)) return;
		IFileManager::Get().MakeDirectory(*Probe.Output, true);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::Tick);
	}
} Register;
}
#endif
