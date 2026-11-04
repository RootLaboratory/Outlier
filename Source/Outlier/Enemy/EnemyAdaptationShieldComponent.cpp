#include "Enemy/EnemyAdaptationShieldComponent.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"

namespace EnemyAdaptationShieldParams
{
	// M_Master_Shield_Octagon_01 파라미터 이름. 에셋 철자를 그대로 따른다.
	const FName BaseColor(TEXT("Base_Color"));
	const FName PreviousColor(TEXT("Base_Color_Previous"));
	const FName ColorFill(TEXT("Color_Fill"));
	const FName ColorFillSoftness(TEXT("Color_Fill_Softness"));
	const FName ColorFillEdgeStrength(TEXT("Strength_Fill_Edge"));
	const FName EdgeStrength(TEXT("Strength_Edge"));
	const FName VertexNoiseStrength(TEXT("Strength_Base_VertexNoise"));
	const FName HitEmissiveStrength(TEXT("Strength_Emissive"));
	const FName Opacity(TEXT("Opacity"));
	const FName HitLocation[UEnemyAdaptationShieldComponent::HitSlotCount] = {
		FName(TEXT("Hit_Location_1")), FName(TEXT("Hit_Location_2"))};
	const FName HitEmissiveRadius[UEnemyAdaptationShieldComponent::HitSlotCount] = {
		FName(TEXT("Radius_Emissive_1")), FName(TEXT("Radius_Emissive_2"))};
	// R은 출렁임 세기, G는 출렁임 반경이다.
	const FName HitNoise[UEnemyAdaptationShieldComponent::HitSlotCount] = {
		FName(TEXT("Noise_1")), FName(TEXT("Noise_2"))};
}

UEnemyAdaptationShieldComponent::UEnemyAdaptationShieldComponent()
{
	// 연출용 외피는 스캔 및 가독성 외곽선에서 사용하는 본체에 포함하지 않는다.
	ComponentTags.AddUnique(TEXT("Outlier.StencilExclude"));
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bReceivesDecals = false;
}

void UEnemyAdaptationShieldComponent::OnRegister()
{
	Super::OnRegister();

	HitSlots.SetNum(HitSlotCount);
	ApplyShieldRadius();

	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	SetVisibility(false);
	if (IsPresentationEnabled())
	{
		EnsureDynamicMaterial();
		ApplyStaticParameters();
		ApplyAppearParameter();
		ApplyColorParameters();
		ClearHitSlots();
	}
}

bool UEnemyAdaptationShieldComponent::ShouldShowForState(EEnemyAdaptationState State) const
{
	return State != EEnemyAdaptationState::Normal
		&& static_cast<uint8>(State) >= static_cast<uint8>(MinVisibleState);
}

FLinearColor UEnemyAdaptationShieldComponent::GetColorForState(EEnemyAdaptationState State) const
{
	const FLinearColor* Color = StateColors.Find(State);
	return Color ? *Color : FallbackColor;
}

void UEnemyAdaptationShieldComponent::ApplyAdaptationState(EEnemyAdaptationState NewState)
{
	const bool bShouldShow = ShouldShowForState(NewState);
	const bool bWasActive = bShieldActive;
	if (bWasActive == bShouldShow && (!bShouldShow || DisplayedState == NewState))
	{
		return;
	}

	bShieldActive = bShouldShow;
	if (bShouldShow)
	{
		const FLinearColor NewColor = GetColorForState(NewState);
		// 이미 보이는 방어막이면 이전 단계 색 위로 새 색이 아래부터 차오른다.
		// 진행 중이던 채움은 그 목표 색으로 확정한 뒤 다음 채움을 시작한다.
		// 색이 같은 단계 전이(예: Preview→1단계)는 경계선만 지나가 보이므로 채우지 않는다.
		if (bWasActive && AppearAlpha > 0.0f && ColorFillDuration > KINDA_SMALL_NUMBER
			&& !NewColor.Equals(CurrentColor))
		{
			PreviousColor = CurrentColor;
			ColorFill = 0.0f;
		}
		else
		{
			// 새로 나타날 때는 등장 와이프가 연출을 맡으므로 색은 바로 새 단계로 맞춘다.
			PreviousColor = NewColor;
			ColorFill = 1.0f;
		}
		CurrentColor = NewColor;
		DisplayedState = NewState;
	}

	if (!IsPresentationEnabled())
	{
		return;
	}

	if (bShieldActive)
	{
		EnsureDynamicMaterial();
		SetVisibility(true);
	}
	ApplyColorParameters();
	RefreshTickState();
}

