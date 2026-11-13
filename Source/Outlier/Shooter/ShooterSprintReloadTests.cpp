#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Shooter/Anim/AnimNotify_TaggedAudio.h"
#include "Weapon/RangedWeaponBase.h"
#include "Animation/AnimMontage.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSprintReloadTest,
	"Outlier.Shooter.SprintReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSprintReloadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	if (!ShooterClass || !RifleClass || !PistolClass) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SprintReload")), GetTransientPackage());
	if (!TestNotNull(TEXT("Reload test world"), World)) return false;
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	// Actor RPC의 로컬 실행에는 월드의 액터 초기화가 필요하다.
	// BeginPlay까지 강제해 무관한 GAS/게임 시작 타이머를 실행하지 않는다.
	World->InitializeActorsForPlay(FURL());
	APlayerController* Controller = nullptr;
	ON_SCOPE_EXIT
	{
		if (Controller) Controller->Player = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Spawn);
	Controller = World->SpawnActor<APlayerController>();
	if (!Shooter || !Controller) return false;
	Controller->Possess(Shooter);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->SetAsLocalPlayerController();
	auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
	auto* Input = Shooter->FindComponentByClass<UShooterMovementComponent>();
	auto* Combat = Shooter->FindComponentByClass<UShooterCombatComponent>();
	if (!Input || !Combat) return false;
	const FFloatProperty* SprintSpeedProperty = FindFProperty<FFloatProperty>(AShooterCharacter::StaticClass(), TEXT("SprintSpeed"));
	if (!TestNotNull(TEXT("Configured Sprint speed"), SprintSpeedProperty)) return false;
	const float SprintSpeed = SprintSpeedProperty->GetPropertyValue_InContainer(Shooter);

	for (UClass* WeaponClass : {RifleClass, PistolClass})
	{
		for (bool bSuit : {false, true})
		{
			// PreSuit에는 Rifle 연출이 없다. 실제 지원하는 무기/슈트 조합만 검사한다.
			if (!bSuit && WeaponClass == RifleClass) continue;
			if (!TestTrue(TEXT("Actual presentation applies"), Shooter->SetSuitPresentation(bSuit))) return false;
			ARangedWeaponBase* Weapon = World->SpawnActor<ARangedWeaponBase>(WeaponClass, Spawn);
			Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
			Shooter->RefreshWeaponMode();
			Shooter->CancelLocalProceduralWeaponSwitch();
			Shooter->UpdateLocalProceduralWeaponSwitch(1.0f);
			const FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
			if (!InfiniteAmmo) return false;
			InfiniteAmmo->SetPropertyValue_InContainer(Weapon, false);
			// 1발 권총은 1발이면 이미 가득 찬 상태다. 저장된 탄창 크기로 장전 조건을 만든다.
			const int32 ReloadStartAmmo = FMath::Max(0, Weapon->GetMagazineSize() - 1);
			UAnimInstance* TP = Shooter->GetMesh()->GetAnimInstance();
			auto* FP = Cast<UShooterFirstPersonAnimInstance>(Shooter->GetFirstPersonMesh()->GetAnimInstance());
			UAnimMontage* Montage = Shooter->GetActionMontage(EShooterMontageAction::Reload, false);
			if (!TestNotNull(TEXT("Actual 3P AnimBP"), TP) || !TestNotNull(TEXT("Actual 1P AnimBP"), FP)
				|| !TestNotNull(TEXT("Actual authoritative reload montage"), Montage)) return false;

			// 이동/탄약 검증에서는 오디오 Notify만 임시 비활성화한다. 실제 Reload의
			// 슬롯/길이/Commit Notify는 유지하며, 공유 에셋의 값은 이 조합이 끝날 때 복원한다.
			TMap<UAnimNotify_TaggedAudio*, FGameplayTag> SavedAudioTypes;
			ON_SCOPE_EXIT { for (const auto& Entry : SavedAudioTypes) Entry.Key->AudioTypeTag = Entry.Value; };
			TArray<UAnimationAsset*> ReloadAssets;
			for (UAnimMontage* Reload : {Montage, Shooter->GetFirstPersonReloadMontage()})
			{
				if (!Reload) continue;
				ReloadAssets.AddUnique(Reload);
				Reload->GetAllAnimationSequencesReferred(ReloadAssets);
			}
			for (UAnimationAsset* Asset : ReloadAssets)
			{
				if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
					for (const FAnimNotifyEvent& Event : Sequence->Notifies)
						if (auto* Audio = Cast<UAnimNotify_TaggedAudio>(Event.Notify); Audio && !SavedAudioTypes.Contains(Audio))
						{
							SavedAudioTypes.Add(Audio, Audio->AudioTypeTag);
							Audio->AudioTypeTag = FGameplayTag();
						}
			}

			// 실제 저장된 AnimBP의 슬롯/상체 필터를 기록한다. 임시 몽타주로 에셋 누락을 덮지 않는다.
			TSet<FName> Slots;
			for (TFieldIterator<FStructProperty> Property(TP->GetClass()); Property; ++Property)
			{
				if (Property->Struct == FAnimNode_Slot::StaticStruct())
				{
					const auto* Slot = Property->ContainerPtrToValuePtr<FAnimNode_Slot>(TP);
					Slots.Add(Slot->SlotName);
					AddInfo(FString::Printf(TEXT("%s slot=%s"), *TP->GetClass()->GetName(), *Slot->SlotName.ToString()));
				}
				if (Property->Struct == FAnimNode_LayeredBoneBlend::StaticStruct())
				{
					const auto* Layer = Property->ContainerPtrToValuePtr<FAnimNode_LayeredBoneBlend>(TP);
					for (const auto& Setup : Layer->LayerSetup)
						for (const auto& Filter : Setup.BranchFilters)
							AddInfo(FString::Printf(TEXT("Upper-body filter=%s depth=%d"), *Filter.BoneName.ToString(), Filter.BlendDepth));
				}
			}
			for (const auto& Track : Montage->SlotAnimTracks)
				TestTrue(TEXT("Reload montage uses an actual AnimBP slot"), Slots.Contains(Track.SlotName));

			for (EOutlierSprintInputMode Mode : {EOutlierSprintInputMode::Toggle, EOutlierSprintInputMode::Hold})
			{
				for (bool bSprintFirst : {false, true})
				{
					for (bool bStopDuringReload : {false, true})
					{
						Combat->CancelReloadInternal();
						Combat->ClearInputIntent();
						Movement->ResetSlideForLifecycle();
						Movement->SetSprintInputMode(Mode);
						Movement->SetMovementMode(MOVE_Walking);
						Movement->Velocity = FVector(150.0f, 0.0f, 0.0f);
						Weapon->RestoreCheckpointAmmo(ReloadStartAmmo);
						const auto Press = [&]() { Movement->NotifyFreshSprintPress(); Input->HandleSprintPressed(); Movement->ApplyMovementInput(); };
						const auto Release = [&]() { Input->HandleSprintReleased(); Movement->ApplyMovementInput(); };
						if (bSprintFirst) Press();
						Combat->TryReload();
						const float ReloadPosition = TP->Montage_GetPosition(Montage) + (bSprintFirst ? 0.0f : 0.125f);
						TP->Montage_SetPosition(Montage, ReloadPosition);
						if (!bSprintFirst) Press();
						if (!TestTrue(TEXT("Both orders start an actual weapon reload"), Combat->IsReloading() && Weapon->IsReloading())) return false;
						TestTrue(TEXT("Both orders retain actual Run"), Input->IsSprinting());
						TestEqual(TEXT("Run keeps Sprint speed cap"), Movement->MaxWalkSpeed, SprintSpeed);
						TestTrue(TEXT("Sprint does not interrupt authoritative reload montage"), TP->Montage_IsPlaying(Montage));
						TestEqual(TEXT("Sprint does not advance/restart montage time"), TP->Montage_GetPosition(Montage), ReloadPosition);
						TestFalse(TEXT("Reload blocks Fire"), Combat->CanFireInCurrentState());
						TestFalse(TEXT("Reload blocks ADS"), Combat->CanAimInCurrentState());
						TestFalse(TEXT("Run plus Reload cannot enter Slide"), Input->CanStartSlide());
						if (bStopDuringReload)
						{
							Release();
							if (Mode == EOutlierSprintInputMode::Toggle) { Press(); Release(); }
							TestFalse(TEXT("Explicit OFF stops Sprint only"), Input->IsSprinting());
							TestTrue(TEXT("Sprint OFF preserves Reload"), Combat->IsReloading());
						}
						Combat->HandleReloadCommitNotify();
						TestEqual(TEXT("Commit refills ammunition"), Weapon->GetCurrentAmmo(), Weapon->GetMagazineSize());
						TestFalse(TEXT("Weapon reload ends at Commit"), Weapon->IsReloading());
						TestTrue(TEXT("Character reload outlives Commit"), Combat->IsReloading());
						TestEqual(TEXT("Commit retains Reload lock"), Shooter->GetActionLock(), EShooterActionLock::Reload);
						Input->RefreshMovementState();
						TestEqual(TEXT("Commit respects remaining Sprint intent"), Input->IsSprinting(), !bStopDuringReload);
						TestFalse(TEXT("Commit cannot unlock Fire early"), Combat->CanFireInCurrentState());
						TestFalse(TEXT("Commit cannot unlock ADS early"), Combat->CanAimInCurrentState());
						Combat->HandleReloadCommitNotify();
						TestEqual(TEXT("Repeated Commit cannot add ammunition twice"), Weapon->GetCurrentAmmo(), Weapon->GetMagazineSize());
						Combat->HandleReloadMontageEnded(Montage, false);
						TestFalse(TEXT("Montage completion unlocks Reload"), Combat->IsReloading());
						TestEqual(TEXT("Completion does not revive Sprint OFF"), Input->IsSprinting(), !bStopDuringReload);
						TestFalse(TEXT("Completion cannot revive rejected attack"), Combat->WantsToFire() || Combat->bPendingSprintExitFire);
						Combat->CancelReloadInternal();
						TP->Montage_Stop(0.0f);
						FP->Montage_Stop(0.0f);
					}
				}
			}

			for (EOutlierSprintInputMode Mode : {EOutlierSprintInputMode::Toggle, EOutlierSprintInputMode::Hold})
			{
				Combat->ClearInputIntent();
				Movement->ResetSlideForLifecycle();
				Movement->SetSprintInputMode(Mode);
				Movement->SetMovementMode(MOVE_Walking);
				Movement->Velocity = FVector(150.0f, 0.0f, 0.0f);
				Weapon->RestoreCheckpointAmmo(ReloadStartAmmo);
				Combat->TryReload();
				Combat->HandleAimPressed();
				Movement->ApplyMovementInput();
				TestTrue(TEXT("Reload stores deferred Aim"), Combat->WantsToAim());
				TestFalse(TEXT("Reload cannot enter actual ADS"), Combat->IsAiming());
				for (int32 Cycle = 0; Cycle < 2; ++Cycle)
				{
					Movement->NotifyFreshSprintPress();
					Input->HandleSprintPressed();
					Movement->ApplyMovementInput();
					TestTrue(TEXT("Fresh Sprint ON overrides deferred Aim during Reload"), Input->IsSprinting());
					TestFalse(TEXT("Fresh Sprint ON clears older Aim"), Combat->WantsToAim());
					Input->HandleSprintReleased();
					Movement->ApplyMovementInput();
					if (Mode == EOutlierSprintInputMode::Toggle)
					{
						Input->HandleSprintPressed();
						Input->HandleSprintReleased();
						Movement->ApplyMovementInput();
					}
					TestFalse(TEXT("Repeated OFF leaves Sprint off"), Input->IsSprinting());
					TestTrue(TEXT("Repeated Sprint edges preserve Reload"), Combat->IsReloading() && TP->Montage_IsPlaying(Montage));
				}
				Combat->HandleAimPressed();
				Movement->ApplyMovementInput();
				Combat->HandleReloadCommitNotify();
				Combat->HandleReloadMontageEnded(Montage, false);
				TestTrue(TEXT("Newest deferred Aim restores after Reload"), Combat->IsAiming());
				TestFalse(TEXT("Newest Aim never coexists with actual Sprint"), Input->IsSprinting());
				Combat->ClearInputIntent();
				TP->Montage_Stop(0.0f);
				FP->Montage_Stop(0.0f);
			}

			// 실제 몽타주를 끝까지 진행해 작성된 Notify와 종료 delegate도 검증한다.
			for (int32 FPS : {30, 60, 120})
			{
				// 앞의 수동 경계 테스트에서 남은 중단 이벤트는 새 Reload를 묶기 전에 소진한다.
				Combat->CancelReloadInternal();
				++GFrameCounter;
				for (USkeletalMeshComponent* Mesh : {Shooter->GetFirstPersonMesh(), Shooter->GetMesh()})
				{
					Mesh->GetAnimInstance()->Montage_Stop(0.0f);
					Mesh->TickAnimation(1.0f / FPS, false);
					Mesh->RefreshBoneTransforms();
					Mesh->ConditionallyDispatchQueuedAnimEvents();
				}
				Combat->ClearInputIntent();
				Movement->ResetSlideForLifecycle();
				Movement->SetSprintInputMode(EOutlierSprintInputMode::Toggle);
				Movement->SetMovementMode(MOVE_Walking);
				Movement->Velocity = FVector(150.0f, 0.0f, 0.0f);
				Input->SetSprintIntent(true);
				Weapon->RestoreCheckpointAmmo(ReloadStartAmmo);
				Combat->TryReload();
				if (!TestTrue(TEXT("Frame-rate fixture starts Reload"), Combat->IsReloading())) return false;
				int32 AmmoChanges = 0;
				int32 PreviousAmmo = Weapon->GetCurrentAmmo();
				for (int32 Frame = 0; Frame < FPS * 8 && Combat->IsReloading(); ++Frame)
				{
					++GFrameCounter;
					const float Delta = 1.0f / FPS;
					for (USkeletalMeshComponent* Mesh : {Shooter->GetFirstPersonMesh(), Shooter->GetMesh()})
					{
						Mesh->TickAnimation(Delta, false);
						Mesh->RefreshBoneTransforms();
						Mesh->ConditionallyDispatchQueuedAnimEvents();
					}
					Input->RefreshMovementState();
					TestTrue(TEXT("Authored Reload keeps Run at each tested frame rate"), Input->IsSprinting());
					if (PreviousAmmo != Weapon->GetCurrentAmmo()) ++AmmoChanges;
					PreviousAmmo = Weapon->GetCurrentAmmo();
				}
				TestFalse(TEXT("Authored montage finishes Reload"), Combat->IsReloading());
				TestEqual(TEXT("Authored Reload changes ammo exactly once"), AmmoChanges, 1);
				TestEqual(TEXT("Authored Notify/finish refills the magazine"), Weapon->GetCurrentAmmo(), Weapon->GetMagazineSize());
				AddInfo(FString::Printf(TEXT("Authored Reload timeline: %s Suit=%d FPS=%d"), *WeaponClass->GetName(), bSuit, FPS));
			}

			// Gameplay SprintAlpha는 보존하고 Reload 포즈/IK의 긴 쪽이 Sprint 연출을 억제한다.
			FP->RefreshPresentationState();
			FP->ViewModelSprintAlpha = 1.0f;
			FP->ViewModelWeaponPoseAlpha = 1.0f;
			FP->ViewModelAimAlpha = 0.0f;
			FP->bEnableProceduralSprint = true;
			for (const FVector2D Blend : {FVector2D(1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.25, 0.5), FVector2D(0.0, 0.0)})
			{
				FP->ViewModelReloadPoseAlpha = Blend.X;
				FP->ViewModelReloadIKBlendAlpha = Blend.Y;
				FP->UpdateFirstPersonProceduralRuntime(1.0f / 60.0f);
				TestEqual(TEXT("Reload suppresses Sprint through both pose and IK return"), FP->GetReloadSprintPoseScale(), 1.0f - float(FMath::Max(Blend.X, Blend.Y)));
				TestEqual(TEXT("Presentation gate preserves raw gameplay Sprint alpha"), FP->GetViewModelSprintAlpha(), 1.0f);
				if (FMath::Max(Blend.X, Blend.Y) == 1.0)
					TestEqual(TEXT("Full Reload suppresses runtime Sprint pose"), FP->ViewModelProceduralRuntime.SprintAlpha, 0.0f);
				else
					TestTrue(TEXT("Sprint pose returns as Reload pose and IK release"), FP->ViewModelProceduralRuntime.SprintAlpha > 0.0f);
			}

			Combat->ClearInputIntent();
			Movement->ResetSlideForLifecycle();
			Weapon->RestoreCheckpointAmmo(ReloadStartAmmo);
			Combat->TryReload();
			Combat->TryStartAttack();
			TestFalse(TEXT("Reload attack does not create deferred first shot"), Combat->bPendingSprintExitFire);
			TestTrue(TEXT("Attack cannot cancel Reload"), Combat->IsReloading());
			Combat->TryStopAttack();
			Combat->HandleReloadMontageEnded(Montage, true);
			TestFalse(TEXT("Interrupted montage cancels Reload"), Combat->IsReloading() || Weapon->IsReloading());
			TestEqual(TEXT("Interruption before Commit does not grant ammunition"), Weapon->GetCurrentAmmo(), ReloadStartAmmo);
			TestEqual(TEXT("Interruption releases Reload lock"), Shooter->GetActionLock(), EShooterActionLock::None);
			// 발사 대기와 수동 장전을 함께 수락하려면 탄약이 남은 미완충 탄창이 필요하다.
			// 1발 권총에는 없는 조합이므로 이 경계는 실제 라이플로 검증한다.
			if (ReloadStartAmmo > 0)
			{
				FP->ViewModelSprintAlpha = 1.0f;
				Weapon->RestoreCheckpointAmmo(ReloadStartAmmo);
				Combat->TryStartAttack();
				TestTrue(TEXT("Sprint-exit first shot is pending before Reload"), Combat->bPendingSprintExitFire);
				const uint32 OldRequest = Combat->PendingSprintExitRequest;
				const uint32 OldEpoch = Combat->PendingSprintExitEpoch;
				Combat->TryReload();
				TestFalse(TEXT("Reload cancels the earlier Sprint-exit first shot"), Combat->bPendingSprintExitFire);
				Combat->HandleReloadCommitNotify();
				Combat->HandleReloadMontageEnded(Montage, false);
				Combat->ResolveSprintExitFire(OldRequest, OldEpoch, true);
				TestEqual(TEXT("Late readiness cannot fire the cancelled request after Reload"), Weapon->GetCurrentAmmo(), Weapon->GetMagazineSize());
			}
			Weapon->Destroy();
		}
	}
	return true;
}

#endif
