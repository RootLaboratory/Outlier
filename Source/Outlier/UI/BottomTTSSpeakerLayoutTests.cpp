#if WITH_DEV_AUTOMATION_TESTS

#include "UI/BottomTTSSpeakerLayout.h"
#include "UI/BottomTTSSpeakerHost.h"

#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/WidgetTransform.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottomTTSFirstLineTest, "Outlier.UI.BottomTTS.FirstLineLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBottomTTSFirstLineTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized())) return false;
	UTextBlock* Text = NewObject<UTextBlock>();
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 24));
	Text->SetShadowOffset(FVector2D::ZeroVector);
	Text->SetJustification(ETextJustify::Center);
	Text->SetText(FText::FromString(TEXT("Hi\nThis second line is much longer.")));
	Text->TakeWidget();
	FBottomTTSSpeakerLayout Layout;
	FVector2D Offset;
	const FGeometry Wide = FGeometry::MakeRoot(FVector2D(500.0, 100.0), FSlateLayoutTransform());
	if (!TestTrue(TEXT("A first rendered line is available"), Layout.GetFirstLineOffset(*Text, Wide, Offset))) return false;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float FirstWidth = Measure->Measure(TEXT("Hi"), Text->GetFont()).X;
	TestTrue(TEXT("The short first line, not the longest line, determines the centered start"),
		FMath::IsNearlyEqual(Offset.X, (500.0 - FirstWidth) * 0.5, 1.0));
	TestTrue(TEXT("The speaker follows the top line"), FMath::IsNearlyZero(Offset.Y));

	Text->SetText(FText::FromString(TEXT("가나다라마바사아자차카타파하가나다라마바사아자차카타파하")));
	Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
	Text->SetAutoWrapText(true);
	const FGeometry Narrow = FGeometry::MakeRoot(FVector2D(100.0, 200.0), FSlateLayoutTransform());
	if (!TestTrue(TEXT("Korean text automatically wraps"), Layout.GetFirstLineOffset(*Text, Narrow, Offset))) return false;
	const FVector2D AutoOffset = Offset;
	TestTrue(TEXT("Automatic wrapping centers the rendered first line within the narrow area"), Offset.X >= -1.0 && Offset.X < 25.0);
	Text->SetAutoWrapText(false);
	Text->SetWrapTextAt(100.0f);
	Layout.GetFirstLineOffset(*Text, Narrow, Offset);
	TestTrue(TEXT("Automatic and explicit wrapping produce the same first line at the same width"), Offset.Equals(AutoOffset, 0.1));

	Text->SetAutoWrapText(true);
	Text->SetWrapTextAt(0.0f);
	const FGeometry Resized = FGeometry::MakeRoot(FVector2D(200.0, 200.0), FSlateLayoutTransform());
	Layout.GetFirstLineOffset(*Text, Resized, Offset);
	const FVector2D ResizedOffset = Offset;
	TestTrue(TEXT("Resizing recomputes wrapping rather than retaining the old first line"), Offset.X >= -1.0 && Offset.X < 25.0);
	const FGeometry Scaled = FGeometry::MakeRoot(FVector2D(200.0, 200.0), FSlateLayoutTransform(1.5f));
	Layout.GetFirstLineOffset(*Text, Scaled, Offset);
	TestTrue(TEXT("DPI scaling preserves the local first-line position"), Offset.Equals(ResizedOffset, 1.0));

	Text->SetText(FText::GetEmpty());
	TestFalse(TEXT("Empty subtitles have no speaker attachment position"), Layout.GetFirstLineOffset(*Text, Wide, Offset));
	Text->SetText(FText::FromString(TEXT("Text")));
	TestFalse(TEXT("Unarranged widgets wait for valid geometry"), Layout.GetFirstLineOffset(*Text, FGeometry(), Offset));
	Text->ReleaseSlateResources(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottomTTSSpeakerPositionTest, "Outlier.UI.BottomTTS.SpeakerPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBottomTTSSpeakerPositionTest::RunTest(const FString& Parameters)
{
	FWidgetTransform ParentTransform;
	ParentTransform.Scale = FVector2D(1.2, 1.1);
	ParentTransform.Angle = 8.0f;
	const FGeometry Parent = FGeometry::MakeRoot(FVector2D(800.0, 200.0), FSlateLayoutTransform(1.5f, FVector2D(50.0, 40.0)),
		ParentTransform.ToSlateRenderTransform());
	const FGeometry Script = Parent.MakeChild(FVector2D(500.0, 100.0), FSlateLayoutTransform(FVector2D(140.0, 0.0)));
	const FVector2D FirstLineOffset(180.0, 5.0);
	const FVector2D Pivot(0.5, 0.5);
	const FSlateLayoutTransform SpeakerLayout(0.9f, FVector2D(0.0, 25.0));
	FWidgetTransform SpeakerTransform;
	SpeakerTransform.Scale = FVector2D(1.1, 1.1);
	SpeakerTransform.Translation = FVector2D(20.0, 10.0);
	FGeometry Speaker = Parent.MakeChild(FVector2D(85.0, 30.0), SpeakerLayout, SpeakerTransform.ToSlateRenderTransform(), Pivot);
	const FVector2D Translation = FBottomTTSSpeakerLayout::GetSpeakerTranslation(Script, FirstLineOffset, Speaker, Parent,
		SpeakerTransform, Pivot, 12.0f);
	SpeakerTransform.Translation = Translation;
	Speaker = Parent.MakeChild(FVector2D(85.0, 30.0), SpeakerLayout, SpeakerTransform.ToSlateRenderTransform(), Pivot);
	const FVector2D SpeakerRightInScript = Script.AbsoluteToLocal(Speaker.LocalToAbsolute(FVector2D(85.0, 0.0)));
	TestTrue(TEXT("The gap is 12 widget units despite parent transforms and DPI"),
		FMath::IsNearlyEqual(SpeakerRightInScript.X, FirstLineOffset.X - 12.0, 0.01));
	TestTrue(TEXT("The speaker's top follows the first line"), FMath::IsNearlyEqual(SpeakerRightInScript.Y, FirstLineOffset.Y, 0.01));
	const FVector2D Repeated = FBottomTTSSpeakerLayout::GetSpeakerTranslation(Script, FirstLineOffset, Speaker, Parent,
		SpeakerTransform, Pivot, 12.0f);
	TestTrue(TEXT("Repeated frames do not accumulate translation or oscillate"), Repeated.Equals(Translation, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottomTTSFirstFrameTest, "Outlier.UI.BottomTTS.FirstFrameAndTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBottomTTSFirstFrameTest::RunTest(const FString& Parameters)
{
	UTextBlock* Speaker = NewObject<UTextBlock>();
	UTextBlock* Text = NewObject<UTextBlock>();
	Speaker->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 24));
	Text->SetFont(Speaker->GetFont());
	Text->SetJustification(ETextJustify::Center);
	Text->SetAutoWrapText(true);
	Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
	Speaker->SetText(FText::FromString(TEXT("Partner:")));
	Text->SetText(FText::FromString(TEXT("Hi\nThe second line is much longer.")));
	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[Speaker->TakeWidget()]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Top)[Text->TakeWidget()];
	FBottomTTSSpeakerLayout Layout;
	const TSharedRef<SBottomTTSSpeakerHost> Host = SNew(SBottomTTSSpeakerHost)
		.OnArrangeSubtitle(FOnArrangeBottomTTS::CreateLambda([&](const TSharedRef<SWidget>& Content, const FGeometry& Geometry)
		{
			if (Geometry.GetLocalSize().X < 1.0) return;
			TestTrue(FString::Printf(TEXT("Pre-paint alignment at width %.1f for speaker '%s'"),
				Geometry.GetLocalSize().X, *Speaker->GetText().ToString()),
				Layout.AlignSpeakerInTree(*Speaker, *Text, Content, Geometry, 12.0f));
		}))
		[Row];
	TestTrue(TEXT("No previous-frame text geometry is available"), Text->GetCachedGeometry().GetLocalSize().X == 0.0);
	TestTrue(TEXT("No previous-frame speaker geometry is available"), Speaker->GetCachedGeometry().GetLocalSize().X == 0.0);
	auto CheckFrame = [&](const TCHAR* Label, float Width)
	{
		Host->SlatePrepass(1.0f);
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(Width, 150.0), FSlateLayoutTransform());
		FArrangedChildren Children(EVisibility::Visible);
		Host->ArrangeChildren(Geometry, Children);
		FGeometry TextGeometry, SpeakerGeometry, ParentGeometry;
		if (!TestTrue(Label, FBottomTTSSpeakerLayout::FindWidgetGeometry(Row, Children[0].Geometry,
			Text->GetCachedWidget().Get(), TextGeometry, ParentGeometry))) return;
		if (!TestTrue(Label, FBottomTTSSpeakerLayout::FindWidgetGeometry(Row, Children[0].Geometry,
			Speaker->GetCachedWidget().Get(), SpeakerGeometry, ParentGeometry))) return;
		FVector2D FirstLine;
		if (!TestTrue(TEXT("The current frame has a measurable first line"), Layout.GetFirstLineOffset(*Text, TextGeometry, FirstLine))) return;
		const FVector2D RightEdge = TextGeometry.AbsoluteToLocal(
			SpeakerGeometry.LocalToAbsolute(FVector2D(SpeakerGeometry.GetLocalSize().X, 0.0)));
		TestTrue(Label, RightEdge.Equals(FirstLine - FVector2D(12.0, 0.0), 0.1));
	};
	CheckFrame(TEXT("The very first frame is already aligned"), 600.0f);
	const FVector2D FirstTranslation = Speaker->GetRenderTransform().Translation;
	CheckFrame(TEXT("Repeated allocation does not drift"), 600.0f);
	TestTrue(TEXT("An unchanged subtitle keeps its position"), Speaker->GetRenderTransform().Translation.Equals(FirstTranslation, 0.01));
	Speaker->SetText(FText::FromString(TEXT("Longer speaker:")));
	Text->SetText(FText::FromString(TEXT("새 대사입니다.\n다음 줄은 더 길게 이어집니다.")));
	CheckFrame(TEXT("A new speaker width and subtitle are aligned on their first frame"), 700.0f);
	CheckFrame(TEXT("A viewport resize and automatic wrapping are aligned on that same frame"), 400.0f);
	Speaker->ReleaseSlateResources(true);
	Text->ReleaseSlateResources(true);
	return true;
}

#endif
