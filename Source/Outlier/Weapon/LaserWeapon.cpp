#include "Weapon/LaserWeapon.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/AutoTurret.h"
#include "Outlier.h"
#include "Components/SkeletalMeshComponent.h"
#include "Interface/WeaponMuzzleProvider.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TrailEffectDefinition.h"
#include "Weapon/LaserWeaponFeedbackDefinition.h"

ALaserWeapon::ALaserWeapon()
{
	EnforceSingleShotConfiguration();
}

void ALaserWeapon::EnforceSingleShotConfiguration()
{
	FireType = EWeaponFireType::HitScan;
	FireMode = EWeaponFireMode::SemiAuto;
	bIsAutomatic = false;
	bInfiniteAmmo = true;
	BurstShotCount = 1;
	ReuseCooldown = 0.0f;
	PostBurstCooldownSeconds = 0.0f;
	BloomCurrent = BloomMin = BloomMax = BloomPerShot = 0.0f;
	RecoilMultiplier = 0.0f;
	bIsReloading = false;
}

void ALaserWeapon::InitializeFromDataTables()
{
	Super::InitializeFromDataTables();
	// 공용 총기 Row가 자동 발사/유한 탄약으로 설정되어 있어도 레이저 계약은 유지한다.
	EnforceSingleShotConfiguration();
}

void ALaserWeapon::StartAttack()
{
	if (!HasAuthority()) return;
	EnforceSingleShotConfiguration();
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][LaserWeapon] StartAttack Owner=%s Weapon=%s CanAttack=%d Equipped=%d Hidden=%d OwnerHidden=%d"),
			*GetNameSafe(WeaponOwner), *GetName(), CanAttack(), bIsEquipped, IsHidden(), IsValid(WeaponOwner) && WeaponOwner->IsHidden());
	}
	Super::StartAttack();
}

void ALaserWeapon::PerformAttack()
{
	if (!HasAuthority()) return;
	EnforceSingleShotConfiguration();
	TGuardValue<bool> CompletingShot(bCompletingSingleShot, true);
	Super::PerformAttack();
}

void ALaserWeapon::StopAttack()
{
	Super::StopAttack();
	// 단발 완료도 기반 클래스의 StopAttack을 거친다. 외부 취소만 연출을 종료한다.
	if (!bCompletingSingleShot) EndLaserPresentation();
}

bool ALaserWeapon::CanPresentLaser() const
{
	if (!IsValid(WeaponOwner) || !bIsEquipped || IsHidden() || WeaponOwner->IsHidden()) return false;
	if (const AEnemyBase* Enemy = Cast<AEnemyBase>(WeaponOwner))
	{
		return !Enemy->IsDead() && Enemy->GetCombatState() != EEnemyCombatState::Stun
			&& !Enemy->IsAIControlSuppressed();
	}
	return true;
}

double ALaserWeapon::GetPresentationTime() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0.0;
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool ALaserWeapon::ResolveLaserMuzzle(FName PreferredSocket, USkeletalMeshComponent*& OutComponent,
	FName& OutSocket, FTransform& OutTransform) const
{
	OutComponent = ThirdPersonWeaponMesh;
	OutSocket = PreferredSocket;
	if (const IWeaponMuzzleProvider* Provider = Cast<IWeaponMuzzleProvider>(WeaponOwner))
	{
		OutComponent = Provider->GetWeaponMuzzleComponent(false);
		if (OutSocket.IsNone())
		{
			TArray<FName> Sockets;
			Provider->GetWeaponMuzzleSocketNames(false, Sockets);
			OutSocket = Sockets.IsEmpty() ? Provider->GetWeaponMuzzleSocketName(false) : Sockets[0];
		}
	}
	if (const ULaserWeaponFeedbackDefinition* Feedback = Cast<ULaserWeaponFeedbackDefinition>(FeedbackDefinition))
	{
		if (Feedback->ChargeEffect && !Feedback->ChargeEffect->AttachSocketName.IsNone())
		{
			OutSocket = Feedback->ChargeEffect->AttachSocketName;
		}
	}
	if (OutSocket.IsNone()) OutSocket = MuzzleSocketName;
	if (!IsValid(OutComponent) || !OutComponent->DoesSocketExist(OutSocket)) return false;
	OutTransform = OutComponent->GetSocketTransform(OutSocket, RTS_World);
	return !OutTransform.ContainsNaN();
}

