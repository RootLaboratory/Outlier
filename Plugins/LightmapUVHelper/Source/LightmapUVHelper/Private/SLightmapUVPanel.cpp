#include "SLightmapUVPanel.h"

#include "LightmapUVSettings.h"
#include "SLightmapMeshPreview.h"
#include "SLightmapBakeReadiness.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LightmapUVHelper"

void SLightmapUVPanel::Construct(const FArguments& InArgs)
{
	Settings.Reset(NewObject<ULightmapUVSettings>(GetTransientPackage()));
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(128);
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.bUpdatesFromSelection = false;
	Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	Details->SetObject(Settings.Get());
	Details->OnFinishedChangingProperties().AddSPLambda(this, [this](const FPropertyChangedEvent&) { InvalidatePlan(); });
	Lighting = LightmapUV::LightingStatus();
	Status = TEXT("폴더를 선택하고 메시 검색을 누르세요. 검색은 에셋을 변경하지 않습니다.");

	ChildSlot
	[
		SNew(SBorder).Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)
				[SAssignNew(PathBox, SEditableTextBox).Text(FText::FromString(TEXT("/Game")))
					.ToolTipText(LOCTEXT("PathHint", "디스크 경로가 아닌 콘텐츠 경로입니다. 예: /Game/Environment"))
					.OnTextChanged_Lambda([this](const FText&) { InvalidatePlan(); })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("Folder", "폴더 선택"))]
					.OnGetMenuContent(this, &SLightmapUVPanel::MakePathPicker)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[SNew(SCheckBox).IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bRecursive = S == ECheckBoxState::Checked; InvalidatePlan(); })
					[SNew(STextBlock).Text(LOCTEXT("Recursive", "하위 폴더 포함"))]]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("Scan", "메시 검색 / 새로고침")).OnClicked(this, &SLightmapUVPanel::Scan)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)
				[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Lighting); }).AutoWrapText(true)]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("Lighting", "조명 상태 새로고침")).OnClicked(this, &SLightmapUVPanel::RefreshLighting)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SExpandableArea).InitiallyCollapsed(true)
				.HeaderContent()[SNew(STextBlock).Text(LOCTEXT("BWYSReadiness", "GPU Lightmass / BWYS 연결·준비 상태 검사"))]
				.BodyContent()[SNew(SBox).HeightOverride(260)[SNew(SLightmapBakeReadiness)]]
			]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				SNew(SSplitter)
				+ SSplitter::Slot().Value(0.43f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[SNew(SSearchBox).HintText(LOCTEXT("Search", "메시 이름 또는 경로 검색"))
						.OnTextChanged_Lambda([this](const FText& T) { Search = T.ToString(); Refilter(); })]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).Text(LOCTEXT("All", "전체 체크")).OnClicked(this, &SLightmapUVPanel::SelectAll, true)]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
						[SNew(SButton).Text(LOCTEXT("None", "전체 해제")).OnClicked(this, &SLightmapUVPanel::SelectAll, false)]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bOnlyNeedsChange = S == ECheckBoxState::Checked; Refilter(); })
							[SNew(STextBlock).Text(LOCTEXT("NeedsChange", "변경 필요/오류만"))]]
					]
					+ SVerticalBox::Slot().FillHeight(1)
					[SAssignNew(List, SListView<TSharedPtr<FLightmapUVRow>>).ListItemsSource(&VisibleRows)
						.SelectionMode(ESelectionMode::Single).OnGenerateRow(this, &SLightmapUVPanel::MakeRow)
						.OnSelectionChanged(this, &SLightmapUVPanel::SelectMesh)]
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(this, &SLightmapUVPanel::Counts)]
				]
				+ SSplitter::Slot().Value(0.30f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(0.5f)
					[SAssignNew(MeshPreview, SLightmapMeshPreview)]
					+ SVerticalBox::Slot().AutoHeight().Padding(4)
					[SNew(STextBlock).Text(LOCTEXT("Controls", "에셋 확인용 미리보기\n마우스로 회전·이동·확대 | F: 메시 맞추기\n체크 = 적용 대상, 행 클릭 = 미리보기"))]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).Text(LOCTEXT("Focus", "메시 맞추기")).OnClicked_Lambda([this]() { MeshPreview->FocusMesh(); return FReply::Handled(); })]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
						[SNew(SButton).Text(LOCTEXT("Browse", "콘텐츠에서 찾기")).OnClicked_Lambda([this]()
						{
							if (Selected) FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<FAssetData>{Selected->Asset});
							return FReply::Handled();
						})]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).Text(LOCTEXT("Open", "메시 에디터")).OnClicked_Lambda([this]()
						{
							if (Selected && Selected->Mesh.IsValid()) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Selected->Mesh.Get());
							return FReply::Handled();
						})]
					]
					+ SVerticalBox::Slot().FillHeight(0.5f).Padding(4)
					[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Text(this, &SLightmapUVPanel::SelectedDescription).AutoWrapText(true)]]
				]
				+ SSplitter::Slot().Value(0.27f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(6)
					[SNew(STextBlock).Text(LOCTEXT("ApplyHint", "왼쪽 체크를 켠 항목만 변경합니다.\n사용 UV / 기본 해상도는 메시 전체에 적용됩니다.")).AutoWrapText(true)]
					+ SVerticalBox::Slot().FillHeight(1)[Details.ToSharedRef()]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("Plan", "1. 변경 미리보기 / 검사")).OnClicked(this, &SLightmapUVPanel::PreviewChanges)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
				[SNew(SButton).Text(LOCTEXT("Apply", "2. 체크한 메시 적용·재빌드")).IsEnabled(this, &SLightmapUVPanel::CanApply).OnClicked(this, &SLightmapUVPanel::ApplyChanges)]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("Save", "3. 변경 에셋 저장…")).OnClicked(this, &SLightmapUVPanel::SaveChanges)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Status); }).AutoWrapText(true)]
		]
	];
}

