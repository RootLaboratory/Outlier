#include "Shooter/Anim/WeaponWallAvoidance.h"
#include "Shooter/Anim/WeaponValues.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

void FWeaponWallAvoidanceState::Update(UWorld* World, const AActor* Shooter, const AActor* Weapon,
	const FVector& DetectionOrigin, const FRotator& AimRotation,
	float DeltaSeconds, const FWeaponValues* WeaponValues, float ActionAlpha)
{
	WallTargetAlpha = 0.0f;
	WallUpTargetAlpha = 0.0f;
	WallDownTargetAlpha = 0.0f;
	WallSideTargetAlpha = 0.0f;
	WallSideTargetSign = 0.0f;
	WallMuzzleBlockTargetAlpha = 0.0f;
	WallVeryCloseTargetAlpha = 0.0f;
	WallTopEdgeTargetAlpha = 0.0f;
	WallCeilingTargetAlpha = 0.0f;
	WallHardStopTargetAlpha = 0.0f;
	WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;


	if (!World || !WeaponValues || DetectionOrigin.ContainsNaN() || AimRotation.ContainsNaN()
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f || !FMath::IsFinite(ActionAlpha))
	{
		Reset();
		return;
	}

	WallMuzzleBlockReleaseHoldTimer = FMath::Max(0.0f, WallMuzzleBlockReleaseHoldTimer - DeltaSeconds);

	// 감지는 Lean을 제거한 기준점에서 수행한다. 실제 발사 위치의 Lean은 호출자가 별도로 적용한다.
	const FVector Start = DetectionOrigin;
	const FRotator Rotation(AimRotation.Pitch, AimRotation.Yaw, 0.0f);
	const FVector Forward = Rotation.Vector();
	const FVector Right = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
	const float TraceDistance = FMath::Max(WeaponValues->WallTraceDistance, 0.0f);
	const float TraceRadius = FMath::Max(WeaponValues->WallTraceRadius, KINDA_SMALL_NUMBER);
	const float SafeDistance = FMath::Max(WeaponValues->WallSafeDistance, KINDA_SMALL_NUMBER);
	const float ProbeForwardOffset = FMath::Max(WeaponValues->WallProbeForwardOffset, 0.0f);
	const float ProbeRightOffset = WeaponValues->WallProbeRightOffset;
	const float ProbeUpOffset = WeaponValues->WallProbeUpOffset;
	const float VerticalProbeOffset = FMath::Max(WeaponValues->WallVerticalProbeOffset, 0.0f);
	const float SideProbeOffset = FMath::Max(WeaponValues->WallSideProbeOffset, TraceRadius * 1.5f);
	const float MuzzleBlockTraceDistance = FMath::Max(WeaponValues->WallMuzzleBlockTraceDistance, 0.0f);
	const float MuzzleBlockSafeDistance = FMath::Max(WeaponValues->WallMuzzleBlockSafeDistance, KINDA_SMALL_NUMBER);
	const float BarrelBlockSafeDistance = FMath::Max(WeaponValues->WallBarrelBlockSafeDistance, KINDA_SMALL_NUMBER);
	const float PitchBlockAlpha = FMath::Clamp(FMath::Abs(Forward.Z), 0.0f, 1.0f);
	const float MuzzleBlockTraceRadius = FMath::Max(
		WeaponValues->WallMuzzleBlockTraceRadius + WeaponValues->WallPitchBlockTraceRadiusBoost * PitchBlockAlpha,
		KINDA_SMALL_NUMBER
	);
	const float BarrelBlockLength = FMath::Max(
		WeaponValues->WallBarrelBlockLength + WeaponValues->WallPitchBlockLengthBoost * PitchBlockAlpha,
		0.0f
	);
	const float BarrelBlockTraceRadius = FMath::Max(
		WeaponValues->WallBarrelBlockTraceRadius + WeaponValues->WallPitchBlockTraceRadiusBoost * PitchBlockAlpha,
		KINDA_SMALL_NUMBER
	);
	const float DebugPointSize = 5.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ViewModelWallTrace), false, Shooter);
	Params.AddIgnoredActor(Shooter);
	if (Weapon)
	{
		Params.AddIgnoredActor(Weapon);
	}

	// ── 2. 십자 프로브 스윕 (중앙/상/하/좌/우) ────────────────────────────
	const FVector BaseTraceStart =
		Start +
		Forward * ProbeForwardOffset +
		Right * ProbeRightOffset +
		Up * ProbeUpOffset;

	struct FWallProbeResult
	{
		float Alpha = 0.0f;
		FHitResult Hit;
		bool bHit = false;
	};

	const auto SweepWallProbe = [&](const FVector& ProbeStart) -> FWallProbeResult
	{
		FWallProbeResult Result;
		const FVector ProbeEnd = ProbeStart + Forward * TraceDistance;
		Result.bHit = World->SweepSingleByChannel(
			Result.Hit,
			ProbeStart,
			ProbeEnd,
			FQuat::Identity,
			ECC_Visibility,
			FCollisionShape::MakeSphere(TraceRadius),
			Params
		);

		// 바닥 필터: 위를 향한 노멀은 벽이 아니다. 앉아서 아래를 볼 때 바닥을
		// 때려 근접 단계가 켜지면, 그 raw 타깃이 3인칭 union으로 전파돼
		// 3인칭 상체까지 튄다
		if (Result.bHit &&
			Result.Hit.ImpactNormal.Z > FMath::Clamp(WeaponValues->WallFloorNormalZ, 0.0f, 1.0f))
		{
			Result.bHit = false;
			Result.Hit = FHitResult();
		}

		if (Result.bHit)
		{
			Result.Alpha = FMath::Clamp((TraceDistance - Result.Hit.Distance) / SafeDistance, 0.0f, 1.0f);
		}

		return Result;
	};

	const FWallProbeResult CenterProbe = SweepWallProbe(BaseTraceStart);
	const FWallProbeResult UpperProbe = SweepWallProbe(BaseTraceStart + Up * VerticalProbeOffset);
	const FWallProbeResult LowerProbe = SweepWallProbe(BaseTraceStart - Up * VerticalProbeOffset);
	const FWallProbeResult RightProbe = SweepWallProbe(BaseTraceStart + Right * SideProbeOffset);
	const FWallProbeResult LeftProbe = SweepWallProbe(BaseTraceStart - Right * SideProbeOffset);

	// ── 3. 경계 판정 ─────────────────────────────────────────────────────
	// TopEdge: 아래 프로브만 벽을 봄(벽 위 모서리 너머를 조준)
	// Ceiling: 위 프로브만 벽을 봄(경계 아래에서 위쪽이 막힘)
	WallTopEdgeTargetAlpha = FMath::Clamp(
		LowerProbe.Alpha - FMath::Max(CenterProbe.Alpha, UpperProbe.Alpha),
		0.0f,
		1.0f
	);
	WallTopEdgeAlpha = FMath::FInterpTo(
		WallTopEdgeAlpha,
		WallTopEdgeTargetAlpha,
		DeltaSeconds,
		WallTopEdgeTargetAlpha > WallTopEdgeAlpha
			? WeaponValues->WallTopEdgeBlendInSpeed
			: WeaponValues->WallTopEdgeBlendOutSpeed
	);
	WallCeilingTargetAlpha = FMath::Clamp(
		UpperProbe.Alpha - FMath::Max(CenterProbe.Alpha, LowerProbe.Alpha),
		0.0f,
		1.0f
	);
	WallCeilingAlpha = FMath::FInterpTo(
		WallCeilingAlpha,
		WallCeilingTargetAlpha,
		DeltaSeconds,
		WallCeilingTargetAlpha > WallCeilingAlpha
			? WeaponValues->WallCeilingBlendInSpeed
			: WeaponValues->WallCeilingBlendOutSpeed
	);

	// ── 4. 총구/총열 차단 프로브 ─────────────────────────────────────────
	// 총열(Barrel)은 총구 기준점 뒤쪽 구간을 스윕해서, 조준 레이가 벽을
	// 비껴가도 벽에 나란히 누운 총열이 감지되게 한다
	const FVector MuzzleBlockTraceStart =
		Start +
		Forward * FMath::Max(WeaponValues->WallMuzzleBlockForwardOffset, 0.0f) +
		Right * WeaponValues->WallMuzzleBlockRightOffset +
		Up * WeaponValues->WallMuzzleBlockUpOffset;
	const FVector MuzzleBlockTraceForward = Forward;
	FWallProbeResult MuzzleBlockProbe;
	const FVector MuzzleBlockTraceEnd = MuzzleBlockTraceStart + MuzzleBlockTraceForward * MuzzleBlockTraceDistance;
	if (MuzzleBlockTraceDistance > KINDA_SMALL_NUMBER)
	{
		MuzzleBlockProbe.bHit = World->SweepSingleByChannel(
			MuzzleBlockProbe.Hit,
			MuzzleBlockTraceStart,
			MuzzleBlockTraceEnd,
			FQuat::Identity,
			ECC_Visibility,
			FCollisionShape::MakeSphere(MuzzleBlockTraceRadius),
			Params
		);

		if (MuzzleBlockProbe.bHit &&
			MuzzleBlockProbe.Hit.ImpactNormal.Z > FMath::Clamp(WeaponValues->WallFloorNormalZ, 0.0f, 1.0f))
		{
			MuzzleBlockProbe.bHit = false;
			MuzzleBlockProbe.Hit = FHitResult();
		}

		if (MuzzleBlockProbe.bHit)
		{
			MuzzleBlockProbe.Alpha = FMath::Clamp(
				(MuzzleBlockTraceDistance - MuzzleBlockProbe.Hit.Distance) / MuzzleBlockSafeDistance,
				0.0f,
				1.0f
			);
		}
	}
	WallMuzzleBlockTargetAlpha = MuzzleBlockProbe.Alpha;

	FWallProbeResult BarrelBlockProbe;
	const FVector BarrelBlockTraceStart = MuzzleBlockTraceStart - MuzzleBlockTraceForward * BarrelBlockLength;
	const FVector BarrelBlockTraceEnd = MuzzleBlockTraceStart;
	if (BarrelBlockLength > KINDA_SMALL_NUMBER)
	{
		BarrelBlockProbe.bHit = World->SweepSingleByChannel(
			BarrelBlockProbe.Hit,
			BarrelBlockTraceStart,
			BarrelBlockTraceEnd,
			FQuat::Identity,
			ECC_Visibility,
			FCollisionShape::MakeSphere(BarrelBlockTraceRadius),
			Params
		);

		if (BarrelBlockProbe.bHit &&
			BarrelBlockProbe.Hit.ImpactNormal.Z > FMath::Clamp(WeaponValues->WallFloorNormalZ, 0.0f, 1.0f))
		{
			BarrelBlockProbe.bHit = false;
			BarrelBlockProbe.Hit = FHitResult();
		}

		if (BarrelBlockProbe.bHit)
		{
			BarrelBlockProbe.Alpha = FMath::Clamp(
				(BarrelBlockLength - BarrelBlockProbe.Hit.Distance) / BarrelBlockSafeDistance,
				0.0f,
				1.0f
			);
		}
	}
	const float TopEdgeBarrelBlockScale = FMath::Clamp(WeaponValues->WallTopEdgeBarrelBlockScale, 0.0f, 1.0f);
	const float BarrelBlockTargetAlpha = FMath::Lerp(
		BarrelBlockProbe.Alpha,
		BarrelBlockProbe.Alpha * TopEdgeBarrelBlockScale,
		WallTopEdgeAlpha
	);
	WallMuzzleBlockTargetAlpha = FMath::Max(WallMuzzleBlockTargetAlpha, BarrelBlockTargetAlpha);

	// ── 5. HardStop: 실제 히트 거리 기반 최근접 ──────────────────────────
	// 선형 프로브 알파는 벽이 아직 멀 때 이미 포화되므로, 최근접 정보는
	// 히트 거리로 직접 램프한다
	const float HardStopClearance = FMath::Max(WeaponValues->WallHardStopClearance, 0.0f);
	const float HardStopRange = FMath::Max(WeaponValues->WallHardStopRange, KINDA_SMALL_NUMBER);
	const auto GetHardStopAlphaFromDistance = [&](const float Distance) -> float
	{
		return FMath::Clamp((HardStopClearance + HardStopRange - Distance) / HardStopRange, 0.0f, 1.0f);
	};
	const float MuzzleHardStopAlpha = MuzzleBlockProbe.bHit
		? GetHardStopAlphaFromDistance(MuzzleBlockProbe.Hit.Distance)
		: 0.0f;
	const float BarrelDistanceToMuzzle = BarrelBlockProbe.bHit
		? FMath::Max(BarrelBlockLength - BarrelBlockProbe.Hit.Distance, 0.0f)
		: MAX_flt;
	const float BarrelHardStopAlpha = BarrelBlockProbe.bHit
		? FMath::Max(GetHardStopAlphaFromDistance(BarrelDistanceToMuzzle), BarrelBlockProbe.Alpha * 0.5f)
		: 0.0f;
	WallHardStopTargetAlpha = FMath::Max(MuzzleHardStopAlpha, BarrelHardStopAlpha);

	// ── 6. VeryClose + 3인칭으로 raw 타깃 publish ────────────────────────
	// 근접 단계 raw 타깃은 3인칭이 읽어가 자기 WallTight 타깃과 union한다
	// (FP → TP 단방향). 역방향으로 3인칭 값을 수입하면 "몸은 벽에 붙었지만
	// 카메라 방향은 뚫려 있는" 상황(위를 보거나 모서리를 비껴 조준)에서
	// 뷰모델이 이유 없이 당겨져 손이 어긋나 보이므로 하지 않는다
	WallHardStopOwnTargetAlpha = WallHardStopTargetAlpha;
	const float VeryCloseProbeAlpha = FMath::Max(
		CenterProbe.Alpha,
		FMath::Min(FMath::Max(UpperProbe.Alpha, LowerProbe.Alpha), CenterProbe.Alpha + 0.25f)
	);
	const float TopEdgeVeryCloseScale = FMath::Clamp(WeaponValues->WallTopEdgeVeryCloseScale, 0.0f, 1.0f);
	const float VeryCloseMuzzleBlockScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleBlockScale, 0.0f, 1.0f);
	const float VeryCloseMuzzleSourceAlpha = FMath::Lerp(
		WallMuzzleBlockTargetAlpha * VeryCloseMuzzleBlockScale,
		WallMuzzleBlockTargetAlpha * VeryCloseMuzzleBlockScale * TopEdgeVeryCloseScale,
		WallTopEdgeAlpha
	);
	const float VeryCloseSourceAlpha = FMath::Max(
		VeryCloseProbeAlpha,
		FMath::Max(VeryCloseMuzzleSourceAlpha, WallHardStopTargetAlpha)
	);
	const float VeryCloseStartAlpha = FMath::Clamp(WeaponValues->WallVeryCloseStartAlpha, 0.0f, 1.0f);
	const float VeryCloseFullAlpha = FMath::Max(
		FMath::Clamp(WeaponValues->WallVeryCloseFullAlpha, 0.0f, 1.0f),
		VeryCloseStartAlpha + KINDA_SMALL_NUMBER
	);
	WallVeryCloseTargetAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(VeryCloseStartAlpha, VeryCloseFullAlpha),
		FVector2D(0.0f, 1.0f),
		VeryCloseSourceAlpha
	);
	WallVeryCloseOwnTargetAlpha = WallVeryCloseTargetAlpha;

	// ── 7. 상/하 회피 라우팅 ─────────────────────────────────────────────
	// Ceiling이 뚜렷하면 무조건 아래로, 아니면 위/아래 중 우세한 쪽으로
	const bool bUpperDominant = UpperProbe.Alpha >= LowerProbe.Alpha;
	const float RawUpAlpha = FMath::Max(UpperProbe.Alpha, bUpperDominant ? CenterProbe.Alpha : 0.0f);
	const float RawDownAlpha = FMath::Max(LowerProbe.Alpha, bUpperDominant ? 0.0f : CenterProbe.Alpha);
	if (WallCeilingAlpha > 0.2f)
	{
		WallUpTargetAlpha = 0.0f;
		WallDownTargetAlpha = FMath::Max(RawDownAlpha, WallCeilingAlpha);
	}
	else if (RawDownAlpha > RawUpAlpha + 0.15f)
	{
		WallDownTargetAlpha = RawDownAlpha;
	}
	else
	{
		WallUpTargetAlpha = RawUpAlpha;
	}
	const float LookDownAlpha = FMath::Clamp((-Forward.Z - 0.12f) / 0.35f, 0.0f, 1.0f);
	const float DownProbePreferenceAlpha = RawDownAlpha > RawUpAlpha + 0.05f ? 1.0f : 0.0f;
	WallMuzzleBlockDownPreferenceTargetAlpha = FMath::Max3(LookDownAlpha, DownProbePreferenceAlpha, WallCeilingAlpha);
	WallMuzzleBlockDownPreferenceAlpha = FMath::FInterpTo(
		WallMuzzleBlockDownPreferenceAlpha,
		WallMuzzleBlockDownPreferenceTargetAlpha,
		DeltaSeconds,
		WeaponValues->WallMuzzleBlockPreferenceBlendSpeed
	);

	// ── 8. 좌/우 회피 ────────────────────────────────────────────────────
	const float RawRightAlpha = RightProbe.Alpha;
	const float RawLeftAlpha = LeftProbe.Alpha;
	const float CenterSideSuppressAlpha = CenterProbe.Alpha * 0.65f;
	const float RightResponseScale = FMath::Clamp(WeaponValues->WallRightSideResponseScale, 0.0f, 1.0f);
	const float LeftResponseScale = FMath::Clamp(WeaponValues->WallLeftSideResponseScale, 0.0f, 1.0f);
	const float RightSideAlpha = FMath::Clamp(
		(RawRightAlpha - FMath::Max(RawLeftAlpha, CenterSideSuppressAlpha)) * RightResponseScale,
		0.0f,
		1.0f
	);
	const float LeftSideAlpha = FMath::Clamp(
		(RawLeftAlpha - FMath::Max(RawRightAlpha, CenterSideSuppressAlpha)) * LeftResponseScale,
		0.0f,
		1.0f
	);
	if (FMath::Max(RightSideAlpha, LeftSideAlpha) > 0.0f)
	{
		WallSideTargetAlpha = FMath::Max(RightSideAlpha, LeftSideAlpha);
		WallSideTargetSign = RightSideAlpha >= LeftSideAlpha ? -1.0f : 1.0f;
	}

	WallTargetAlpha = FMath::Max(WallUpTargetAlpha, WallDownTargetAlpha);

	// ── 디버그 드로잉 (bDrawWallOffsetDebug) ─────────────────────────────
	if (WeaponValues->bDrawWallOffsetDebug)
	{
		const auto DrawProbe = [&](const FWallProbeResult& Probe, const FVector& ProbeStart, const FColor& HitColor)
		{
			const FVector ProbeEnd = ProbeStart + Forward * TraceDistance;
			const FColor TraceColor = Probe.bHit ? HitColor : FColor(120, 120, 120);
			DrawDebugLine(
				World,
				ProbeStart,
				ProbeEnd,
				TraceColor,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0,
				Probe.bHit ? 1.0f : 0.5f
			);
			DrawDebugPoint(
				World,
				Probe.bHit ? Probe.Hit.ImpactPoint : ProbeEnd,
				DebugPointSize,
				TraceColor,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0
			);
			if (Probe.bHit)
			{
				DrawDebugDirectionalArrow(
					World,
					Probe.Hit.ImpactPoint,
					Probe.Hit.ImpactPoint + Probe.Hit.Normal * 10.0f,
					4.0f,
					FColor::Green,
					false,
					WeaponValues->WallOffsetDebugDrawTime,
					0,
					0.75f
				);
			}
		};

		DrawProbe(CenterProbe, BaseTraceStart, FColor::Red);
		DrawProbe(UpperProbe, BaseTraceStart + Up * VerticalProbeOffset, FColor::Blue);
		DrawProbe(LowerProbe, BaseTraceStart - Up * VerticalProbeOffset, FColor::Cyan);
		DrawProbe(RightProbe, BaseTraceStart + Right * SideProbeOffset, FColor::Yellow);
		DrawProbe(LeftProbe, BaseTraceStart - Right * SideProbeOffset, FColor(255, 128, 0));

		const FColor MuzzleBlockColor = MuzzleBlockProbe.bHit ? FColor::Magenta : FColor(180, 80, 180);
		DrawDebugLine(
			World,
			MuzzleBlockTraceStart,
			MuzzleBlockTraceEnd,
			MuzzleBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0,
			MuzzleBlockProbe.bHit ? 1.5f : 0.75f
		);
		DrawDebugPoint(
			World,
			MuzzleBlockProbe.bHit ? MuzzleBlockProbe.Hit.ImpactPoint : MuzzleBlockTraceEnd,
			DebugPointSize + 2.0f,
			MuzzleBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0
		);
		if (MuzzleBlockProbe.bHit)
		{
			DrawDebugDirectionalArrow(
				World,
				MuzzleBlockProbe.Hit.ImpactPoint,
				MuzzleBlockProbe.Hit.ImpactPoint + MuzzleBlockProbe.Hit.Normal * 12.0f,
				4.0f,
				FColor::Purple,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0,
				1.0f
			);
		}
		const FColor BarrelBlockColor = BarrelBlockProbe.bHit ? FColor::Orange : FColor(160, 100, 40);
		DrawDebugLine(
			World,
			BarrelBlockTraceStart,
			BarrelBlockTraceEnd,
			BarrelBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0,
			BarrelBlockProbe.bHit ? 1.25f : 0.6f
		);
		DrawDebugPoint(
			World,
			BarrelBlockProbe.bHit ? BarrelBlockProbe.Hit.ImpactPoint : BarrelBlockTraceEnd,
			DebugPointSize + 1.0f,
			BarrelBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0
		);
		DrawDebugString(
			World,
			Start + Forward * 45.0f + Up * 18.0f,
			FString::Printf(
				TEXT("Wall Target U %.2f D %.2f C %.2f S %.2f %.0f M %.2f B %.2f HS %.2f VC %.2f TE %.2f P %.2f PB %.2f\nWall Blend U %.2f D %.2f C %.2f S %.2f %.2f M %.2f HS %.2f HSS %.2f VC %.2f TE %.2f"),
				WallUpTargetAlpha,
				WallDownTargetAlpha,
				WallCeilingTargetAlpha,
				WallSideTargetAlpha,
				WallSideTargetSign,
				WallMuzzleBlockTargetAlpha,
				BarrelBlockTargetAlpha,
				WallHardStopTargetAlpha,
				WallVeryCloseTargetAlpha,
				WallTopEdgeTargetAlpha,
				WallMuzzleBlockDownPreferenceAlpha,
				PitchBlockAlpha,
				WallAvoidUpAlpha,
				WallAvoidDownAlpha,
				WallCeilingAlpha,
				WallAvoidSideAlpha,
				WallAvoidSideSign,
				WallMuzzleBlockAlpha,
				WallHardStopAlpha,
				WallHardStopSafetyAlpha,
				WallVeryCloseAlpha,
				WallTopEdgeAlpha
			),
			nullptr,
			WallTargetAlpha > 0.0f ? FColor::Red : FColor(120, 120, 120),
			WeaponValues->WallOffsetDebugDrawTime,
			false,
			1.0f
		);
	}

	// ── 9. 타깃 스무딩과 보간 ────────────────────────────────────────────
	// 타깃 스무딩은 상승 시 프로브 노이즈를 가라앉히기 위한 것. 하강에도
	// 적용하면 블렌드 아웃 보간 위에 지수 감쇠 꼬리가 하나 더 쌓이므로,
	// 릴리즈 시에는 스무딩 타깃을 즉시 떨어뜨린다. 마지막으로 SnapWallAlpha가
	// 점근 구간을 잘라 오프셋이 실제로 0에 도달하게 한다.
	const auto SmoothWallTarget = [](float Smoothed, float Target, float SmoothAlpha)
	{
		return Target < Smoothed ? Target : FMath::Lerp(Smoothed, Target, SmoothAlpha);
	};
	const auto SnapWallAlpha = [](float Alpha, float Target)
	{
		return (Target <= KINDA_SMALL_NUMBER && Alpha < KINDA_SMALL_NUMBER) ? 0.0f : Alpha;
	};

	const float WallTargetSmoothSpeed = FMath::Max(WeaponValues->WallTargetSmoothSpeed, 0.0f);
	const float WallTargetSmoothAlpha = 1.0f - FMath::Exp(-WallTargetSmoothSpeed * DeltaSeconds);
	WallSmoothedTargetAlpha = SmoothWallTarget(WallSmoothedTargetAlpha, WallTargetAlpha, WallTargetSmoothAlpha);
	WallUpSmoothedTargetAlpha = SmoothWallTarget(WallUpSmoothedTargetAlpha, WallUpTargetAlpha, WallTargetSmoothAlpha);
	WallDownSmoothedTargetAlpha = SmoothWallTarget(WallDownSmoothedTargetAlpha, WallDownTargetAlpha, WallTargetSmoothAlpha);
	WallSideSmoothedTargetAlpha = SmoothWallTarget(WallSideSmoothedTargetAlpha, WallSideTargetAlpha, WallTargetSmoothAlpha);

	const float MuzzleBlockTargetSmoothAlpha =
		1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallMuzzleBlockTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallMuzzleBlockSmoothedTargetAlpha =
		SmoothWallTarget(WallMuzzleBlockSmoothedTargetAlpha, WallMuzzleBlockTargetAlpha, MuzzleBlockTargetSmoothAlpha);

	const float VeryCloseTargetSmoothAlpha =
		1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallVeryCloseTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallVeryCloseSmoothedTargetAlpha =
		SmoothWallTarget(WallVeryCloseSmoothedTargetAlpha, WallVeryCloseTargetAlpha, VeryCloseTargetSmoothAlpha);

	const float InterpSpeed = WallSmoothedTargetAlpha > WallOffsetAlpha
		? WeaponValues->WallBlendInSpeed
		: WeaponValues->WallBlendOutSpeed;

	WallOffsetAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallOffsetAlpha,
		WallSmoothedTargetAlpha,
		DeltaSeconds,
		InterpSpeed
	), WallSmoothedTargetAlpha);

	WallAvoidUpAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallAvoidUpAlpha,
		WallUpSmoothedTargetAlpha,
		DeltaSeconds,
		WallUpSmoothedTargetAlpha > WallAvoidUpAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	), WallUpSmoothedTargetAlpha);
	WallAvoidDownAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallAvoidDownAlpha,
		WallDownSmoothedTargetAlpha,
		DeltaSeconds,
		WallDownSmoothedTargetAlpha > WallAvoidDownAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	), WallDownSmoothedTargetAlpha);
	WallAvoidSideAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallAvoidSideAlpha,
		WallSideSmoothedTargetAlpha,
		DeltaSeconds,
		WallSideSmoothedTargetAlpha > WallAvoidSideAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	), WallSideSmoothedTargetAlpha);
	WallAvoidSideSign = FMath::FInterpTo(
		WallAvoidSideSign,
		WallSideSmoothedTargetAlpha > KINDA_SMALL_NUMBER ? WallSideTargetSign : 0.0f,
		DeltaSeconds,
		WeaponValues->WallBlendInSpeed
	);
	if (WallMuzzleBlockTargetAlpha > KINDA_SMALL_NUMBER)
	{
		WallMuzzleBlockReleaseHoldTimer = FMath::Max(WeaponValues->WallMuzzleBlockReleaseHoldTime, 0.0f);
	}
	const float EffectiveMuzzleBlockTargetAlpha = WallMuzzleBlockReleaseHoldTimer > 0.0f
		? FMath::Max(WallMuzzleBlockSmoothedTargetAlpha, WallMuzzleBlockAlpha)
		: WallMuzzleBlockSmoothedTargetAlpha;
	WallMuzzleBlockAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallMuzzleBlockAlpha,
		EffectiveMuzzleBlockTargetAlpha,
		DeltaSeconds,
		EffectiveMuzzleBlockTargetAlpha > WallMuzzleBlockAlpha
			? WeaponValues->WallMuzzleBlockBlendInSpeed
			: WeaponValues->WallMuzzleBlockBlendOutSpeed
	), EffectiveMuzzleBlockTargetAlpha);
	const float HardStopTargetSmoothAlpha = 1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallHardStopTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallHardStopSmoothedTargetAlpha = SmoothWallTarget(WallHardStopSmoothedTargetAlpha, WallHardStopTargetAlpha, HardStopTargetSmoothAlpha);
	WallHardStopAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallHardStopAlpha,
		WallHardStopSmoothedTargetAlpha,
		DeltaSeconds,
		WallHardStopSmoothedTargetAlpha > WallHardStopAlpha
			? WeaponValues->WallHardStopBlendInSpeed
			: WeaponValues->WallHardStopBlendOutSpeed
	), WallHardStopSmoothedTargetAlpha);
	WallHardStopSafetyAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallHardStopSafetyAlpha,
		WallHardStopTargetAlpha,
		DeltaSeconds,
		WallHardStopTargetAlpha > WallHardStopSafetyAlpha
			? WeaponValues->WallHardStopSafetyBlendInSpeed
			: WeaponValues->WallHardStopSafetyBlendOutSpeed
	), WallHardStopTargetAlpha);
	WallVeryCloseAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallVeryCloseAlpha,
		WallVeryCloseSmoothedTargetAlpha,
		DeltaSeconds,
		WallVeryCloseSmoothedTargetAlpha > WallVeryCloseAlpha
			? WeaponValues->WallVeryCloseBlendInSpeed
			: WeaponValues->WallVeryCloseBlendOutSpeed
	), WallVeryCloseSmoothedTargetAlpha);

	// ── 10. 오프셋 합성 ──────────────────────────────────────────────────
	// 단계별 당김을 합산해 최종 뷰모델 위치/회전 오프셋을 만든다.
	// 근접 단계가 강해질수록 MuzzleBlock 항은 억제되어 이중 당김을 막는다
	const float EasedAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallOffsetAlpha, 2.0f);
	const float EasedMuzzleBlockAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockAlpha, 2.0f);
	const float EasedVeryCloseAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallVeryCloseAlpha, 2.0f);
	const float EasedHardStopAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallHardStopAlpha, 2.0f);
	const float EasedHardStopSafetyAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallHardStopSafetyAlpha, 2.0f);
	const float EasedFinalHardStopAlpha = FMath::Max(EasedHardStopAlpha, EasedHardStopSafetyAlpha);
	const float ActionWallAlpha = FMath::Clamp(ActionAlpha, 0.0f, 1.0f);
	const float ActionWallOffsetScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues->WallActionOffsetScale, 0.0f, 1.0f),
		ActionWallAlpha
	);
	const float ActionWallHardStopScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues->WallActionHardStopScale, 0.0f, 1.0f),
		ActionWallAlpha
	);
	const float EasedPoseAlpha = FMath::InterpEaseOut(
		0.0f,
		1.0f,
		FMath::Max(WallAvoidUpAlpha, WallAvoidDownAlpha),
		2.0f
	);
	const float WallStateSuppressAlpha = FMath::Clamp(
		FMath::Max3(EasedPoseAlpha, EasedVeryCloseAlpha, WallCeilingAlpha),
		0.0f,
		1.0f
	);
	const float EasedSideAlpha =
		FMath::InterpEaseInOut(0.0f, 1.0f, WallAvoidSideAlpha, 2.0f) *
		(1.0f - WallStateSuppressAlpha);
	const float SideCloseAlpha =
		EasedSideAlpha *
		FMath::InterpEaseIn(
			0.0f,
			1.0f,
			FMath::Clamp(FMath::Max(RawRightAlpha, RawLeftAlpha), 0.0f, 1.0f),
			2.0f
	);
	const float VeryCloseMuzzleOffsetSuppressScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleOffsetSuppressScale, 0.0f, 1.0f);
	const float MuzzleOffsetSuppressAlpha = FMath::Max(EasedVeryCloseAlpha * VeryCloseMuzzleOffsetSuppressScale, EasedFinalHardStopAlpha);
	const float MuzzleOffsetScale = FMath::Clamp(1.0f - MuzzleOffsetSuppressAlpha, 0.0f, 1.0f);

	const FVector WallNonHardStopOffsetLoc =
		(WeaponValues->WallMaxOffsetLoc * EasedAlpha) +
		(WeaponValues->WallMaxOffsetLoc * SideCloseAlpha * FMath::Clamp(WeaponValues->WallSideCloseOffsetScale, 0.0f, 1.0f)) +
		(WeaponValues->WallMuzzleBlockLoc * EasedMuzzleBlockAlpha * MuzzleOffsetScale) +
		(WeaponValues->WallVeryCloseLoc * EasedVeryCloseAlpha);
	ViewModelWallOffsetLoc =
		(WallNonHardStopOffsetLoc * ActionWallOffsetScale) +
		(WeaponValues->WallHardStopLoc * EasedFinalHardStopAlpha * ActionWallHardStopScale);
	const float VeryCloseMuzzleRotSuppressScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleRotSuppressScale, 0.0f, 1.0f);
	const float MuzzleRotSuppressAlpha = FMath::Max(EasedVeryCloseAlpha * VeryCloseMuzzleRotSuppressScale, EasedFinalHardStopAlpha);
	const float MuzzleRotScale = FMath::Clamp(1.0f - MuzzleRotSuppressAlpha, 0.0f, 1.0f);
	const FRotator WallNonHardStopOffsetRot =
		(WeaponValues->WallMaxOffsetRot * EasedAlpha) +
		(WeaponValues->WallSideOffsetRot * WallAvoidSideSign * EasedSideAlpha * FMath::Clamp(WeaponValues->WallSideRotationScale, 0.0f, 1.0f)) +
		(WeaponValues->WallMuzzleBlockRot * EasedMuzzleBlockAlpha * (1.0f - WallMuzzleBlockDownPreferenceAlpha) * MuzzleRotScale) +
		(WeaponValues->WallMuzzleBlockDownRot * EasedMuzzleBlockAlpha * WallMuzzleBlockDownPreferenceAlpha * MuzzleRotScale) +
		(WeaponValues->WallVeryCloseRot * EasedVeryCloseAlpha);
	ViewModelWallOffsetRot =
		(WallNonHardStopOffsetRot * ActionWallOffsetScale) +
		(WeaponValues->WallHardStopRot * EasedFinalHardStopAlpha * ActionWallHardStopScale);
	WallOffsetLoc = ViewModelWallOffsetLoc;
	WallOffsetRot = ViewModelWallOffsetRot;
}