bool ALaserWeapon::ResolveShotTransform(FName FiredMuzzleSocketName, FTransform& OutTransform) const
{
	USkeletalMeshComponent* Component = nullptr;
	FName Socket;
	// 카메라/타겟 좌표로 보정하지 않고 실제 총구 소켓의 +X 방향을 사용한다.
	const bool bResolved = ResolveLaserMuzzle(FiredMuzzleSocketName, Component, Socket, OutTransform);
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][LaserWeapon] ShotTransform Owner=%s Weapon=%s Resolved=%d Mesh=%s Socket=%s Start=%s Forward=%s"),
			*GetNameSafe(WeaponOwner), *GetName(), bResolved, *GetNameSafe(Component), *Socket.ToString(),
			bResolved ? *OutTransform.GetLocation().ToCompactString() : TEXT("Invalid"),
			bResolved ? *OutTransform.GetUnitAxis(EAxis::X).ToCompactString() : TEXT("Invalid"));
	}
	return bResolved;
}

void ALaserWeapon::BeginCharge(float ChargeDuration, float BeamDuration)
{
	if (!HasAuthority() || !CanPresentLaser())
	{
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][LaserWeapon] ChargeRejected Owner=%s Weapon=%s Authority=%d Equipped=%d Hidden=%d OwnerHidden=%d"),
				*GetNameSafe(WeaponOwner), *GetName(), HasAuthority(), bIsEquipped, IsHidden(), IsValid(WeaponOwner) && WeaponOwner->IsHidden());
		}
		return;
	}
	if (IWeaponMuzzleProvider* Provider = Cast<IWeaponMuzzleProvider>(WeaponOwner))
	{
		Provider->ResetWeaponMuzzleSequence();
	}
	USkeletalMeshComponent* Component = nullptr;
	FName Socket;
	FTransform Muzzle;
	if (!ResolveLaserMuzzle(NAME_None, Component, Socket, Muzzle))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][LaserWeapon] MissingSocket Owner=%s Weapon=%s Mesh=%s Socket=%s Feedback=%s"),
			*GetNameSafe(WeaponOwner), *GetName(), *GetNameSafe(Component), *Socket.ToString(), *GetNameSafe(FeedbackDefinition));
		return;
	}
	ActiveBeamDuration = FMath::IsFinite(BeamDuration) ? FMath::Max(BeamDuration, 0.0f) : 0.0f;
	++LaserPresentation.Revision;
	LaserPresentation.Phase = ELaserWeaponPresentationPhase::Telegraph;
	LaserPresentation.ServerStartTime = GetPresentationTime();
	LaserPresentation.Duration = FMath::IsFinite(ChargeDuration) ? FMath::Max(ChargeDuration, 0.0f) : 0.0f;
	LaserPresentation.MuzzleSocket = Socket;
	LaserPresentation.bHasImpact = false;
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		const ULaserWeaponFeedbackDefinition* Feedback = Cast<ULaserWeaponFeedbackDefinition>(FeedbackDefinition);
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][LaserWeapon] ChargeStarted Owner=%s Weapon=%s Revision=%u Socket=%s Start=%s Forward=%s Charge=%.3f Trail=%.3f Feedback=%s ChargeFX=%s TrailFX=%s ImpactFX=%s"),
			*GetNameSafe(WeaponOwner), *GetName(), LaserPresentation.Revision, *Socket.ToString(), *Muzzle.GetLocation().ToCompactString(),
			*Muzzle.GetUnitAxis(EAxis::X).ToCompactString(), LaserPresentation.Duration, ActiveBeamDuration, *GetNameSafe(FeedbackDefinition),
			*GetNameSafe(Feedback && Feedback->ChargeEffect ? Feedback->ChargeEffect->FXAsset.Get() : nullptr),
			*GetNameSafe(Feedback && Feedback->WeaponTrail ? Feedback->WeaponTrail->FXAsset.Get() : nullptr),
			*GetNameSafe(Feedback && Feedback->ImpactEffect ? Feedback->ImpactEffect->FXAsset.Get() : nullptr));
	}
	ApplyPresentation(LaserPresentation);
	SetActorTickEnabled(true);
	ForceNetUpdate();
}

void ALaserWeapon::EndLaserPresentation()
{
	if (HasAuthority() && LaserPresentation.Phase != ELaserWeaponPresentationPhase::Idle)
	{
		if (AAutoTurret::IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][LaserWeapon] PresentationEnd Owner=%s Weapon=%s Revision=%u Phase=%d Elapsed=%.3f"),
				*GetNameSafe(WeaponOwner), *GetName(), LaserPresentation.Revision, static_cast<int32>(LaserPresentation.Phase), GetPresentationTime() - LaserPresentation.ServerStartTime);
		}
		++LaserPresentation.Revision;
		LaserPresentation.Phase = ELaserWeaponPresentationPhase::Idle;
		ForceNetUpdate();
		MulticastLaserPresentation(LaserPresentation);
	}
	ClearLocalEffects();
}