SLightmapUVPanel::~SLightmapUVPanel()
{
	if (Details) Details->OnFinishedChangingProperties().RemoveAll(this);
}

TSharedRef<SWidget> SLightmapUVPanel::MakePathPicker()
{
	FPathPickerConfig Config;
	Config.DefaultPath = PathBox->GetText().ToString();
	Config.OnPathSelected = FOnPathSelected::CreateLambda([this](const FString& Path) { PathBox->SetText(FText::FromString(Path)); });
	return SNew(SBox).WidthOverride(350).HeightOverride(400)
		[FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreatePathPicker(Config)];
}

FReply SLightmapUVPanel::Scan()
{
	if (GEditor->PlayWorld) { Status = TEXT("PIE를 종료한 뒤 검색하세요."); return FReply::Handled(); }
	FString Path = PathBox->GetText().ToString().TrimStartAndEnd();
	Path.RemoveFromEnd(TEXT("/"));
	if (!FPackageName::IsValidLongPackageName(Path))
	{
		Status = TEXT("유효한 콘텐츠 경로를 입력하세요. 예: /Game/Environment");
		return FReply::Handled();
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (Registry.IsLoadingAssets())
	{
		Status = TEXT("에셋 레지스트리가 검색 중입니다. 검색 완료 후 다시 시도하세요.");
		return FReply::Handled();
	}
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = bRecursive;
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
	MeshPreview->SetMesh(nullptr);
	Selected.Reset();
	List->ClearSelection();
	Rows.Reset();
	bPlanReady = false;
	FScopedSlowTask Progress(Assets.Num(), LOCTEXT("Loading", "Static Mesh 설정을 읽는 중…"));
	Progress.MakeDialog(true);
	for (const FAssetData& Asset : Assets)
	{
		if (Progress.ShouldCancel()) break;
		Progress.EnterProgressFrame(1, FText::FromName(Asset.AssetName));
		TSharedPtr<FLightmapUVRow> Row = MakeShared<FLightmapUVRow>();
		Row->Asset = Asset;
		Row->Mesh.Reset(Cast<UStaticMesh>(Asset.GetAsset()));
		Row->Current = LightmapUV::Describe(Row->Mesh.Get());
		Row->bChecked = Row->Mesh.IsValid();
		Row->Thumbnail = MakeShared<FAssetThumbnail>(Asset, 64, 64, ThumbnailPool);
		Rows.Add(Row);
	}
	Refilter();
	if (!VisibleRows.IsEmpty()) List->SetSelection(VisibleRows[0]);
	Status = FString::Printf(TEXT("%s 검색: %d / %d개 로드. 체크한 메시만 적용합니다. 검색 취소 시 일부만 표시됩니다."), *Path, Rows.Num(), Assets.Num());
	return FReply::Handled();
}

TSharedRef<ITableRow> SLightmapUVPanel::MakeRow(TSharedPtr<FLightmapUVRow> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FLightmapUVRow>>, Owner).Padding(4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[SNew(SCheckBox).IsChecked_Lambda([Item]() { return Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State)
			{
				Item->bChecked = State == ECheckBoxState::Checked;
				InvalidateSelection();
				if (Selected != Item) List->SetSelection(Item);
			})]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
		[SNew(SBox).WidthOverride(64).HeightOverride(64)[Item->Thumbnail->MakeThumbnailWidget()]]
		+ SHorizontalBox::Slot().FillWidth(1)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromName(Item->Asset.AssetName))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromName(Item->Asset.PackagePath)).AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Item]()
			{
				if (!Item->Result.IsEmpty()) return FText::FromString(Item->Result);
				if (!Item->bHasPlan) return FText::FromString(Item->Current);
				return FText::FromString(!Item->Plan.Errors.IsEmpty() ? TEXT("적용 불가 — 상세 확인") : Item->Plan.bChanged ? TEXT("변경 예정 — 상세 확인") : TEXT("동일하여 생략"));
			})]
		]
	];
}