float FWeaponWallAvoidanceState::GetShotBlendAlpha() const
{
	return FMath::Clamp(FMath::Max3(FMath::Max(WallOffsetAlpha, WallAvoidSideAlpha),
		FMath::Max(WallMuzzleBlockAlpha, WallVeryCloseAlpha),
		FMath::Max3(WallHardStopAlpha, WallHardStopSafetyAlpha, WallCeilingAlpha)), 0.0f, 1.0f);
}

float FWeaponWallAvoidanceState::GetAimBreakAlpha(const FWeaponValues& Values) const
{
	const float Start = FMath::Clamp(Values.WallAimBreakStartAlpha, 0.0f, 1.0f);
	const float Full = FMath::Max(FMath::Clamp(Values.WallAimBreakFullAlpha, 0.0f, 1.0f), Start + KINDA_SMALL_NUMBER);
	return FMath::GetMappedRangeValueClamped(FVector2D(Start, Full), FVector2D(0.0f, 1.0f),
		FMath::Max3(WallVeryCloseAlpha, WallHardStopAlpha, WallMuzzleBlockAlpha));
}

void FWeaponWallAvoidanceState::UpdateAim(bool bAiming, float DeltaSeconds, const FWeaponValues& Values)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		AimAlpha = 0.0f;
		return;
	}
	const float BreakAlpha = GetAimBreakAlpha(Values);
	AimAlpha = FMath::FInterpTo(AimAlpha, bAiming ? 1.0f - BreakAlpha : 0.0f, DeltaSeconds,
		FMath::Max(bAiming && BreakAlpha <= KINDA_SMALL_NUMBER ? Values.AimInterpSpeedIn : Values.AimInterpSpeedOut, 0.0f));
}

