#include "SLightmapBakeReadiness.h"

#include "AssetRegistry/AssetData.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LightmapBakeReadiness"

void SLightmapBakeReadiness::Construct(const FArguments& Args)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Hint",
			"실제 레벨의 마지막 활성 Perspective 뷰포트를 기준으로 검사합니다. 카메라/레벨/설정 변경 후 다시 검사하세요.\n환경 검사이며, UV 품질·PP Volume·노출·조명 배치까지 검증하지는 않습니다."))]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(SButton).Text(LOCTEXT("Refresh", "BWYS 준비 상태 / 카메라 검사")).OnClicked(this, &SLightmapBakeReadiness::Refresh)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
			[SNew(SButton).Text(LOCTEXT("Open", "GPU Lightmass 열기"))
				.ToolTipText(LOCTEXT("OpenHint", "엔진 기본 패널을 엽니다. 플러그인 활성화와 재시작은 직접 해야 합니다. 베이크는 자동 실행하지 않습니다."))
				.IsEnabled_Lambda([]() { return LightmapBake::CanOpenNativePanel(); })
				.OnClicked(this, &SLightmapBakeReadiness::OpenPanel)]
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(SButton).Text(LOCTEXT("Export", "검사 보고서 저장"))
				.ToolTipText(LOCTEXT("ExportHint", "현재 환경과 카메라를 다시 검사하고 Saved/LightmapUVHelper/Readiness에 JSON으로 저장합니다."))
				.OnClicked(this, &SLightmapBakeReadiness::Export)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(SButton).Text(LOCTEXT("MeshScan", "크래시 후보 메시 검사"))
				.ToolTipText(LOCTEXT("MeshScanHint", "현재 로드된 레벨의 SM·ISM·HISM을 읽고 GPULM 예상 배치와 Ray Tracing Proxy 섹션 수를 비교합니다. 베이크/에셋 변경은 하지 않습니다."))
				.OnClicked(this, &SLightmapBakeReadiness::ScanMeshes)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
			[SNew(SButton).Text(LOCTEXT("ExportMeshes", "메시 진단 보고서 저장"))
				.OnClicked(this, &SLightmapBakeReadiness::ExportMeshes)]
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(CheckRows, SVerticalBox)]]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(Status); })]
	];
	Status = TEXT("준비 상태 검사를 눌러 현재 환경을 확인하세요. 프로젝트 설정 변경·베이크 실행은 하지 않습니다.");
}

FReply SLightmapBakeReadiness::Refresh()
{
	Report = LightmapBake::Evaluate(LightmapBake::CaptureEnvironment());
	CheckRows->ClearChildren();
	int32 Blocked = 0;
	for (const FLightmapBakeCheck& Check : Report.Checks)
	{
		const bool bBlocked = Check.State == ELightmapBakeCheck::Blocked;
		Blocked += bBlocked ? 1 : 0;
		const TCHAR* Prefix = bBlocked ? TEXT("[확인 필요] ") : Check.State == ELightmapBakeCheck::Warning ? TEXT("[주의] ") : TEXT("[통과] ");
		const FLinearColor Color = bBlocked ? FLinearColor(1.0f, 0.45f, 0.35f)
			: Check.State == ELightmapBakeCheck::Warning ? FLinearColor(1.0f, 0.8f, 0.3f) : FLinearColor(0.5f, 0.85f, 0.5f);
		CheckRows->AddSlot().AutoHeight().Padding(4, 2)
		[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Color)
			.Text(FText::FromString(FString(Prefix) + Check.Name + TEXT(" — ") + Check.Detail))];
	}
	Status = Report.IsEnvironmentReady()
		? TEXT("기본 환경 조건 통과. 실제 베이크는 실행하지 않았습니다. 엔진 GPU Lightmass 패널에서 Mode = Bake What You See를 선택해 검수할 수 있습니다.")
		: FString::Printf(TEXT("기본 환경 조건 중 %d개 확인 필요. 프로젝트 설정은 변경하지 않았고 베이크도 실행하지 않았습니다."), Blocked);
	return FReply::Handled();
}