void SLightmapUVPanel::SelectMesh(TSharedPtr<FLightmapUVRow> Item, ESelectInfo::Type SelectInfo)
{
	if (Selected == Item) return;
	Selected = Item;
	MeshPreview->SetMesh(Item ? Item->Mesh.Get() : nullptr);
}

void SLightmapUVPanel::InvalidatePlan()
{
	bPlanReady = false;
	for (const auto& Row : Rows) { Row->bHasPlan = false; Row->Result.Reset(); }
	Status = TEXT("대상/설정이 변경되었습니다. 적용 전 변경 미리보기를 실행하세요.");
	Refilter();
}

void SLightmapUVPanel::InvalidateSelection()
{
	// Selection does not change mesh settings or row membership. Keep existing row widgets and
	// thumbnails alive instead of rebuilding the entire list on every checkbox click.
	bPlanReady = false;
	Status = TEXT("적용 대상이 변경되었습니다. 적용 전 변경 미리보기를 실행하세요.");
}

void SLightmapUVPanel::Refilter()
{
	VisibleRows.Reset();
	for (const auto& Row : Rows)
	{
		if (!Search.IsEmpty() && !Row->Asset.PackageName.ToString().Contains(Search)) continue;
		if (bOnlyNeedsChange && Row->bHasPlan && !Row->Plan.bChanged && Row->Plan.Errors.IsEmpty()) continue;
		VisibleRows.Add(Row);
	}
	if (List) List->RequestListRefresh();
}

FReply SLightmapUVPanel::SelectAll(bool bChecked)
{
	// Deliberately affects all discovered meshes, including rows hidden by search/filter.
	for (const auto& Row : Rows) Row->bChecked = bChecked && Row->Mesh.IsValid();
	InvalidateSelection();
	return FReply::Handled();
}

FReply SLightmapUVPanel::RefreshLighting()
{
	Lighting = LightmapUV::LightingStatus();
	return FReply::Handled();
}

FReply SLightmapUVPanel::PreviewChanges()
{
	if (GEditor->PlayWorld) { Status = TEXT("PIE를 종료한 뒤 검사하세요."); return FReply::Handled(); }
	bPlanReady = false;
	FScopedSlowTask Progress(Rows.Num(), LOCTEXT("Planning", "변경 내용과 UV 채널 검사 중…"));
	Progress.MakeDialog(true);
	int32 Changes = 0, Errors = 0, Checked = 0;
	bool bCanceled = false;
	for (const auto& Row : Rows)
	{
		if (Progress.ShouldCancel()) { bCanceled = true; break; }
		Progress.EnterProgressFrame(1);
		Row->Result.Reset();
		Row->Current = LightmapUV::Describe(Row->Mesh.Get());
		Row->Plan = LightmapUV::MakePlan(Row->Mesh.Get(), *Settings);
		Row->bHasPlan = true;
		if (Row->bChecked)
		{
			++Checked;
			if (!Row->Plan.Errors.IsEmpty()) ++Errors;
			else if (Row->Plan.bChanged) ++Changes;
		}
	}
	bPlanReady = !bCanceled && Checked > 0 && Changes > 0;
	Lighting = LightmapUV::LightingStatus();
	Status = FString::Printf(TEXT("체크 %d개: 변경 가능 %d / 오류로 제외 %d. 숨겨진 행의 체크도 포함됩니다.%s"),
		Checked, Changes, Errors, bCanceled ? TEXT(" 검사 취소됨 — 다시 검사하세요.") : TEXT(" 적용은 에셋 공통 설정을 변경하며 모든 배치에 영향을 줍니다."));
	Refilter();
	return FReply::Handled();
}

