#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Shooter/Anim/FirstPersonProceduralAnimRuntime.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
constexpr float ProbeStep = 1.0f / 60.0f;

enum class EProbeVariant : uint8
{
	ProceduralOff,
	NoBlend,
	Baseline,
	SourceDA,
	SourceHip,
	SourceRightEquip,
	SourceLeftEquip,
	// 무기가 ik_hand_gun에 붙으므로 오른손 IK만 옮기는 Equip 오프셋은 손-총 분리를 만든다. 0 적용 효과를 보간 ON/OFF로 검증.
	ZeroRightEquip,
	ZeroRightEquipNoBlend,
	// Equip 왼손 IK 오프셋(ik_hand_l에 더해짐)을 0으로 해 "ik_hand_l이 몽타주 왼손을 총 기준으로 드는지"만 분리
	ZeroLeftEquipIK,
	// 추가로 HipPose까지 0: 총의 절차 이동이 없으므로 복사된 ik_hand_l은 hand_l과 거의 같아야 한다
	ZeroLeftEquipIKAndHip,
	// 추가로 Equip 왼손 TwoBoneIK(alpha=LeftHandEquipIKAlpha)까지 끔: 복사 후 hand_l을 움직이는 노드인지 분리
	ZeroLeftEquipIKHipAndEquipIKNode,
	// ZeroLeftEquipIKAndHip 기준에서 왼팔 절차 요소를 추가로 끔(원인 분리)
	HipZeroNoArmPitch,     // 조준 피치 팔 커브 + LeftLowerArmRot (Stand 팔 오프셋)
	HipZeroNoArmEquipDA,   // Equip 윗팔/아랫팔/손 그립 DA 오프셋
	HipZeroNoArmAll        // 위 둘 모두
};

const TCHAR* VariantName(EProbeVariant Variant)
{
	switch (Variant)
	{
	case EProbeVariant::ProceduralOff: return TEXT("ProceduralOff");
	case EProbeVariant::NoBlend: return TEXT("NoBlend");
	case EProbeVariant::Baseline: return TEXT("Baseline");
	case EProbeVariant::SourceDA: return TEXT("SourceDA");
	case EProbeVariant::SourceHip: return TEXT("SourceHip");
	case EProbeVariant::SourceRightEquip: return TEXT("SourceRightEquip");
	case EProbeVariant::SourceLeftEquip: return TEXT("SourceLeftEquip");
	case EProbeVariant::ZeroRightEquip: return TEXT("ZeroRightEquip");
	case EProbeVariant::ZeroRightEquipNoBlend: return TEXT("ZeroRightEquipNoBlend");
	case EProbeVariant::ZeroLeftEquipIK: return TEXT("ZeroLeftEquipIK");
	case EProbeVariant::ZeroLeftEquipIKAndHip: return TEXT("ZeroLeftEquipIKAndHip");
	case EProbeVariant::ZeroLeftEquipIKHipAndEquipIKNode: return TEXT("ZeroLeftEquipIKHipAndEquipIKNode");
	case EProbeVariant::HipZeroNoArmPitch: return TEXT("HipZeroNoArmPitch");
	case EProbeVariant::HipZeroNoArmEquipDA: return TEXT("HipZeroNoArmEquipDA");
	case EProbeVariant::HipZeroNoArmAll: return TEXT("HipZeroNoArmAll");
	default: return TEXT("Unknown");
	}
}

struct FScopedEquipProbeWorld
{
	UWorld* World = nullptr;
	uint64 InitialFrameCounter = GFrameCounter;

	bool Initialize()
	{
		const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		if (!World)
		{
			GEngine->DestroyWorldContext(World);
			return false;
		}
		World->AddToRoot();
		Context.SetCurrentWorld(World);
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		if (!World->SetGameMode(FURL()))
		{
			return false;
		}
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return true;
	}