void FWeaponWallAvoidanceState::GetPoseAlphas(const FWeaponValues& Values, float ActionPoseScale,
	float& OutUp, float& OutDown) const
{
	const float Up = WallAvoidUpAlpha * ActionPoseScale;
	const float Down = WallAvoidDownAlpha * ActionPoseScale;
	const float Ceiling = WallCeilingAlpha * ActionPoseScale;
	const float Close = WallVeryCloseAlpha * ActionPoseScale;
	const float Suppression = FMath::Clamp(FMath::Max(
		FMath::InterpEaseOut(0.0f, 1.0f, Close, 2.0f) * Values.WallVeryClosePoseSuppressScale,
		FMath::Max(WallHardStopAlpha, WallHardStopSafetyAlpha)), 0.0f, 1.0f);
	const float MuzzleOnly = FMath::Clamp(WallMuzzleBlockAlpha * ActionPoseScale * (1.0f - Suppression), 0.0f, 1.0f);
	const float DownPreference = FMath::Clamp(WallMuzzleBlockDownPreferenceAlpha, 0.0f, 1.0f);
	OutUp = FMath::Clamp(FMath::Max(Up, MuzzleOnly * Values.WallMuzzleBlockUpPoseScale * (1.0f - DownPreference))
		* (1.0f - FMath::Clamp(Ceiling, 0.0f, 1.0f)) * (1.0f - Suppression) * Values.WallAvoidUpPoseAlphaScale, 0.0f, 1.0f);
	OutDown = FMath::Clamp(FMath::Max3(Down * (1.0f - Suppression),
		MuzzleOnly * Values.WallMuzzleBlockDownPoseScale * DownPreference, Ceiling * (1.0f - Suppression)), 0.0f, 1.0f);
}