bool SLightmapUVPanel::CanApply() const
{
	return bPlanReady && GEditor && !GEditor->PlayWorld;
}

FReply SLightmapUVPanel::ApplyChanges()
{
	if (!CanApply()) return FReply::Handled();
	FScopedTransaction Transaction(LOCTEXT("Transaction", "Batch edit lightmap UV settings"));
	FScopedSlowTask Progress(Rows.Num(), LOCTEXT("Applying", "라이트맵 UV 설정 적용·메시 재빌드 중…"));
	Progress.MakeDialog(true);
	int32 Changed = 0, Skipped = 0;
	for (const auto& Row : Rows)
	{
		if (Progress.ShouldCancel()) break;
		Progress.EnterProgressFrame(1, FText::FromName(Row->Asset.AssetName));
		if (!Row->bChecked) continue;
		if (!Row->bHasPlan || !Row->Plan.Errors.IsEmpty())
		{
			Row->Result = TEXT("검사 오류로 제외");
			++Skipped;
			continue;
		}
		// Geometry may have been reimported after preview. Revalidate before writing.
		FLightmapUVPlan Fresh = LightmapUV::MakePlan(Row->Mesh.Get(), *Settings);
		if (!Fresh.Errors.IsEmpty() || !LightmapUV::MatchesSnapshot(Row->Mesh.Get(), Row->Plan))
		{
			Row->Result = TEXT("원본/설정이 바뀌었거나 새 검사 오류가 있습니다. 다시 검사하세요.");
			++Skipped;
			continue;
		}
		FString Filename;
		if (FPackageName::DoesPackageExist(Row->Asset.PackageName.ToString(), &Filename) && IFileManager::Get().IsReadOnly(*Filename))
		{
			Row->Result = TEXT("읽기 전용: 소스 관리에서 체크아웃한 뒤 다시 적용하세요.");
			++Skipped;
			continue;
		}
		if (LightmapUV::Apply(Row->Mesh.Get(), Fresh, Row->Result)) ++Changed;
		else ++Skipped;
		if (Fresh.bChanged && Row->Mesh->GetOutermost()->IsDirty()) ChangedPackages.Add(Row->Mesh->GetOutermost());
		Row->Current = LightmapUV::Describe(Row->Mesh.Get());
	}
	bPlanReady = false;
	Status = FString::Printf(TEXT("완료 %d / 생략·오류 %d. 취소 시 완료한 메시만 변경됩니다. Ctrl+Z로 되돌릴 수 있습니다. 저장 버튼은 이 창에서 변경한 에셋만 대상으로 합니다."), Changed, Skipped);
	List->RequestListRefresh();
	if (Selected) MeshPreview->SetMesh(Selected->Mesh.Get());
	return FReply::Handled();
}

FReply SLightmapUVPanel::SaveChanges()
{
	if (GEditor->PlayWorld) return FReply::Handled();
	TArray<UPackage*> Packages;
	for (const TWeakObjectPtr<UPackage>& Package : ChangedPackages)
	{
		if (Package.IsValid() && Package->IsDirty()) Packages.AddUnique(Package.Get());
	}
	if (Packages.IsEmpty()) Status = TEXT("이 창에서 변경한 에셋 중 저장할 항목이 없습니다.");
	else
	{
		FEditorFileUtils::PromptForCheckoutAndSave(Packages, true, true);
		Status = TEXT("저장 창을 닫았습니다. 저장 취소/실패한 에셋은 수정 상태로 남습니다.");
	}
	return FReply::Handled();
}

FText SLightmapUVPanel::SelectedDescription() const
{
	if (!Selected) return LOCTEXT("NoSelection", "메시를 클릭하거나 체크하면 미리보기가 표시됩니다.");
	FString Text = Selected->Asset.GetObjectPathString() + TEXT("\n\n") + Selected->Current;
	if (Selected->bHasPlan) Text += TEXT("\n\n변경 미리보기\n") + Selected->Plan.Summary;
	if (!Selected->Result.IsEmpty()) Text += TEXT("\n\n처리 결과\n") + Selected->Result;
	return FText::FromString(Text);
}

FText SLightmapUVPanel::Counts() const
{
	int32 Checked = 0;
	for (const auto& Row : Rows) if (Row->bChecked) ++Checked;
	return FText::FromString(FString::Printf(TEXT("전체 %d | 표시 %d | 체크 %d (필터로 숨긴 항목 포함)"), Rows.Num(), VisibleRows.Num(), Checked));
}

#undef LOCTEXT_NAMESPACE
