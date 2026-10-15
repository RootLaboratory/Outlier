#include "UI/BottomTTSSpeakerLayout.h"

#include "Components/TextBlock.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Layout/ArrangedChildren.h"
#include "Slate/WidgetTransform.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/SlateTextBlockLayout.h"

namespace
{
// UTextLayoutWidget은 이 설정들을 디자이너에 노출하지만 UE 5.7에는 C++ getter가 없다.
// 측정에도 설정된 margin, shaping, wrapping 정책이 반영되도록 기존 설정을 읽는다.
struct FTextLayoutProperties
{
	const FStructProperty* ShapedTextOptions = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	const FStructProperty* Margin = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FProperty* WrappingPolicy = FindFProperty<FProperty>(UTextLayoutWidget::StaticClass(), TEXT("WrappingPolicy"));
	const FProperty* Justification = FindFProperty<FProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FFloatProperty* LineHeight = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FBoolProperty* ApplyLineHeight = FindFProperty<FBoolProperty>(UTextLayoutWidget::StaticClass(), TEXT("ApplyLineHeightToBottomLine"));

	// 엔진 업그레이드로 이름이 바뀔 수 있다. 그 경우 첫 자막에서 크래시하지 않고 정렬을 건너뛴다.
	bool IsValid() const
	{
		return ShapedTextOptions && Margin && WrappingPolicy && Justification && LineHeight && ApplyLineHeight;
	}
};
}

FBottomTTSSpeakerLayout::FBottomTTSSpeakerLayout() = default;
FBottomTTSSpeakerLayout::~FBottomTTSSpeakerLayout() = default;

bool FBottomTTSSpeakerLayout::FindWidgetGeometry(const TSharedRef<SWidget>& Root, const FGeometry& RootGeometry,
	const SWidget* Target, FGeometry& OutGeometry, FGeometry& OutParentGeometry)
{
	if (&Root.Get() == Target)
	{
		OutGeometry = RootGeometry;
		OutParentGeometry = RootGeometry;
		return true;
	}
	FArrangedChildren Children(EVisibility::Visible);
	Root->ArrangeChildren(RootGeometry, Children);
	for (const FArrangedWidget& Child : Children.GetInternalArray())
	{
		if (&Child.Widget.Get() == Target)
		{
			OutGeometry = Child.Geometry;
			OutParentGeometry = RootGeometry;
			return true;
		}
		if (FindWidgetGeometry(Child.Widget, Child.Geometry, Target, OutGeometry, OutParentGeometry)) return true;
	}
	return false;
}

bool FBottomTTSSpeakerLayout::AlignSpeakerInTree(UTextBlock& Speaker, const UTextBlock& Text,
	const TSharedRef<SWidget>& Root, const FGeometry& RootGeometry, float Gap)
{
	const TSharedPtr<SWidget> ScriptWidget = Text.GetCachedWidget();
	const TSharedPtr<SWidget> SpeakerWidget = Speaker.GetCachedWidget();
	if (!ScriptWidget || !SpeakerWidget || Speaker.GetText().IsEmpty()) return false;
	FGeometry ScriptGeometry, SpeakerGeometry, SpeakerParentGeometry, ScriptParentGeometry;
	if (!FindWidgetGeometry(Root, RootGeometry, ScriptWidget.Get(), ScriptGeometry, ScriptParentGeometry)
		|| !FindWidgetGeometry(Root, RootGeometry, SpeakerWidget.Get(), SpeakerGeometry, SpeakerParentGeometry)) return false;
	FVector2D FirstLineOffset;
	if (!GetFirstLineOffset(Text, ScriptGeometry, FirstLineOffset)) return false;
	const FVector2D Translation = GetSpeakerTranslation(ScriptGeometry, FirstLineOffset, SpeakerGeometry,
		SpeakerParentGeometry, Speaker.GetRenderTransform(), Speaker.GetRenderTransformPivot(), Gap);
	if (!Translation.Equals(Speaker.GetRenderTransform().Translation, 0.01)) Speaker.SetRenderTranslation(Translation);
	return true;
}