void ALaserWeapon::OnHitscanShotResolved(const FVector& ShotStart, const FVector& TraceEnd, const FVector& ImpactNormal,
	bool bHasImpact, FName FiredMuzzleSocketName)
{
	USkeletalMeshComponent* Component = nullptr;
	FName Socket;
	FTransform Muzzle;
	if (!ResolveLaserMuzzle(FiredMuzzleSocketName, Component, Socket, Muzzle)) return;
	++LaserPresentation.Revision;
	LaserPresentation.Phase = ELaserWeaponPresentationPhase::Firing;
	LaserPresentation.EndPoint = TraceEnd;
	LaserPresentation.ShotStart = ShotStart;
	LaserPresentation.ImpactNormal = ImpactNormal;
	LaserPresentation.MuzzleSocket = Socket;
	LaserPresentation.ServerStartTime = GetPresentationTime();
	LaserPresentation.Duration = ActiveBeamDuration;
	LaserPresentation.bHasImpact = bHasImpact;
	if (AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][LaserWeapon] ShotResolved Owner=%s Weapon=%s Revision=%u Socket=%s Hit=%d Start=%s End=%s Normal=%s Trail=%.3f"),
			*GetNameSafe(WeaponOwner), *GetName(), LaserPresentation.Revision, *Socket.ToString(), bHasImpact,
			*ShotStart.ToCompactString(), *TraceEnd.ToCompactString(), *ImpactNormal.ToCompactString(), ActiveBeamDuration);
	}
	SetActorTickEnabled(true);
	ForceNetUpdate();
	// 짧은 발사 단계가 상태 복제 사이에 끝나도 현재 관련 클라이언트는 한 번 수신한다.
	MulticastLaserPresentation(LaserPresentation);
}

void ALaserWeapon::MulticastLaserPresentation_Implementation(const FLaserWeaponPresentation& State)
{
	ApplyPresentation(State);
}

void ALaserWeapon::OnRep_LaserPresentation()
{
	ApplyPresentation(LaserPresentation);
}

void ALaserWeapon::ApplyPresentation(const FLaserWeaponPresentation& State)
{
	// RPC와 RepNotify의 도착 순서가 달라도 이전 단계로 돌아가거나 효과를 중복 생성하지 않는다.
	if (bHasLocalPresentation && static_cast<int32>(State.Revision - LocalPresentation.Revision) < 0) return;
	const bool bNewPhase = !bHasLocalPresentation || State.Revision != LocalPresentation.Revision;
	if (bNewPhase && AAutoTurret::IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][LaserWeapon] PresentationReceived Owner=%s Weapon=%s NetMode=%d Revision=%u Phase=%d Age=%.3f Duration=%.3f CanPresent=%d"),
			*GetNameSafe(WeaponOwner), *GetName(), static_cast<int32>(GetNetMode()), State.Revision, static_cast<int32>(State.Phase),
			GetPresentationTime() - State.ServerStartTime, State.Duration, CanPresentLaser());
	}
	if (bNewPhase) ClearLocalEffects();
	LocalPresentation = State;
	bHasLocalPresentation = true;
	if (State.Phase != ELaserWeaponPresentationPhase::Idle)
	{
		SetActorTickEnabled(true);
		UpdateLocalPresentation();
	}
}

UNiagaraComponent* ALaserWeapon::CreateEffect(const UTrailEffectDefinition* Definition)
{
	UNiagaraSystem* System = Definition ? Cast<UNiagaraSystem>(Definition->FXAsset) : nullptr;
	if (!System) return nullptr;
	// 무기가 명시적으로 해제하므로 자동 파괴/풀 반환과 소유권을 섞지 않는다.
	return UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System,
		GetActorLocation(), FRotator::ZeroRotator, Definition->Scale,
		false, false, ENCPoolMethod::None, false);
}

void ALaserWeapon::UpdateEffect(UNiagaraComponent* Component, const UTrailEffectDefinition* Definition,
	const FTransform& Muzzle, const FVector& EndPoint, float Progress, bool bBeam)
{
	if (!Component || !Definition) return;
	const FVector Start = Muzzle.GetLocation() + Definition->StartOffset;
	const FVector End = EndPoint + Definition->EndOffset;
	Component->SetWorldLocationAndRotation(Start,
		(bBeam ? (End - Start).Rotation() : Muzzle.Rotator()) + Definition->RotationOffset);
	Component->SetVariablePosition(Definition->StartParameterName, Start);
	Component->SetVariablePosition(Definition->EndParameterName, End);
	if (const ULaserWeaponFeedbackDefinition* Feedback = Cast<ULaserWeaponFeedbackDefinition>(FeedbackDefinition))
	{
		if (bBeam) Component->SetVariableFloat(Feedback->WidthParameterName, Feedback->FireWidth);
		Component->SetVariableFloat(Feedback->ProgressParameterName, Progress);
	}
	if (!Component->IsActive()) Component->Activate(true);
}