bool FWeaponWallAvoidanceState::ResolveShotTransform(const FTransform& View,
	const FTransform& AvoidedMuzzle, FTransform& Out) const
{
	if (View.ContainsNaN() || AvoidedMuzzle.ContainsNaN()
		|| !View.GetRotation().IsNormalized() || !AvoidedMuzzle.GetRotation().IsNormalized())
	{
		return false;
	}
	const float Alpha = GetShotBlendAlpha();
	Out = FTransform(FQuat::Slerp(View.GetRotation(), AvoidedMuzzle.GetRotation(), Alpha).GetNormalized(),
		FMath::Lerp(View.GetLocation(), AvoidedMuzzle.GetLocation(), Alpha));
	return !Out.ContainsNaN();
}

bool FWeaponWallAvoidanceSnapshot::Reconcile(UWorld* World, const AActor* Shooter,
	const AActor* CurrentWeapon, const FWeaponValues& Values, double Now, double LastAppliedServerTime,
	TConstArrayView<FWeaponWallPredictionFrame> Frames, FWeaponWallAvoidanceState& OutState) const
{
	// 이전 장착/시각은 호출부에서도 걸러낸다. 여기서는 재생 가능한 시간 범위를 확인한다.
	if (!World || !FMath::IsFinite(ServerTime) || !FMath::IsFinite(Now)
		|| ServerTime <= LastAppliedServerTime || ServerTime > Now + 0.05 || Now - ServerTime > 0.75
		|| State.ViewModelWallOffsetLoc.ContainsNaN() || State.ViewModelWallOffsetRot.ContainsNaN()
		|| State.WallOffsetLoc.ContainsNaN() || State.WallOffsetRot.ContainsNaN()) return false;
	const float Scalars[] =
	{
		State.AimAlpha, State.WallOffsetAlpha, State.WallMuzzleBlockAlpha,
		State.WallAvoidUpAlpha, State.WallAvoidDownAlpha, State.WallAvoidSideAlpha,
		State.WallAvoidSideSign, State.WallTargetAlpha, State.WallSmoothedTargetAlpha,
		State.WallUpTargetAlpha, State.WallUpSmoothedTargetAlpha, State.WallDownTargetAlpha,
		State.WallDownSmoothedTargetAlpha, State.WallSideTargetAlpha, State.WallSideSmoothedTargetAlpha,
		State.WallSideTargetSign, State.WallMuzzleBlockTargetAlpha, State.WallMuzzleBlockSmoothedTargetAlpha,
		State.WallVeryCloseTargetAlpha, State.WallVeryCloseOwnTargetAlpha, State.WallVeryCloseSmoothedTargetAlpha,
		State.WallVeryCloseAlpha, State.WallTopEdgeTargetAlpha, State.WallTopEdgeAlpha,
		State.WallCeilingTargetAlpha, State.WallCeilingAlpha, State.WallHardStopTargetAlpha,
		State.WallHardStopOwnTargetAlpha, State.WallHardStopSmoothedTargetAlpha, State.WallHardStopAlpha,
		State.WallHardStopSafetyAlpha, State.WallMuzzleBlockDownPreferenceTargetAlpha, State.WallMuzzleBlockDownPreferenceAlpha,
		State.WallMuzzleBlockReleaseHoldTimer
	};
	for (float Scalar : Scalars) if (!FMath::IsFinite(Scalar)) return false;
	FWeaponWallAvoidanceState Corrected = State;
	double ReplayedUntil = ServerTime;
	for (const FWeaponWallPredictionFrame& Frame : Frames)
	{
		if (!FMath::IsFinite(Frame.ServerTime) || !FMath::IsFinite(Frame.DeltaSeconds)
			|| Frame.ServerTime > Now + 0.05 || Frame.DeltaSeconds < 0.0f || Frame.DetectionOrigin.ContainsNaN() || Frame.AimRotation.ContainsNaN()) return false;
		if (Frame.ServerTime <= ServerTime) continue;
		const double FrameStart = Frame.ServerTime - Frame.DeltaSeconds;
		// 첫 보정이 로컬 장착보다 먼저 생성됐다면 그 앞 구간의 입력은 없다.
		// 최초로 확보한 입력을 유지한 것으로 제한된 시간만 진행하고, 이후에는 기록된 입력을 재생한다.
		while (FrameStart > ReplayedUntil + 0.0001)
		{
			const float MissingStep = float(FMath::Min(FrameStart - ReplayedUntil, 1.0 / 30.0));
			Corrected.Update(World, Shooter, CurrentWeapon, Frame.DetectionOrigin, Frame.AimRotation, MissingStep, &Values);
			Corrected.UpdateAim(Frame.bAiming, MissingStep, Values);
			ReplayedUntil += MissingStep;
		}
		const float ReplaySeconds = FMath::Clamp(float(Frame.ServerTime - FMath::Max(FrameStart, ReplayedUntil)), 0.0f, Frame.DeltaSeconds);
		// 입력 시각은 재생하지만 충돌은 현재 클라이언트 월드다. 과거 서버 월드의 복원이 아니다.
		Corrected.Update(World, Shooter, CurrentWeapon, Frame.DetectionOrigin, Frame.AimRotation, ReplaySeconds, &Values);
		Corrected.UpdateAim(Frame.bAiming, ReplaySeconds, Values);
		ReplayedUntil = Frame.ServerTime;
	}
	if (Now - ReplayedUntil > 0.05) return false;
	OutState = Corrected;
	return true;
}

bool FWeaponWallAvoidanceSnapshot::MatchesEquipment(const AActor* CurrentWeapon, uint32 CurrentRevision,
	const AActor* InvalidatedWeapon, uint32 InvalidatedRevision) const
{
	return Weapon == CurrentWeapon && EquipmentRevision == CurrentRevision
		&& !(Weapon == InvalidatedWeapon && EquipmentRevision == InvalidatedRevision);
}