bool FBottomTTSSpeakerLayout::GetFirstLineOffset(const UTextBlock& Text, const FGeometry& Geometry, FVector2D& OutOffset)
{
	const TSharedPtr<SWidget> Widget = Text.GetCachedWidget();
	const FVector2D LocalSize = Geometry.GetLocalSize();
	const float Scale = Geometry.GetAccumulatedLayoutTransform().GetScale();
	if (!Widget || Text.GetText().IsEmpty() || LocalSize.X < 1.0 || Scale <= UE_SMALL_NUMBER)
	{
		return false;
	}

	static const FTextLayoutProperties Properties;
	if (!Properties.IsValid())
	{
		static bool bWarned = false;
		UE_CLOG(!bWarned, LogTemp, Warning, TEXT("[BottomTTS] UTextLayoutWidget properties not found; speaker alignment disabled."));
		bWarned = true;
		return false;
	}
	const FShapedTextOptions& Shaping = *Properties.ShapedTextOptions->ContainerPtrToValuePtr<FShapedTextOptions>(&Text);
	const FMargin& Margin = *Properties.Margin->ContainerPtrToValuePtr<FMargin>(&Text);
	const ETextWrappingPolicy Wrapping = *Properties.WrappingPolicy->ContainerPtrToValuePtr<ETextWrappingPolicy>(&Text);
	const ETextJustify::Type Justification = Properties.Justification->ContainerPtrToValuePtr<TEnumAsByte<ETextJustify::Type>>(&Text)->GetValue();
	const FTextBlockStyle Style = FTextBlockStyle().SetFont(Text.GetFont());
	if (!LayoutCache || Owner.Pin() != Widget)
	{
		Owner = Widget;
		LayoutCache = MakeUnique<FSlateTextBlockLayout>(Widget.Get(), Style, TOptional<ETextShapingMethod>(), TOptional<ETextFlowDirection>(),
			FCreateSlateTextLayout::CreateLambda([this](SWidget* InOwner, FTextBlockStyle InStyle)
			{
				TextLayout = FSlateTextLayout::Create(InOwner, MoveTemp(InStyle));
				return TextLayout.ToSharedRef();
			}), FPlainTextLayoutMarshaller::Create(), nullptr);
	}
	LayoutCache->SetTextShapingMethod(Shaping.bOverride_TextShapingMethod ? TOptional<ETextShapingMethod>(Shaping.TextShapingMethod) : TOptional<ETextShapingMethod>());
	LayoutCache->SetTextFlowDirection(Shaping.bOverride_TextFlowDirection ? TOptional<ETextFlowDirection>(Shaping.TextFlowDirection) : TOptional<ETextFlowDirection>());

	// 명시적 wrapping과 자동 wrapping이 모두 켜져 있으면 STextBlock은 더 작은 양수 폭을 선택한다.
	float WrapWidth = Text.GetWrapTextAt();
	if (Text.GetAutoWrapText())
	{
		WrapWidth = WrapWidth >= 1.0f ? FMath::Min(WrapWidth, static_cast<float>(LocalSize.X)) : LocalSize.X;
	}
	const FSlateTextBlockLayout::FWidgetDesiredSizeArgs Args(Text.GetText(), FText::GetEmpty(), WrapWidth, false,
		Wrapping, Text.GetTextTransformPolicy(), Margin, Properties.LineHeight->GetPropertyValue_InContainer(&Text),
		Properties.ApplyLineHeight->GetPropertyValue_InContainer(&Text), Justification);
	LayoutCache->ComputeDesiredSize(Args, Scale, Style);

	// 끊을 수 없는 줄이 할당 폭을 넘을 때 STextBlock의 paint 시점 justification과 스크롤에 맞춘다.
	FVector2D Scroll = FVector2D::ZeroVector;
	const float ExcessWidth = FMath::Max(0.0f, TextLayout->GetSize().X - static_cast<float>(LocalSize.X));
	if (TextLayout->GetVisualJustification() == ETextJustify::Center) Scroll.X = ExcessWidth * 0.5f;
	else if (TextLayout->GetVisualJustification() == ETextJustify::Right) Scroll.X = ExcessWidth;
	TextLayout->SetVisibleRegion(LocalSize, Scroll * Scale);
	TextLayout->UpdateIfNeeded();
	if (TextLayout->GetLineViews().IsEmpty()) return false;
	OutOffset = TextLayout->GetLineViews()[0].Offset / Scale;
	return true;
}

FVector2D FBottomTTSSpeakerLayout::GetSpeakerTranslation(const FGeometry& ScriptGeometry, const FVector2D& FirstLineOffset,
	const FGeometry& SpeakerGeometry, const FGeometry& SpeakerParentGeometry,
	const FWidgetTransform& SpeakerTransform, const FVector2D& SpeakerPivot, float Gap)
{
	// scale-to-zero visibility 애니메이션 중에는 아직 역변환 가능한 layout geometry가 없다.
	if (SpeakerParentGeometry.GetAccumulatedLayoutTransform().GetScale() <= UE_SMALL_NUMBER
		|| SpeakerGeometry.GetAccumulatedLayoutTransform().GetScale() <= UE_SMALL_NUMBER)
	{
		return SpeakerTransform.Translation;
	}
	const FSlateLayoutTransform LocalLayout = Concatenate(SpeakerGeometry.GetAccumulatedLayoutTransform(),
		Inverse(SpeakerParentGeometry.GetAccumulatedLayoutTransform()));
	FWidgetTransform UntranslatedTransform = SpeakerTransform;
	UntranslatedTransform.Translation = FVector2D::ZeroVector;
	const FGeometry UntranslatedGeometry = SpeakerParentGeometry.MakeChild(SpeakerGeometry.GetLocalSize(), LocalLayout,
		UntranslatedTransform.ToSlateRenderTransform(), SpeakerPivot);
	const FVector2D Target = SpeakerParentGeometry.AbsoluteToLocal(
		ScriptGeometry.LocalToAbsolute(FirstLineOffset - FVector2D(FMath::Max(0.0f, Gap), 0.0)));
	const FVector2D RightEdge = SpeakerParentGeometry.AbsoluteToLocal(
		UntranslatedGeometry.LocalToAbsolute(FVector2D(SpeakerGeometry.GetLocalSize().X, 0.0)));
	return (Target - RightEdge) / LocalLayout.GetScale();
}