void UEnemyAdaptationShieldComponent::NotifyShieldHit(const FVector& WorldImpactLocation)
{
	if (!bShieldActive || !IsPresentationEnabled() || HitSlots.Num() != HitSlotCount)
	{
		return;
	}

	const int32 SlotIndex = NextHitSlot;
	NextHitSlot = (NextHitSlot + 1) % HitSlotCount;

	// 적이 움직여도 파문이 표면에 붙어 있도록 로컬 좌표로 보관한다.
	FHitSlot& Slot = HitSlots[SlotIndex];
	Slot.LocalLocation = GetComponentTransform().InverseTransformPosition(
		ProjectToShieldSurface(GetComponentLocation(), ShieldRadius, WorldImpactLocation));
	Slot.Elapsed = 0.0f;
	Slot.bActive = true;

	ApplyHitSlotParameters(SlotIndex);
	RefreshTickState();
}

void UEnemyAdaptationShieldComponent::PlayBreak(UNiagaraSystem* BreakVFX)
{
	// 서버의 Normal 복제가 먼저 와서 사라지는 중이어도 아직 화면에 남아 있으면 터뜨린다.
	const bool bWasVisible = IsVisible() && AppearAlpha > 0.0f;
	bShieldActive = false;
	if (!IsPresentationEnabled())
	{
		return;
	}

	if (bWasVisible && BreakVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			BreakVFX,
			GetComponentLocation(),
			GetComponentRotation(),
			GetComponentScale());
	}

	AppearAlpha = 0.0f;
	ColorFill = 1.0f;
	ApplyAppearParameter();
	ClearHitSlots();
	SetVisibility(false);
	RefreshTickState();
}

void UEnemyAdaptationShieldComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float TargetAlpha = bShieldActive ? 1.0f : 0.0f;
	if (AppearAlpha != TargetAlpha)
	{
		AppearAlpha = AppearDuration > KINDA_SMALL_NUMBER
			? FMath::FInterpConstantTo(AppearAlpha, TargetAlpha, DeltaTime, 1.0f / AppearDuration)
			: TargetAlpha;
		ApplyAppearParameter();
		if (!bShieldActive && AppearAlpha <= 0.0f)
		{
			ClearHitSlots();
			SetVisibility(false);
		}
	}

	if (ColorFill < 1.0f)
	{
		ColorFill = ColorFillDuration > KINDA_SMALL_NUMBER
			? FMath::Min(ColorFill + DeltaTime / ColorFillDuration, 1.0f)
			: 1.0f;
		ApplyColorParameters();
	}

	for (int32 SlotIndex = 0; SlotIndex < HitSlots.Num(); ++SlotIndex)
	{
		FHitSlot& Slot = HitSlots[SlotIndex];
		if (!Slot.bActive)
		{
			continue;
		}

		Slot.Elapsed += DeltaTime;
		if (Slot.Elapsed >= HitFadeDuration)
		{
			Slot.bActive = false;
		}
		ApplyHitSlotParameters(SlotIndex);
	}

	RefreshTickState();
}

FVector UEnemyAdaptationShieldComponent::ProjectToShieldSurface(
	const FVector& Center,
	float Radius,
	const FVector& Point)
{
	const FVector Offset = Point - Center;
	const FVector Direction = Offset.IsNearlyZero() ? FVector::UpVector : Offset.GetUnsafeNormal();
	return Center + Direction * FMath::Max(Radius, 0.0f);
}

float UEnemyAdaptationShieldComponent::ComputeHitFade(float Elapsed, float Duration)
{
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp(1.0f - Elapsed / Duration, 0.0f, 1.0f);
}

bool UEnemyAdaptationShieldComponent::IsPresentationEnabled() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void UEnemyAdaptationShieldComponent::EnsureDynamicMaterial()
{
	if (ShieldMID)
	{
		return;
	}

	if (ShieldMaterial)
	{
		ShieldMID = UMaterialInstanceDynamic::Create(ShieldMaterial, this);
		SetMaterial(0, ShieldMID);
	}
	else if (UMaterialInterface* CurrentMaterial = GetMaterial(0))
	{
		ShieldMID = CreateAndSetMaterialInstanceDynamicFromMaterial(0, CurrentMaterial);
	}
}

void UEnemyAdaptationShieldComponent::ApplyShieldRadius()
{
	const UStaticMesh* Mesh = GetStaticMesh();
	if (!Mesh)
	{
		return;
	}

	const float MeshRadius = Mesh->GetBounds().SphereRadius;
	if (MeshRadius > KINDA_SMALL_NUMBER)
	{
		SetRelativeScale3D(FVector(ShieldRadius / MeshRadius));
	}
}

