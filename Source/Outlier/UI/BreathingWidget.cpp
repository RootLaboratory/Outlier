#include "UI/BreathingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FName PhaseParameter(TEXT("PulsePhase"));
	const FName AmountParameter(TEXT("PulseAmount"));
	const FName ColorParameter(TEXT("PulseColor"));
	const FName AspectRatioParameter(TEXT("AspectRatio"));

	float SafeRange(float Value, float Fallback, float Min, float Max)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Fallback;
	}
}

TSharedRef<SWidget> UBreathingWidget::RebuildWidget()
{
	// Blueprint의 WidgetTree와 바인딩을 먼저 준비해야 기본 Image가 나중에 교체되지 않는다.
	Initialize();
	// 공통 WBP를 별도 Graph 없이 배치해도 사용할 수 있도록 기본 Image를 제공한다.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		BreathingImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BreathingImage"));
		WidgetTree->RootWidget = BreathingImage;
	}
	return Super::RebuildWidget();
}

void UBreathingWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyBreathingSettings();
}

void UBreathingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyBreathingSettings();
	LastGameTime = GetGameTime();
}

void UBreathingWidget::NativeDestruct()
{
	// 재부착할 때는 Construct에서 시각을 다시 잡아 화면 밖에 있던 시간을 누적하지 않는다.
	LastGameTime = 0.0;
	Super::NativeDestruct();
}

void UBreathingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (DynamicMaterial && BreathingImage && IsVisible() && BreathingImage->IsVisible())
	{
		UpdateAspectRatio(BreathingImage->GetCachedGeometry().GetLocalSize());
		if (!IsDesignTime())
		{
			UpdatePlayback();
		}
	}
	else
	{
		LastGameTime = GetGameTime();
	}
}

void UBreathingWidget::ApplyBreathingSettings()
{
	if (!BreathingImage)
	{
		return;
	}
	UMaterialInterface* Material = BreathingMaterial ? BreathingMaterial.Get()
		: Cast<UMaterialInterface>(BreathingImage->GetBrush().GetResourceObject());
	// 같은 MID를 매번 감싸지 않는다. 소스가 바뀔 때만 이 위젯 전용 MID를 새로 만든다.
	if (Material != DynamicMaterial && (Material != SourceMaterial || !DynamicMaterial))
	{
		DynamicMaterial = nullptr;
		SourceMaterial = Material;
		CachedAspectRatio = -1.0f;
		float ScalarValue = 0.0f;
		FLinearColor ColorValue;
		if (Material
			&& Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PulsePhase")), ScalarValue)
			&& Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PulseAmount")), ScalarValue)
			&& Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("PulseColor")), ColorValue))
		{
			DynamicMaterial = UMaterialInstanceDynamic::Create(Material, this);
			BreathingImage->SetBrushFromMaterial(DynamicMaterial);
		}
	}
	BreathingImage->SetVisibility(DynamicMaterial ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!DynamicMaterial)
	{
		return;
	}
	DynamicMaterial->SetScalarParameterValue(TEXT("Shape"), static_cast<float>(Shape));
	DynamicMaterial->SetScalarParameterValue(TEXT("Padding"), SafeRange(MaskPadding, 0.06f, 0.0f, 0.49f));
	DynamicMaterial->SetScalarParameterValue(TEXT("Feather"), SafeRange(Feather, 0.12f, 0.001f, 1.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("CornerRadius"), SafeRange(CornerRadius, 0.12f, 0.0f, 0.5f));
	DynamicMaterial->SetScalarParameterValue(TEXT("BaseOpacity"), SafeRange(BaseOpacity, 0.08f, 0.0f, 1.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("SlowPeakOpacity"), SafeRange(SlowPeakOpacity, 0.25f, 0.0f, 1.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("FastPeakOpacity"), SafeRange(FastPeakOpacity, 0.45f, 0.0f, 1.0f));
	UpdateAspectRatio(BreathingImage->GetCachedGeometry().GetLocalSize());
	ApplyPlayback();
}

void UBreathingWidget::SetBreathingState(FLinearColor Color, float Amount, float Period)
{
	PulseColor = Color;
	PulseAmount = SafeRange(Amount, 0.0f, 0.0f, 1.0f);
	PulsePeriod = FMath::IsFinite(Period) && Period > 0.0f ? Period : 2.0f;
	ApplyPlayback();
}

void UBreathingWidget::SetBreathingPlaying(bool bInPlaying)
{
	if (bPlaying != bInPlaying)
	{
		bPlaying = bInPlaying;
		LastGameTime = GetGameTime();
	}
}

void UBreathingWidget::ResetBreathing()
{
	Phase = 0.0f;
	LastGameTime = GetGameTime();
	ApplyPlayback();
}

void UBreathingWidget::FreezeBreathing(FLinearColor Color)
{
	SetBreathingPlaying(false);
	PulseColor = Color;
	ApplyPlayback();
}

double UBreathingWidget::GetGameTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UBreathingWidget::UpdatePlayback()
{
	const double Now = GetGameTime();
	const double Delta = FMath::Max(0.0, Now - LastGameTime);
	LastGameTime = Now;
	if (!bPlaying || !DynamicMaterial)
	{
		return;
	}
	const float Period = FMath::IsFinite(PulsePeriod) && PulsePeriod > 0.0f ? PulsePeriod : 2.0f;
	// 실제 게임 시각 차이만 누적한다. 일시정지는 멈추고 주기 변경 시에도 위상은 이어진다.
	Phase = static_cast<float>(FMath::Frac(Phase + Delta / Period));
	ApplyPlayback();
}

void UBreathingWidget::ApplyPlayback()
{
	if (!DynamicMaterial)
	{
		return;
	}
	DynamicMaterial->SetVectorParameterValue(ColorParameter, PulseColor);
	DynamicMaterial->SetScalarParameterValue(AmountParameter, SafeRange(PulseAmount, 0.0f, 0.0f, 1.0f));
	DynamicMaterial->SetScalarParameterValue(PhaseParameter, IsDesignTime()
		? SafeRange(PreviewPhase, 0.5f, 0.0f, 1.0f) : Phase);
}

void UBreathingWidget::UpdateAspectRatio(const FVector2D& ImageSize)
{
	if (!DynamicMaterial || !FMath::IsFinite(ImageSize.X) || !FMath::IsFinite(ImageSize.Y)
		|| ImageSize.X <= 0.0 || ImageSize.Y <= 0.0)
	{
		return;
	}
	const float AspectRatio = static_cast<float>(ImageSize.X / ImageSize.Y);
	if (!FMath::IsFinite(AspectRatio) || AspectRatio <= 0.0f || FMath::IsNearlyEqual(CachedAspectRatio, AspectRatio))
	{
		return;
	}
	CachedAspectRatio = AspectRatio;
	DynamicMaterial->SetScalarParameterValue(AspectRatioParameter, AspectRatio);
}