	~FScopedEquipProbeWorld()
	{
		if (!World)
		{
			return;
		}
		if (World->HasBegunPlay())
		{
			World->BeginTearingDown();
			World->EndPlay(EEndPlayReason::Quit);
			GFrameCounter = InitialFrameCounter;
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};

void TickProbeWorld(UWorld* World)
{
	World->Tick(LEVELTICK_All, ProbeStep);
	++GFrameCounter;
}

struct FProbeSample
{
	int32 Frame = 0;
	float MontageTime = 0.0f;
	float EquipPoseAlpha = 0.0f;
	float EquipIKAlpha = 0.0f;
	float HandGripGap = 0.0f;
	float IKGripGap = 0.0f;
	float HandStep = 0.0f;
	float HipStep = 0.0f;
	float WeaponRootStep = 0.0f;
	float RightHandIKStep = 0.0f;
	FVector Hand = FVector::ZeroVector;
	FVector Grip = FVector::ZeroVector;
	FVector Hip = FVector::ZeroVector;
	FVector WeaponRoot = FVector::ZeroVector;
	FVector RightHandIK = FVector::ZeroVector;

	// 프레임별 원인 분리용: 손/IK/총손 본, 그립 회전, 부착 상태, 슬롯·IK 가중치
	FRotator HandRot = FRotator::ZeroRotator;
	FVector HandIK = FVector::ZeroVector;
	FRotator HandIKRot = FRotator::ZeroRotator;
	FRotator GripRot = FRotator::ZeroRotator;
	FVector RightHand = FVector::ZeroVector;
	FVector GunBone = FVector::ZeroVector;
	FVector AttachSocket = FVector::ZeroVector;
	float WeaponToAttachSocketCm = 0.0f;
	// ik_hand_gun(무기 부착 본) 로컬 공간의 왼손 위치: 절차 오프셋과 무관하게 "총 기준 왼손"을 비교하기 위함
	FVector HandInGun = FVector::ZeroVector;
	FVector HandIKInGun = FVector::ZeroVector;
	float LeftHandFinalIKAlpha = 0.0f;
	float LeftHandFreeAlpha = 0.0f;
	float SlotWeight = 0.0f;
	float LeftHandIKAlpha = 0.0f;
	float WeaponPoseAlpha = 0.0f;
	bool bWeaponHidden = false;
	FName AttachParentBone = NAME_None;
};

FVector ComponentBoneOrZero(const USkeletalMeshComponent* Mesh, FName Bone)
{
	return Mesh->GetBoneIndex(Bone) != INDEX_NONE ? Mesh->GetSocketLocation(Bone) : FVector::ZeroVector;
}

bool CaptureSample(AShooterCharacter* Shooter, AWeaponBase* Weapon, int32 Frame,
	const FProbeSample* Previous, FProbeSample& OutSample)
{
	USkeletalMeshComponent* Arms = Shooter->GetFirstPersonMesh();
	USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonWeaponMesh();
	if (!Arms || !WeaponMesh || Arms->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE ||
		Arms->GetBoneIndex(TEXT("ik_hand_l")) == INDEX_NONE ||
		!WeaponMesh->DoesSocketExist(Weapon->GetLeftHandIKSocketName()))
	{
		return false;
	}
	const UShooterFirstPersonAnimInstance* Anim = Cast<UShooterFirstPersonAnimInstance>(Arms->GetAnimInstance());
	const FStructProperty* RuntimeProperty = Anim
		? FindFProperty<FStructProperty>(Anim->GetClass(), TEXT("ViewModelProceduralRuntime"))
		: nullptr;
	const FFirstPersonProceduralAnimRuntime* Runtime = RuntimeProperty
		? RuntimeProperty->ContainerPtrToValuePtr<FFirstPersonProceduralAnimRuntime>(Anim)
		: nullptr;
	if (!Anim || !Runtime)
	{
		return false;
	}
	OutSample.Frame = Frame;
	const FTransform HandTransform = Arms->GetSocketTransform(TEXT("hand_l"), RTS_World);
	const FTransform HandIKTransform = Arms->GetSocketTransform(TEXT("ik_hand_l"), RTS_World);
	const FTransform GripTransform = WeaponMesh->GetSocketTransform(Weapon->GetLeftHandIKSocketName(), RTS_World);
	OutSample.Hand = HandTransform.GetLocation();
	OutSample.HandRot = HandTransform.Rotator();
	OutSample.HandIK = HandIKTransform.GetLocation();
	OutSample.HandIKRot = HandIKTransform.Rotator();
	OutSample.Grip = GripTransform.GetLocation();
	OutSample.GripRot = GripTransform.Rotator();
	OutSample.HandGripGap = FVector::Distance(OutSample.Hand, OutSample.Grip);
	OutSample.IKGripGap = FVector::Distance(OutSample.HandIK, OutSample.Grip);
	OutSample.RightHand = ComponentBoneOrZero(Arms, TEXT("hand_r"));
	OutSample.GunBone = ComponentBoneOrZero(Arms, TEXT("ik_hand_gun"));
	if (Arms->GetBoneIndex(TEXT("ik_hand_gun")) != INDEX_NONE)
	{
		const FTransform GunTransform = Arms->GetSocketTransform(TEXT("ik_hand_gun"), RTS_World);
		OutSample.HandInGun = GunTransform.InverseTransformPositionNoScale(OutSample.Hand);
		OutSample.HandIKInGun = GunTransform.InverseTransformPositionNoScale(OutSample.HandIK);
	}
	OutSample.LeftHandFinalIKAlpha = Runtime->LeftHandFinalIKAlpha;
	OutSample.LeftHandFreeAlpha = Runtime->LeftHandFreeAlpha;
	OutSample.bWeaponHidden = WeaponMesh->bHiddenInGame;
	const FName AttachSocketName = WeaponMesh->GetAttachSocketName();
	if (WeaponMesh->GetAttachParent() == Arms && Arms->DoesSocketExist(AttachSocketName))
	{
		OutSample.AttachParentBone = Arms->GetSocketBoneName(AttachSocketName);
		OutSample.AttachSocket = Arms->GetSocketLocation(AttachSocketName);
		OutSample.WeaponToAttachSocketCm = FVector::Distance(
			WeaponMesh->GetComponentLocation(),
			(WeaponMesh->GetRelativeTransform() * Arms->GetSocketTransform(AttachSocketName, RTS_World)).GetLocation());
	}
	OutSample.SlotWeight = Anim->GetSlotMontageGlobalWeight(TEXT("UpperBody"));
	OutSample.LeftHandIKAlpha = Runtime->LeftHandIKAlpha;
	OutSample.WeaponPoseAlpha = Runtime->WeaponPoseAlpha;
	OutSample.HandStep = Previous ? FVector::Distance(Previous->Hand, OutSample.Hand) : 0.0f;
	OutSample.Hip = Runtime->HipPoseLoc;
	OutSample.WeaponRoot = Runtime->WeaponRootLocOffset;
	OutSample.RightHandIK = Runtime->RightHandIKLocOffset;
	OutSample.HipStep = Previous ? FVector::Distance(Previous->Hip, OutSample.Hip) : 0.0f;
	OutSample.WeaponRootStep = Previous ? FVector::Distance(Previous->WeaponRoot, OutSample.WeaponRoot) : 0.0f;
	OutSample.RightHandIKStep = Previous ? FVector::Distance(Previous->RightHandIK, OutSample.RightHandIK) : 0.0f;
	OutSample.EquipPoseAlpha = Runtime->EquipPoseAlpha;
	OutSample.EquipIKAlpha = Runtime->LeftHandEquipIKAlpha;
	if (const UAnimMontage* Montage = Shooter->GetFirstPersonEquipMontage())
	{
		OutSample.MontageTime = Anim->Montage_GetPosition(Montage);
	}
	return FMath::IsFinite(OutSample.HandGripGap) && FMath::IsFinite(OutSample.IKGripGap);
}

void ApplyVariant(UProceduralAnimValues* Destination, const UProceduralAnimValues* Source, EProbeVariant Variant)
{
	if (Variant == EProbeVariant::SourceDA)
	{
		Destination->WeaponValues = Source->WeaponValues;
		Destination->RecoilValues = Source->RecoilValues;
		return;
	}
	FWeaponValues& To = Destination->WeaponValues;
	const FWeaponValues& From = Source->WeaponValues;
	switch (Variant)
	{
	case EProbeVariant::SourceHip:
		To.HipPoseLoc = From.HipPoseLoc;
		To.HipPoseRot = From.HipPoseRot;
		break;
	case EProbeVariant::SourceRightEquip:
		To.RightHandEquipIKLocOffset = From.RightHandEquipIKLocOffset;
		To.RightHandEquipIKRotOffset = From.RightHandEquipIKRotOffset;
		break;
	case EProbeVariant::ZeroRightEquip:
	case EProbeVariant::ZeroRightEquipNoBlend:
		To.RightHandEquipIKLocOffset = FVector::ZeroVector;
		To.RightHandEquipIKRotOffset = FRotator::ZeroRotator;
		break;
	case EProbeVariant::ZeroLeftEquipIK:
		To.LeftHandEquipIKLoc = FVector::ZeroVector;
		To.LeftHandEquipIKRot = FRotator::ZeroRotator;
		break;
	case EProbeVariant::ZeroLeftEquipIKAndHip:
		To.LeftHandEquipIKLoc = FVector::ZeroVector;
		To.LeftHandEquipIKRot = FRotator::ZeroRotator;
		To.HipPoseLoc = FVector::ZeroVector;
		To.HipPoseRot = FRotator::ZeroRotator;
		break;
	case EProbeVariant::ZeroLeftEquipIKHipAndEquipIKNode:
		To.LeftHandEquipIKLoc = FVector::ZeroVector;
		To.LeftHandEquipIKRot = FRotator::ZeroRotator;
		To.HipPoseLoc = FVector::ZeroVector;
		To.HipPoseRot = FRotator::ZeroRotator;
		To.LeftHandEquipIKAlpha = 0.0f;
		break;
	case EProbeVariant::HipZeroNoArmPitch:
	case EProbeVariant::HipZeroNoArmEquipDA:
	case EProbeVariant::HipZeroNoArmAll:
		To.LeftHandEquipIKLoc = FVector::ZeroVector;
		To.LeftHandEquipIKRot = FRotator::ZeroRotator;
		To.HipPoseLoc = FVector::ZeroVector;
		To.HipPoseRot = FRotator::ZeroRotator;
		if (Variant != EProbeVariant::HipZeroNoArmEquipDA)
		{
			To.PitchLeftUpperArmLocCurve = nullptr;
			To.PitchLeftUpperArmRotCurve = nullptr;
			To.PitchLeftHandJointTargetLocCurve = nullptr;
			To.CrouchPitchLeftUpperArmLocCurve = nullptr;
			To.CrouchPitchLeftUpperArmRotCurve = nullptr;
			To.CrouchPitchLeftHandJointTargetLocCurve = nullptr;
			To.LeftLowerArmRot = FRotator::ZeroRotator;
		}
		if (Variant != EProbeVariant::HipZeroNoArmPitch)
		{
			To.LeftUpperArmEquipLoc = FVector::ZeroVector;
			To.LeftUpperArmEquipRot = FRotator::ZeroRotator;
			To.LeftLowerArmEquipRot = FRotator::ZeroRotator;
			To.LeftHandEquipGripOffsetLoc = FVector::ZeroVector;
			To.LeftHandEquipGripOffsetRot = FRotator::ZeroRotator;
		}
		break;
	case EProbeVariant::SourceLeftEquip:
		To.LeftHandEquipGripOffsetLoc = From.LeftHandEquipGripOffsetLoc;
		To.LeftHandEquipGripOffsetRot = From.LeftHandEquipGripOffsetRot;
		To.LeftHandEquipIKLoc = From.LeftHandEquipIKLoc;
		To.LeftHandEquipIKRot = From.LeftHandEquipIKRot;
		To.LeftHandEquipJointTargetLoc = From.LeftHandEquipJointTargetLoc;
		To.LeftHandEquipIKAlpha = From.LeftHandEquipIKAlpha;
		To.LeftHandEquipArmAlpha = From.LeftHandEquipArmAlpha;
		To.LeftHandEquipIKAlphaScale = From.LeftHandEquipIKAlphaScale;
		To.LeftHandEquipIKBlendInSpeed = From.LeftHandEquipIKBlendInSpeed;
		To.LeftHandEquipIKBlendOutSpeed = From.LeftHandEquipIKBlendOutSpeed;
		To.LeftUpperArmEquipLoc = From.LeftUpperArmEquipLoc;
		To.LeftUpperArmEquipRot = From.LeftUpperArmEquipRot;
		To.LeftLowerArmEquipRot = From.LeftLowerArmEquipRot;
		break;
	default:
		break;
	}
}

bool RunProbeCase(FAutomationTestBase& Test, EWeaponType SourceType, EProbeVariant Variant,
	TArray<FProbeSample>& OutSamples)
{
	FScopedEquipProbeWorld Fixture;
	if (!Fixture.Initialize())
	{
		Test.AddError(TEXT("Could not initialize the transient Shooter world"));
		return false;
	}
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PistolClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	UClass* RifleClass = LoadClass<ARangedWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!ShooterClass || !PistolClass || !RifleClass)
	{
		Test.AddError(TEXT("Shooter, pistol or rifle Blueprint could not be loaded"));
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass,
		FTransform::Identity, SpawnParameters);
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	ARangedWeaponBase* Pistol = Fixture.World->SpawnActor<ARangedWeaponBase>(PistolClass,
		FTransform::Identity, SpawnParameters);
	ARangedWeaponBase* Rifle = Fixture.World->SpawnActor<ARangedWeaponBase>(RifleClass,
		FTransform::Identity, SpawnParameters);
	if (!Shooter || !Controller || !Pistol || !Rifle)
	{
		Test.AddError(TEXT("Shooter fixture actors could not be spawned"));
		return false;
	}
	Controller->Possess(Shooter);
	Controller->SetAsLocalPlayerController();
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	USkeletalMeshComponent* Arms = Shooter->GetFirstPersonMesh();
	if (Arms && !Arms->GetAnimInstance())
	{
		Arms->InitAnim(true);
	}
	if (!Shooter->IsLocallyControlled() || !Arms || !Arms->GetAnimInstance())
	{
		Test.AddError(FString::Printf(TEXT("Shooter requires a locally controlled first-person AnimInstance: Local=%d Mesh=%s Anim=%s"),
			Shooter->IsLocallyControlled() ? 1 : 0, *GetNameSafe(Arms),
			*GetNameSafe(Arms ? Arms->GetAnimInstance() : nullptr)));
		return false;
	}
	Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// IK 본 베이크 소스 결정용: 가상 본(VB hand_gun 등)의 소스/타깃 본을 기록
	if (Arms->GetSkeletalMeshAsset() && Arms->GetSkeletalMeshAsset()->GetSkeleton())
	{
		for (const FVirtualBone& VirtualBone : Arms->GetSkeletalMeshAsset()->GetSkeleton()->GetVirtualBones())
		{
			Test.AddInfo(FString::Printf(TEXT("VirtualBone %s: Source=%s Target=%s"),
				*VirtualBone.VirtualBoneName.ToString(), *VirtualBone.SourceBoneName.ToString(),
				*VirtualBone.TargetBoneName.ToString()));
		}
	}
	ARangedWeaponBase* Source = SourceType == EWeaponType::Pistol ? Pistol : Rifle;
	ARangedWeaponBase* Target = SourceType == EWeaponType::Pistol ? Rifle : Pistol;
	const UProceduralAnimValues* SourceDA = Source->GetFirstPersonProceduralValues();
	const UProceduralAnimValues* TargetDA = Target->GetFirstPersonProceduralValues();
	if (!SourceDA || !TargetDA)
	{
		Test.AddError(TEXT("Weapon fixture has no first-person procedural DA"));
		return false;
	}
	if (Variant != EProbeVariant::Baseline)
	{
		UProceduralAnimValues* Override = DuplicateObject<UProceduralAnimValues>(TargetDA, Target);
		Override->SetFlags(RF_Transient);
		ApplyVariant(Override, SourceDA, Variant);
		FObjectProperty* Property = FindFProperty<FObjectProperty>(Target->GetClass(),
			TEXT("FirstPersonProceduralValues"));
		if (!Property)
		{
			Test.AddError(TEXT("Weapon DA property was not found"));
			return false;
		}
		Property->SetObjectPropertyValue_InContainer(Target, Override);
	}
	UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent();
	if (!Inventory)
	{
		Test.AddError(TEXT("Shooter inventory is missing"));
		return false;
	}
	Inventory->HandleEquipWeapon(Source);
	if (Shooter->GetCurrentWeapon() != Source)
	{
		Test.AddError(TEXT("Shooter could not equip the source weapon"));
		return false;
	}
	for (int32 Frame = 0; Frame < 360 && Shooter->IsActionLocked(); ++Frame)
	{
		TickProbeWorld(Fixture.World);
	}
	if (Shooter->IsActionLocked())
	{
		Test.AddError(TEXT("Source Equip did not finish"));
		return false;
	}
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		TickProbeWorld(Fixture.World);
	}
	FProbeSample BeforeSwitch;
	if (!CaptureSample(Shooter, Source, -1, nullptr, BeforeSwitch))
	{
		Test.AddError(TEXT("Source pose could not be sampled before switching weapons"));
		return false;
	}
	Inventory->HandleEquipWeapon(Target);
	if (Shooter->GetCurrentWeapon() != Target || Shooter->GetActionLock() != EShooterActionLock::Equip)
	{
		Test.AddError(TEXT("Shooter did not start the target Equip"));
		return false;
	}
	Target->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	int32 PostEquipFrames = 0;
	for (int32 Frame = 0; Frame < 360; ++Frame)
	{
		TickProbeWorld(Fixture.World);
		FProbeSample Sample;
		if (!CaptureSample(Shooter, Target, Frame, OutSamples.IsEmpty() ? &BeforeSwitch : &OutSamples.Last(), Sample))
		{
			Test.AddError(TEXT("Finalized hand/IK/grip transforms could not be sampled"));
			return false;
		}
		OutSamples.Add(Sample);
		if (Shooter->GetActionLock() == EShooterActionLock::Equip)
		{
			PostEquipFrames = 0;
		}
		else if (++PostEquipFrames >= 20)
		{
			break;
		}
	}
	if (OutSamples.IsEmpty() || Shooter->IsActionLocked())
	{
		Test.AddError(TEXT("Target Equip did not produce a complete sample sequence"));
		return false;
	}
	return true;
}