void UEnemyAdaptationShieldComponent::ApplyStaticParameters()
{
	if (!ShieldMID)
	{
		return;
	}

	ShieldMID->SetScalarParameterValue(EnemyAdaptationShieldParams::ColorFillSoftness, ColorFillSoftness);
	ShieldMID->SetScalarParameterValue(EnemyAdaptationShieldParams::ColorFillEdgeStrength, ColorFillEdgeStrength);
	ShieldMID->SetScalarParameterValue(EnemyAdaptationShieldParams::EdgeStrength, EdgeStrength);
	ShieldMID->SetScalarParameterValue(EnemyAdaptationShieldParams::VertexNoiseStrength, VertexNoiseStrength);
	ShieldMID->SetScalarParameterValue(EnemyAdaptationShieldParams::HitEmissiveStrength, HitEmissiveStrength);
}

float UEnemyAdaptationShieldComponent::ComputeWipeOpacity(float LocalMinZ, float LocalMaxZ, float Alpha)
{
	// 머티리얼 Divide 노드의 상수. 이 높이(로컬 단위)만큼이 와이프 경계의 그라데이션 폭이다.
	constexpr float WipeGradientHeight = 64.0f;
	const float HiddenOpacity = 1.0f - LocalMinZ / WipeGradientHeight;
	const float VisibleOpacity = -LocalMaxZ / WipeGradientHeight;
	return FMath::Lerp(HiddenOpacity, VisibleOpacity, FMath::Clamp(Alpha, 0.0f, 1.0f));
}

void UEnemyAdaptationShieldComponent::ApplyAppearParameter()
{
	if (!ShieldMID)
	{
		return;
	}

	// 버텍스 노이즈가 바운드 밖으로 밀어낸 정점도 덮도록 위아래로 조금 여유를 둔다.
	float LocalMinZ = -ShieldRadius;
	float LocalMaxZ = ShieldRadius;
	if (const UStaticMesh* Mesh = GetStaticMesh())
	{
		const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
		const float Margin = MeshBounds.BoxExtent.Z * 0.1f;
		LocalMinZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z - Margin;
		LocalMaxZ = MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z + Margin;
	}
	ShieldMID->SetScalarParameterValue(
		EnemyAdaptationShieldParams::Opacity,
		ComputeWipeOpacity(LocalMinZ, LocalMaxZ, AppearAlpha));
}

void UEnemyAdaptationShieldComponent::ApplyColorParameters()
{
	if (ShieldMID)
	{
		ShieldMID->SetVectorParameterValue(EnemyAdaptationShieldParams::PreviousColor, PreviousColor);
		ShieldMID->SetVectorParameterValue(EnemyAdaptationShieldParams::BaseColor, CurrentColor);
		ShieldMID->SetScalarParameterValue(EnemyAdaptationShieldParams::ColorFill, ColorFill);
	}
}

void UEnemyAdaptationShieldComponent::ApplyHitSlotParameters(int32 SlotIndex)
{
	if (!ShieldMID || !HitSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	const FHitSlot& Slot = HitSlots[SlotIndex];
	const float Fade = Slot.bActive ? ComputeHitFade(Slot.Elapsed, HitFadeDuration) : 0.0f;
	ShieldMID->SetVectorParameterValue(
		EnemyAdaptationShieldParams::HitLocation[SlotIndex],
		FLinearColor(GetComponentTransform().TransformPosition(Slot.LocalLocation)));
	ShieldMID->SetScalarParameterValue(
		EnemyAdaptationShieldParams::HitEmissiveRadius[SlotIndex],
		HitEmissiveRadius * Fade);
	ShieldMID->SetVectorParameterValue(
		EnemyAdaptationShieldParams::HitNoise[SlotIndex],
		FLinearColor(HitNoiseStrength * Fade, HitNoiseRadius, 0.0f, 1.0f));
}

void UEnemyAdaptationShieldComponent::ClearHitSlots()
{
	for (int32 SlotIndex = 0; SlotIndex < HitSlots.Num(); ++SlotIndex)
	{
		HitSlots[SlotIndex] = FHitSlot();
		ApplyHitSlotParameters(SlotIndex);
	}
	NextHitSlot = 0;
}

void UEnemyAdaptationShieldComponent::RefreshTickState()
{
	const bool bAppearing = AppearAlpha != (bShieldActive ? 1.0f : 0.0f);
	const bool bFillingColor = bShieldActive && ColorFill < 1.0f;
	const bool bHasActiveHit = HitSlots.ContainsByPredicate(
		[](const FHitSlot& Slot) { return Slot.bActive; });
	SetComponentTickEnabled(bAppearing || bFillingColor || bHasActiveHit);
}