FReply SLightmapBakeReadiness::OpenPanel()
{
	Status = LightmapBake::OpenNativePanel()
		? TEXT("엔진 GPU Lightmass 패널을 열었습니다. 이 도구는 모드/설정 변경이나 베이크 실행을 하지 않았습니다.")
		: TEXT("GPU Lightmass 패널을 열 수 없습니다. 플러그인 활성화·재시작과 레벨 에디터 상태를 확인하세요.");
	return FReply::Handled();
}

FReply SLightmapBakeReadiness::Export()
{
	Refresh();
	FString Path;
	Status = LightmapBake::SaveReport(Report, Path) ? TEXT("검사 보고서 저장: ") + Path : TEXT("보고서 저장 실패: ") + Path;
	return FReply::Handled();
}

FReply SLightmapBakeReadiness::ScanMeshes()
{
	MeshReport = LightmapMeshDiagnostics::ScanEditorWorld();
	CheckRows->ClearChildren();
	if (!MeshReport.Error.IsEmpty()) { Status = MeshReport.Error; return FReply::Handled(); }
	CheckRows->AddSlot().AutoHeight().Padding(4)
	[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("DiagnosticScope",
		"로드된 SM·ISM·HISM의 모든 렌더 LOD를 검사합니다. 후보가 곧 원인 확정은 아닙니다. -1은 RHI 미상주/확인 불가입니다. Landscape와 언로드된 액터는 제외됩니다."))];
	int32 Shown = 0;
	for (const FLightmapMeshDiagnostic& Entry : MeshReport.Entries)
	{
		if (!Entry.HasRisk()) continue;
		if (Shown++ >= 50) break;
		CheckRows->AddSlot().AutoHeight().Padding(4, 6)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Entry.bCountMismatch || Entry.bIndexMismatch
				? FLinearColor(1, 0.45f, 0.35f) : FLinearColor(1, 0.8f, 0.3f))
				.Text(FText::FromString(Entry.Description()))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("SelectActor", "액터 선택"))
					.OnClicked_Lambda([Component = Entry.Component]()
					{
						if (Component.IsValid() && Component->GetOwner() && GEditor)
						{
							GEditor->SelectNone(false, true);
							GEditor->SelectActor(Component->GetOwner(), true, true);
						}
						return FReply::Handled();
					})]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
				[SNew(SButton).Text(LOCTEXT("FindMesh", "콘텐츠에서 찾기"))
					.OnClicked_Lambda([Component = Entry.Component]()
					{
						if (Component.IsValid() && Component->GetStaticMesh())
						{
							FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get()
								.SyncBrowserToAssets(TArray<FAssetData>{FAssetData(Component->GetStaticMesh())});
						}
						return FReply::Handled();
					})]
			]
		];
	}
	Status = FString::Printf(TEXT("컴포넌트 %d / 고유 메시 %d / 후보 LOD 항목 %d. 화면에는 최대 50개, 보고서에는 전체 결과가 저장됩니다.%s"),
		MeshReport.Components, MeshReport.UniqueMeshes, MeshReport.RiskCount(), MeshReport.bCancelled ? TEXT(" 취소됨: 일부 검사 결과입니다.") : TEXT(" 에셋 변경·베이크 실행 없음."));
	return FReply::Handled();
}

FReply SLightmapBakeReadiness::ExportMeshes()
{
	if (MeshReport.Map.IsEmpty()) { Status = TEXT("크래시 후보 메시 검사를 먼저 실행하세요."); return FReply::Handled(); }
	FString Path;
	Status = LightmapMeshDiagnostics::Save(MeshReport, Path) ? TEXT("메시 진단 보고서 저장: ") + Path : TEXT("보고서 저장 실패: ") + Path;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