void ALaserWeapon::UpdateLocalPresentation()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (LocalPresentation.Phase == ELaserWeaponPresentationPhase::Idle || !CanPresentLaser())
	{
		ClearLocalEffects();
		return;
	}
	const double Elapsed = FMath::Max(GetPresentationTime() - LocalPresentation.ServerStartTime, 0.0);
	const bool bFiring = LocalPresentation.Phase == ELaserWeaponPresentationPhase::Firing;
	if (bFiring && Elapsed >= LocalPresentation.Duration)
	{
		ClearLocalEffects();
		return;
	}
	const ULaserWeaponFeedbackDefinition* Feedback = Cast<ULaserWeaponFeedbackDefinition>(FeedbackDefinition);
	if (!Feedback) return;
	const float Progress = LocalPresentation.Duration > KINDA_SMALL_NUMBER
		? FMath::Clamp(static_cast<float>(Elapsed / LocalPresentation.Duration), 0.0f, 1.0f) : 1.0f;
	if (!bFiring)
	{
		USkeletalMeshComponent* MuzzleComponent = nullptr;
		FName Socket;
		FTransform Muzzle;
		if (!ResolveLaserMuzzle(LocalPresentation.MuzzleSocket, MuzzleComponent, Socket, Muzzle)) return;
		const UTrailEffectDefinition* Charge = Feedback->ChargeEffect;
		if (!IsValid(ChargeComponent) && Charge)
		{
			if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Charge->FXAsset))
			{
				// 실제 소켓에 부착하여 머리/본 움직임을 그대로 따라간다.
				ChargeComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(System, MuzzleComponent,
					Socket, Charge->RelativeLocation, Charge->RelativeRotation + Charge->RotationOffset,
					Charge->Scale, EAttachLocation::KeepRelativeOffset, false, ENCPoolMethod::None, false, false);
				if (ChargeComponent) ChargeComponent->Activate(true);
			}
		}
		if (ChargeComponent) ChargeComponent->SetVariableFloat(Feedback->ProgressParameterName, Progress);
	}
	else
	{
		// 시작/끝 모두 발사 순간의 서버 결과를 사용하여 머리가 돌아가도 빔 경로가 꺾이지 않는다.
		const FVector EndPoint = LocalPresentation.EndPoint;
		const FTransform ShotTransform((EndPoint - LocalPresentation.ShotStart).Rotation(), LocalPresentation.ShotStart);
		if (!IsValid(BeamComponent)) BeamComponent = CreateEffect(Feedback->WeaponTrail);
		UpdateEffect(BeamComponent, Feedback->WeaponTrail, ShotTransform, EndPoint, Progress);
		if (LocalPresentation.bHasImpact)
		{
			if (!IsValid(ImpactComponent)) ImpactComponent = CreateEffect(Feedback->ImpactEffect);
			const FTransform ImpactTransform(LocalPresentation.ImpactNormal.Rotation(), EndPoint);
			UpdateEffect(ImpactComponent, Feedback->ImpactEffect, ImpactTransform, EndPoint, Progress, false);
		}
	}
}

void ALaserWeapon::ClearLocalEffects()
{
	for (UNiagaraComponent* Component : { BeamComponent.Get(), ChargeComponent.Get(), ImpactComponent.Get() })
	{
		if (IsValid(Component)) Component->DestroyComponent();
	}
	BeamComponent = nullptr;
	ChargeComponent = nullptr;
	ImpactComponent = nullptr;
}

void ALaserWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && LaserPresentation.Phase != ELaserWeaponPresentationPhase::Idle)
	{
		if (!CanPresentLaser() || (LaserPresentation.Phase == ELaserWeaponPresentationPhase::Firing
			&& GetPresentationTime() - LaserPresentation.ServerStartTime >= LaserPresentation.Duration))
		{
			EndLaserPresentation();
		}
	}
	UpdateLocalPresentation();
	// 기반 클래스는 과충전 보간이 끝나면 Tick을 끄므로 레이저가 활성인 동안 다시 유지한다.
	if (LaserPresentation.Phase != ELaserWeaponPresentationPhase::Idle
		|| LocalPresentation.Phase != ELaserWeaponPresentationPhase::Idle) SetActorTickEnabled(true);
}

void ALaserWeapon::OnRep_EquippedState()
{
	Super::OnRep_EquippedState();
	if (!CanPresentLaser()) ClearLocalEffects();
	else if (LocalPresentation.Phase != ELaserWeaponPresentationPhase::Idle) SetActorTickEnabled(true);
}

void ALaserWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearLocalEffects();
	Super::EndPlay(EndPlayReason);
}

void ALaserWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALaserWeapon, LaserPresentation);
}