FString TransformJson(const FTransform& Transform)
{
	const FVector L = Transform.GetLocation();
	const FQuat Q = Transform.GetRotation();
	return FString::Printf(TEXT("{\"loc\":[%.4f,%.4f,%.4f],\"quat\":[%.6f,%.6f,%.6f,%.6f]}"),
		L.X, L.Y, L.Z, Q.X, Q.Y, Q.Z, Q.W);
}

// Equip 재생성용: 정착된 평상시 포즈에서 카메라/팔 컴포넌트/전체 본/무기 변환을 JSON으로 덤프
bool DumpSettledFraming(FAutomationTestBase& Test, EWeaponType WeaponType, bool bProceduralOff, FString& OutJson)
{
	FScopedEquipProbeWorld Fixture;
	if (!Fixture.Initialize())
	{
		Test.AddError(TEXT("Could not initialize the transient Shooter world"));
		return false;
	}
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr, WeaponType == EWeaponType::Pistol
		? TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C")
		: TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C"));
	if (!ShooterClass || !WeaponClass)
	{
		Test.AddError(TEXT("Shooter or weapon Blueprint could not be loaded"));
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = Fixture.World->SpawnActor<AShooterCharacter>(ShooterClass, FTransform::Identity, SpawnParameters);
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	ARangedWeaponBase* Weapon = Fixture.World->SpawnActor<ARangedWeaponBase>(WeaponClass, FTransform::Identity, SpawnParameters);
	if (!Shooter || !Controller || !Weapon)
	{
		Test.AddError(TEXT("Framing fixture actors could not be spawned"));
		return false;
	}
	Controller->Possess(Shooter);
	Controller->SetAsLocalPlayerController();
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Shooter->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	USkeletalMeshComponent* Arms = Shooter->GetFirstPersonMesh();
	if (Arms && !Arms->GetAnimInstance())
	{
		Arms->InitAnim(true);
	}
	UCameraComponent* Camera = Shooter->GetFirstPersonCameraComponent();
	if (!Arms || !Arms->GetAnimInstance() || !Camera)
	{
		Test.AddError(TEXT("Framing fixture requires first-person arms, AnimInstance and camera"));
		return false;
	}
	Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Shooter->GetInventoryComponent()->HandleEquipWeapon(Weapon);
	Weapon->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	for (int32 Frame = 0; Frame < 360 && Shooter->IsActionLocked(); ++Frame)
	{
		TickProbeWorld(Fixture.World);
	}
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		TickProbeWorld(Fixture.World);
	}

	const FTransform ArmsWorld = Arms->GetComponentTransform();
	const auto ToArms = [&ArmsWorld](const FTransform& World) { return World.GetRelativeTransform(ArmsWorld); };
	USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonWeaponMesh();
	const FBoxSphereBounds WeaponLocalBounds = WeaponMesh->CalcBounds(FTransform::Identity);

	const UShooterFirstPersonAnimInstance* Anim = Cast<UShooterFirstPersonAnimInstance>(Arms->GetAnimInstance());
	const FStructProperty* RuntimeProperty = Anim
		? FindFProperty<FStructProperty>(Anim->GetClass(), TEXT("ViewModelProceduralRuntime"))
		: nullptr;
	const FFirstPersonProceduralAnimRuntime* Runtime = RuntimeProperty
		? RuntimeProperty->ContainerPtrToValuePtr<FFirstPersonProceduralAnimRuntime>(Anim)
		: nullptr;

	TArray<FString> Bones;
	for (int32 BoneIndex = 0; BoneIndex < Arms->GetNumBones(); ++BoneIndex)
	{
		Bones.Add(FString::Printf(TEXT("\"%s\":%s"), *Arms->GetBoneName(BoneIndex).ToString(),
			*TransformJson(Arms->GetBoneTransform(BoneIndex, FTransform::Identity))));
	}
	const FName GripSocket = Weapon->GetLeftHandIKSocketName();
	OutJson = FString::Printf(
		TEXT("{\"weapon\":\"%s\",\"proceduralOff\":%s,")
		TEXT("\"cameraInArms\":%s,\"cameraFov\":%.3f,\"firstPersonFov\":%.3f,\"firstPersonFovEnabled\":%s,\"aspectRatio\":%.4f,")
		TEXT("\"armsWorld\":%s,\"weaponMeshInArms\":%s,\"weaponAttachSocket\":\"%s\",\"weaponAttachBone\":\"%s\",")
		TEXT("\"weaponLocalBoundsOrigin\":[%.3f,%.3f,%.3f],\"weaponLocalBoundsExtent\":[%.3f,%.3f,%.3f],")
		TEXT("\"gripSocketInArms\":%s,\"hipPoseLoc\":[%.4f,%.4f,%.4f],\"hipPoseRot\":[%.4f,%.4f,%.4f],")
		TEXT("\"bones\":{%s}}"),
		WeaponType == EWeaponType::Pistol ? TEXT("Pistol") : TEXT("Rifle"), bProceduralOff ? TEXT("true") : TEXT("false"),
		*TransformJson(ToArms(Camera->GetComponentTransform())), Camera->FieldOfView, Camera->FirstPersonFieldOfView,
		Camera->bEnableFirstPersonFieldOfView ? TEXT("true") : TEXT("false"), Camera->AspectRatio,
		*TransformJson(ArmsWorld), *TransformJson(ToArms(WeaponMesh->GetComponentTransform())),
		*WeaponMesh->GetAttachSocketName().ToString(), *Arms->GetSocketBoneName(WeaponMesh->GetAttachSocketName()).ToString(),
		WeaponLocalBounds.Origin.X, WeaponLocalBounds.Origin.Y, WeaponLocalBounds.Origin.Z,
		WeaponLocalBounds.BoxExtent.X, WeaponLocalBounds.BoxExtent.Y, WeaponLocalBounds.BoxExtent.Z,
		*TransformJson(WeaponMesh->DoesSocketExist(GripSocket) ? ToArms(WeaponMesh->GetSocketTransform(GripSocket, RTS_World)) : FTransform::Identity),
		Runtime ? Runtime->HipPoseLoc.X : 0.0, Runtime ? Runtime->HipPoseLoc.Y : 0.0, Runtime ? Runtime->HipPoseLoc.Z : 0.0,
		Runtime ? Runtime->HipPoseRot.Pitch : 0.0, Runtime ? Runtime->HipPoseRot.Yaw : 0.0, Runtime ? Runtime->HipPoseRot.Roll : 0.0,
		*FString::Join(Bones, TEXT(",")));
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierFirstPersonEquipDAProbeTest,
	"Outlier.Animation.FP.EquipDAProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierFirstPersonEquipDAProbeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (const IConsoleVariable* ForceOff = IConsoleManager::Get().FindConsoleVariable(
		TEXT("outlier.FPAnimForceProceduralOff")))
	{
		if (ForceOff->GetInt() != 0)
		{
			AddError(TEXT("Set outlier.FPAnimForceProceduralOff 0 before running this probe"));
			return false;
		}
	}
	IConsoleVariable* BlendDuration = IConsoleManager::Get().FindConsoleVariable(
		TEXT("outlier.FPAnimWeaponSwitchBlendDuration"));
	if (!BlendDuration || BlendDuration->GetFloat() <= 0.0f)
	{
		AddError(TEXT("Weapon switch blend duration must be positive before running this probe"));
		return false;
	}
	const float OriginalBlendDuration = BlendDuration->GetFloat();
	IConsoleVariable* ForceProceduralOff = IConsoleManager::Get().FindConsoleVariable(
		TEXT("outlier.FPAnimForceProceduralOff"));
	TArray<FString> CsvLines;
	CsvLines.Add(TEXT("Direction,Variant,Frame,MontageTime,EquipPoseAlpha,EquipIKAlpha,HandGripGapCm,IKGripGapCm,HandStepCm,HipStepCm,WeaponRootStepCm,RightHandIKStepCm,HandX,HandY,HandZ,GripX,GripY,GripZ,")
		TEXT("SlotWeight,LeftHandIKAlpha,WeaponPoseAlpha,WeaponHidden,AttachBone,WeaponToAttachSocketCm,")
		TEXT("HandPitch,HandYaw,HandRoll,HandIKX,HandIKY,HandIKZ,HandIKPitch,HandIKYaw,HandIKRoll,GripPitch,GripYaw,GripRoll,")
		TEXT("RightHandX,RightHandY,RightHandZ,GunBoneX,GunBoneY,GunBoneZ,AttachSocketX,AttachSocketY,AttachSocketZ,")
		TEXT("HandInGunX,HandInGunY,HandInGunZ,HandIKInGunX,HandIKInGunY,HandIKInGunZ,LeftHandFinalIKAlpha,LeftHandFreeAlpha"));
	for (EWeaponType SourceType : {EWeaponType::Pistol, EWeaponType::Rifle})
	{
		const TCHAR* Direction = SourceType == EWeaponType::Pistol ? TEXT("PistolToRifle") : TEXT("RifleToPistol");
		TArray<FProbeSample> ProceduralOff;
		TArray<FProbeSample> Baseline;
		TArray<FProbeSample> NoBlend;
		for (EProbeVariant Variant : {EProbeVariant::ProceduralOff, EProbeVariant::NoBlend, EProbeVariant::Baseline,
			EProbeVariant::SourceDA, EProbeVariant::SourceHip, EProbeVariant::SourceRightEquip, EProbeVariant::SourceLeftEquip,
			EProbeVariant::ZeroRightEquip, EProbeVariant::ZeroRightEquipNoBlend, EProbeVariant::ZeroLeftEquipIK,
			EProbeVariant::ZeroLeftEquipIKAndHip, EProbeVariant::ZeroLeftEquipIKHipAndEquipIKNode,
			EProbeVariant::HipZeroNoArmPitch, EProbeVariant::HipZeroNoArmEquipDA, EProbeVariant::HipZeroNoArmAll})
		{
			if (Variant == EProbeVariant::ProceduralOff && !ForceProceduralOff)
			{
				continue;
			}
			TArray<FProbeSample> Samples;
			const bool bNoBlend = Variant == EProbeVariant::NoBlend || Variant == EProbeVariant::ZeroRightEquipNoBlend;
			BlendDuration->Set(bNoBlend ? 0.0f : OriginalBlendDuration, ECVF_SetByCode);
			if (Variant == EProbeVariant::ProceduralOff)
			{
				ForceProceduralOff->Set(1, ECVF_SetByCode);
			}
			const bool bCaseSucceeded = RunProbeCase(*this, SourceType, Variant, Samples);
			BlendDuration->Set(OriginalBlendDuration, ECVF_SetByCode);
			if (ForceProceduralOff)
			{
				ForceProceduralOff->Set(0, ECVF_SetByCode);
			}
			if (!bCaseSucceeded)
			{
				return false;
			}
			if (Variant == EProbeVariant::Baseline && !NoBlend.IsEmpty())
			{
				TestTrue(FString::Printf(TEXT("%s blended first-frame hand step"), Direction),
					Samples[0].HandStep <= NoBlend[0].HandStep + 0.01f);
				TestTrue(FString::Printf(TEXT("%s blended first-frame Hip step"), Direction),
					Samples[0].HipStep <= NoBlend[0].HipStep + 0.01f);
				TestTrue(FString::Printf(TEXT("%s blended first-frame WeaponRoot step"), Direction),
					Samples[0].WeaponRootStep <= NoBlend[0].WeaponRootStep + 0.01f);
				TestTrue(FString::Printf(TEXT("%s blended first-frame right-hand IK step"), Direction),
					Samples[0].RightHandIKStep <= NoBlend[0].RightHandIKStep + 0.01f);
				TestTrue(FString::Printf(TEXT("%s reaches unblended Hip target"), Direction),
					FVector::Distance(Samples.Last().Hip, NoBlend.Last().Hip) < 0.05f);
				TestTrue(FString::Printf(TEXT("%s reaches unblended WeaponRoot target"), Direction),
					FVector::Distance(Samples.Last().WeaponRoot, NoBlend.Last().WeaponRoot) < 0.05f);
				TestTrue(FString::Printf(TEXT("%s reaches unblended right-hand IK target"), Direction),
					FVector::Distance(Samples.Last().RightHandIK, NoBlend.Last().RightHandIK) < 0.05f);
			}
			float MaxGap = 0.0f;
			float MaxHandStep = 0.0f;
			float MaxBaselineHandDelta = 0.0f;
			float SettledGapSum = 0.0f;
			float MaxMontageRightHandGunGap = 0.0f;
			float MaxMontageHandInGunDelta = 0.0f;
			float MaxMontageHandIKInGunDelta = 0.0f;
			float MaxMontageIKHandGap = 0.0f;
			const float SettledRightHandGunGap = FVector::Distance(Samples.Last().RightHand, Samples.Last().GunBone);
			for (int32 Index = 0; Index < Samples.Num(); ++Index)
			{
				const FProbeSample& Sample = Samples[Index];
				if (Sample.SlotWeight > 0.99f)
				{
					MaxMontageRightHandGunGap = FMath::Max(MaxMontageRightHandGunGap,
						FVector::Distance(Sample.RightHand, Sample.GunBone));
					MaxMontageIKHandGap = FMath::Max(MaxMontageIKHandGap, FVector::Distance(Sample.HandIK, Sample.Hand));
					// 몽타주 풀 가중치 구간에서 "총 기준 왼손"이 몽타주 원본(ProceduralOff)과 얼마나 다른지
					if (ProceduralOff.IsValidIndex(Index) && ProceduralOff[Index].SlotWeight > 0.99f)
					{
						MaxMontageHandInGunDelta = FMath::Max(MaxMontageHandInGunDelta,
							FVector::Distance(Sample.HandInGun, ProceduralOff[Index].HandInGun));
						// Final IK 타깃(ik_hand_l)이 몽타주 왼손을 총 기준으로 들고 있는지 (ABP 바인딩 전에도 확인 가능)
						MaxMontageHandIKInGunDelta = FMath::Max(MaxMontageHandIKInGunDelta,
							FVector::Distance(Sample.HandIKInGun, ProceduralOff[Index].HandInGun));
					}
				}
				MaxGap = FMath::Max(MaxGap, Sample.HandGripGap);
				MaxHandStep = FMath::Max(MaxHandStep, Sample.HandStep);
				if (Baseline.IsValidIndex(Index))
				{
					MaxBaselineHandDelta = FMath::Max(MaxBaselineHandDelta,
						FVector::Distance(Sample.Hand, Baseline[Index].Hand));
				}
				if (Index >= Samples.Num() - 20)
				{
					SettledGapSum += Sample.HandGripGap;
				}
				CsvLines.Add(FString::Printf(TEXT("%s,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,")
					TEXT("%.4f,%.4f,%.4f,%d,%s,%.4f,")
					TEXT("%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,")
					TEXT("%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,")
					TEXT("%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f"),
					Direction, VariantName(Variant), Sample.Frame, Sample.MontageTime,
					Sample.EquipPoseAlpha, Sample.EquipIKAlpha, Sample.HandGripGap, Sample.IKGripGap,
					Sample.HandStep, Sample.HipStep, Sample.WeaponRootStep, Sample.RightHandIKStep,
					Sample.Hand.X, Sample.Hand.Y, Sample.Hand.Z,
					Sample.Grip.X, Sample.Grip.Y, Sample.Grip.Z,
					Sample.SlotWeight, Sample.LeftHandIKAlpha, Sample.WeaponPoseAlpha, Sample.bWeaponHidden ? 1 : 0,
					*Sample.AttachParentBone.ToString(), Sample.WeaponToAttachSocketCm,
					Sample.HandRot.Pitch, Sample.HandRot.Yaw, Sample.HandRot.Roll,
					Sample.HandIK.X, Sample.HandIK.Y, Sample.HandIK.Z,
					Sample.HandIKRot.Pitch, Sample.HandIKRot.Yaw, Sample.HandIKRot.Roll,
					Sample.GripRot.Pitch, Sample.GripRot.Yaw, Sample.GripRot.Roll,
					Sample.RightHand.X, Sample.RightHand.Y, Sample.RightHand.Z,
					Sample.GunBone.X, Sample.GunBone.Y, Sample.GunBone.Z,
					Sample.AttachSocket.X, Sample.AttachSocket.Y, Sample.AttachSocket.Z,
					Sample.HandInGun.X, Sample.HandInGun.Y, Sample.HandInGun.Z,
					Sample.HandIKInGun.X, Sample.HandIKInGun.Y, Sample.HandIKInGun.Z,
					Sample.LeftHandFinalIKAlpha, Sample.LeftHandFreeAlpha));
			}
			AddInfo(FString::Printf(TEXT("%s %s: frames=%d max grip gap=%.2f cm, settled gap=%.2f cm, max hand step=%.2f cm, max hand deviation from baseline=%.2f cm, montage right-hand/gun gap=%.2f cm (settled %.2f cm), montage left-hand-in-gun delta vs ProceduralOff=%.2f cm, ik_hand_l-in-gun delta=%.2f cm, montage ik_hand_l/hand_l gap=%.2f cm"),
				Direction, VariantName(Variant), Samples.Num(), MaxGap,
				SettledGapSum / 20.0f, MaxHandStep, MaxBaselineHandDelta,
				MaxMontageRightHandGunGap, SettledRightHandGunGap, MaxMontageHandInGunDelta, MaxMontageHandIKInGunDelta,
				MaxMontageIKHandGap));
			if (Variant == EProbeVariant::ZeroRightEquip || Variant == EProbeVariant::ZeroRightEquipNoBlend)
			{
				// 기준: 몽타주 원본(ProceduralOff)의 몽타주 구간 오른손-총 간격. 없으면 정착값.
				float ReferenceRightHandGunGap = SettledRightHandGunGap;
				if (!ProceduralOff.IsEmpty())
				{
					ReferenceRightHandGunGap = 0.0f;
					for (const FProbeSample& OffSample : ProceduralOff)
					{
						if (OffSample.SlotWeight > 0.99f)
						{
							ReferenceRightHandGunGap = FMath::Max(ReferenceRightHandGunGap,
								FVector::Distance(OffSample.RightHand, OffSample.GunBone));
						}
					}
				}
				TestTrue(FString::Printf(TEXT("%s %s keeps the right hand on the gun during Equip"), Direction, VariantName(Variant)),
					MaxMontageRightHandGunGap <= ReferenceRightHandGunGap + 1.0f);
			}
			if (Variant == EProbeVariant::ProceduralOff)
			{
				ProceduralOff = MoveTemp(Samples);
			}
			else if (Variant == EProbeVariant::Baseline)
			{
				Baseline = MoveTemp(Samples);
			}
			else if (Variant == EProbeVariant::NoBlend)
			{
				NoBlend = MoveTemp(Samples);
			}
		}
	}
	const FString OutputPath = FPaths::ProjectSavedDir() / TEXT("ProceduralAnimExports") /
		FString::Printf(TEXT("EquipDAProbe_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	if (!FFileHelper::SaveStringArrayToFile(CsvLines, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		AddError(FString::Printf(TEXT("Could not save Equip DA probe CSV: %s"), *OutputPath));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Frame samples: %s"), *OutputPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierFirstPersonFramingDumpTest,
	"Outlier.Animation.FP.FramingDump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierFirstPersonFramingDumpTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	IConsoleVariable* ForceProceduralOff = IConsoleManager::Get().FindConsoleVariable(
		TEXT("outlier.FPAnimForceProceduralOff"));
	if (!ForceProceduralOff)
	{
		AddError(TEXT("outlier.FPAnimForceProceduralOff is not registered"));
		return false;
	}
	IConsoleVariable* EyeAlign = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.FPViewModelEyeAlign"));
	if (!EyeAlign)
	{
		AddError(TEXT("outlier.FPViewModelEyeAlign is not registered"));
		return false;
	}
	TArray<FString> Entries;
	for (bool bEyeAlign : {false, true})
	{
		for (EWeaponType WeaponType : {EWeaponType::Rifle, EWeaponType::Pistol})
		{
			for (bool bProceduralOff : {true, false})
			{
				ForceProceduralOff->Set(bProceduralOff ? 1 : 0, ECVF_SetByCode);
				EyeAlign->Set(bEyeAlign ? 1 : 0, ECVF_SetByCode);
				FString Json;
				const bool bSucceeded = DumpSettledFraming(*this, WeaponType, bProceduralOff, Json);
				ForceProceduralOff->Set(0, ECVF_SetByCode);
				EyeAlign->Set(0, ECVF_SetByCode);
				if (!bSucceeded)
				{
					return false;
				}
				Entries.Add(FString::Printf(TEXT("{\"eyeAlign\":%s,"), bEyeAlign ? TEXT("true") : TEXT("false")) + Json.Mid(1));
			}
		}
	}
	const FString OutputPath = FPaths::ProjectSavedDir() / TEXT("ProceduralAnimExports") /
		FString::Printf(TEXT("FPFraming_%s.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	if (!FFileHelper::SaveStringToFile(TEXT("[") + FString::Join(Entries, TEXT(",")) + TEXT("]"), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		AddError(FString::Printf(TEXT("Could not save framing dump: %s"), *OutputPath));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Framing dump: %s"), *OutputPath));
	return true;
}

#endif
