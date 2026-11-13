#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/Anim/AnimNotify_MagazineToHand.h"
#include "Shooter/Anim/AnimNotify_MagazineToWeapon.h"
#include "Shooter/Anim/AnimNotify_ReloadCommit.h"
#include "Weapon/RangedWeaponBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponActionNotifyTest,
	"Outlier.Shooter.Equip.WeaponActionNotify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponActionNotifyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	if (!TestNotNull(TEXT("Production Shooter class"), ShooterClass)
		|| !TestNotNull(TEXT("Production pistol class"), PistolClass)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("WeaponActionNotify")), GetTransientPackage());
	if (!TestNotNull(TEXT("Notify lifecycle world"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
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
	ARangedWeaponBase* Pistol = World->SpawnActor<ARangedWeaponBase>(PistolClass, Spawn);
	Controller = World->SpawnActor<APlayerController>();
	if (!Shooter || !Pistol || !Controller) return false;
	Controller->Possess(Shooter);
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Controller->SetAsLocalPlayerController();
	Shooter->AFirstPersonCharacter::EquipWeapon(Pistol);
	Shooter->RefreshWeaponMode();

	const FBoolProperty* InfiniteAmmo = FindFProperty<FBoolProperty>(ARangedWeaponBase::StaticClass(), TEXT("bInfiniteAmmo"));
	const FObjectProperty* HandMagazineProperty = FindFProperty<FObjectProperty>(ARangedWeaponBase::StaticClass(), TEXT("FirstHandMagazineMesh"));
	const FObjectProperty* MagazineMeshProperty = FindFProperty<FObjectProperty>(ARangedWeaponBase::StaticClass(), TEXT("MagazineMesh"));
	if (!InfiniteAmmo || !HandMagazineProperty || !MagazineMeshProperty) return false;
	InfiniteAmmo->SetPropertyValue_InContainer(Pistol, false);
	UStaticMeshComponent* HandMagazine = Cast<UStaticMeshComponent>(HandMagazineProperty->GetObjectPropertyValue_InContainer(Pistol));
	if (!TestNotNull(TEXT("Pistol hand magazine component"), HandMagazine)) return false;
	// 메시 유무에 따른 숨김 정책과 Notify 수명 검증을 분리한다. 테스트 Actor의 탄창만 설정한다.
	if (!MagazineMeshProperty->GetObjectPropertyValue_InContainer(Pistol))
	{
		UStaticMesh* MagazineMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!TestNotNull(TEXT("Visible magazine fixture mesh"), MagazineMesh)) return false;
		MagazineMeshProperty->SetObjectPropertyValue_InContainer(Pistol, MagazineMesh);
	}
	Pistol->RestoreCheckpointAmmo(0);
	Shooter->TryReload();
	if (!TestTrue(TEXT("Actual Combat path begins first reload"), Shooter->IsReloading() && Pistol->IsReloading())) return false;
	USkeletalMeshComponent* FP = Shooter->GetFirstPersonMesh();
	USkeletalMeshComponent* TP = Shooter->GetMesh();
	UAnimMontage* FPMontage = Shooter->GetActionMontage(EShooterMontageAction::Reload, true);
	UAnimMontage* TPMontage = Shooter->GetActionMontage(EShooterMontageAction::Reload, false);
	UAnimInstance* FPAnim = FP ? FP->GetAnimInstance() : nullptr;
	UAnimInstance* TPAnim = TP ? TP->GetAnimInstance() : nullptr;
	FAnimMontageInstance* FirstFP = FPAnim ? FPAnim->GetActiveInstanceForMontage(FPMontage) : nullptr;
	FAnimMontageInstance* FirstTP = TPAnim ? TPAnim->GetActiveInstanceForMontage(TPMontage) : nullptr;
	if (!TestNotNull(TEXT("Real first FP reload instance"), FirstFP)
		|| !TestNotNull(TEXT("Real first TP reload instance"), FirstTP)) return false;
	const int32 FirstFPId = FirstFP->GetInstanceID();
	const int32 FirstTPId = FirstTP->GetInstanceID();
	FAnimNotifyEvent NotifyEvent;
	FAnimNotifyEventReference OldFP(&NotifyEvent, FPMontage);
	OldFP.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(FirstFPId);
	FAnimNotifyEventReference OldTP(&NotifyEvent, TPMontage);
	OldTP.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(FirstTPId);
	UAnimNotify_MagazineToHand* ToHand = NewObject<UAnimNotify_MagazineToHand>();
	UAnimNotify_MagazineToWeapon* ToWeapon = NewObject<UAnimNotify_MagazineToWeapon>();
	UAnimNotify_ReloadCommit* Commit = NewObject<UAnimNotify_ReloadCommit>();
	ToHand->Notify(FP, FPMontage, OldFP);
	TestFalse(TEXT("Current original Notify shows its magazine"), HandMagazine->bHiddenInGame);
	ToWeapon->Notify(FP, FPMontage, OldFP);
	TestTrue(TEXT("Current original Notify hides its magazine"), HandMagazine->bHiddenInGame);

	// 본래 취소/재시작 경로를 실행한 뒤 이미 큐에 있던 이전 인스턴스의 Notify만 지연 도착시킨다.
	// 같은 무기와 장착 세대에서도 재시작된 Reload의 탄약/탄창을 바꾸면 안 된다.
	Shooter->CancelReloadInternal();
	Shooter->TryReload();
	if (!TestTrue(TEXT("Actual Combat path restarts reload"), Shooter->IsReloading() && Pistol->IsReloading())) return false;
	FAnimMontageInstance* NextFP = FPAnim->GetActiveInstanceForMontage(FPMontage);
	FAnimMontageInstance* NextTP = TPAnim->GetActiveInstanceForMontage(TPMontage);
	if (!TestNotNull(TEXT("Real restarted FP reload instance"), NextFP)
		|| !TestNotNull(TEXT("Real restarted TP reload instance"), NextTP)) return false;
	TestTrue(TEXT("FP restart creates a new instance"), NextFP->GetInstanceID() != FirstFPId);
	TestTrue(TEXT("TP restart creates a new instance"), NextTP->GetInstanceID() != FirstTPId);
	ToHand->Notify(FP, FPMontage, OldFP);
	ToHand->Notify(TP, TPMontage, OldTP);
	Commit->Notify(FP, FPMontage, OldFP);
	Commit->Notify(TP, TPMontage, OldTP);
	TestTrue(TEXT("Old magazine Notifies preserve the new reload presentation"), HandMagazine->bHiddenInGame);
	TestEqual(TEXT("Old Commit Notifies cannot refill the new reload"), Pistol->GetCurrentAmmo(), 0);
	TestTrue(TEXT("Old Commit Notifies cannot finish the new reload"), Pistol->IsReloading());

	FAnimNotifyEventReference CurrentFP(&NotifyEvent, FPMontage);
	CurrentFP.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(NextFP->GetInstanceID());
	FAnimNotifyEventReference CurrentTP(&NotifyEvent, TPMontage);
	CurrentTP.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(NextTP->GetInstanceID());
	ToHand->Notify(FP, FPMontage, CurrentFP);
	TestFalse(TEXT("New FP Notify still shows its magazine"), HandMagazine->bHiddenInGame);
	ToWeapon->Notify(TP, TPMontage, CurrentTP);
	TestTrue(TEXT("New TP Notify still hides its magazine"), HandMagazine->bHiddenInGame);
	Commit->Notify(TP, TPMontage, CurrentTP);
	TestEqual(TEXT("New authoritative Commit refills its own magazine"), Pistol->GetCurrentAmmo(), Pistol->GetMagazineSize());
	TestFalse(TEXT("New authoritative Commit finishes its own weapon reload"), Pistol->IsReloading());
	return true;
}

#endif
