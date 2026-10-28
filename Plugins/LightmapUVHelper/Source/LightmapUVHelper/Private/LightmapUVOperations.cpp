#include "LightmapUVOperations.h"

#include "LightmapUVSettings.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "Subsystems/AssetEditorSubsystem.h"

namespace
{
	int32 SourceChannels(UStaticMesh* Mesh, int32 LOD)
	{
		// Reduced LODs inherit geometry from a base LOD instead of owning a mesh description.
		TSet<int32> Visited;
		while (Mesh->IsSourceModelValid(LOD) && !Visited.Contains(LOD))
		{
			Visited.Add(LOD);
			if (const FMeshDescription* Description = Mesh->GetMeshDescription(LOD))
			{
				return FStaticMeshConstAttributes(*Description).GetVertexInstanceUVs().GetNumChannels();
			}
			LOD = Mesh->GetSourceModel(LOD).ReductionSettings.BaseLODModel;
		}
		return 0;
	}

	bool ValidResolution(int32 Value)
	{
		return Value >= 4 && Value <= 4096 && FMath::IsPowerOfTwo(Value);
	}

	FString DescribeBuild(int32 LOD, const FMeshBuildSettings& B)
	{
		return FString::Printf(TEXT("LOD%d: 생성 %s | UV %d → %d | 최소 %d"),
			LOD, B.bGenerateLightmapUVs ? TEXT("ON") : TEXT("OFF"),
			B.SrcLightmapIndex, B.DstLightmapIndex, B.MinLightmapResolution);
	}
}

FLightmapUVPlan LightmapUV::MakePlan(UStaticMesh* Mesh, const ULightmapUVSettings& S)
{
	FLightmapUVPlan P;
	if (!Mesh || Mesh->GetNumSourceModels() == 0)
	{
		P.Errors.Add(TEXT("편집할 원본 LOD가 없습니다."));
		P.Summary = P.Errors[0];
		return P;
	}
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
	P.SourceModelCount = Mesh->GetNumSourceModels();
	P.BeforeCoordinate = Mesh->GetLightMapCoordinateIndex();
	P.AfterCoordinate = S.bApplyCoordinate ? S.CoordinateIndex : P.BeforeCoordinate;
	P.BeforeResolution = Mesh->GetLightMapResolution();
	P.AfterResolution = S.bApplyResolution ? S.Resolution : P.BeforeResolution;
	if (S.bApplyResolution && !ValidResolution(S.Resolution))
	{
		P.Errors.Add(TEXT("Lightmap Resolution은 4~4096 범위의 2의 거듭제곱이어야 합니다."));
	}
	if (S.bApplyMinResolution && !ValidResolution(S.MinResolution))
	{
		P.Errors.Add(TEXT("Min Lightmap Resolution은 4~4096 범위의 2의 거듭제곱이어야 합니다."));
	}
	if (P.AfterCoordinate < 0 || P.AfterCoordinate > 7)
	{
		P.Errors.Add(TEXT("Lightmap Coordinate Index는 0~7 범위여야 합니다."));
	}
	const int32 First = S.LODScope == ELightmapUVLOD::Specified ? S.LODIndex : 0;
	const int32 End = S.LODScope == ELightmapUVLOD::All ? Mesh->GetNumSourceModels() : First + 1;
	if (!Mesh->IsSourceModelValid(First))
	{
		P.Errors.Add(FString::Printf(TEXT("LOD%d가 없습니다."), First));
		P.Summary = FString::Join(P.Errors, TEXT("\n"));
		return P;
	}
	TArray<FString> Lines;
	for (int32 LOD = First; LOD < End; ++LOD)
	{
		const FMeshBuildSettings Before = Mesh->GetSourceModel(LOD).BuildSettings;
		FMeshBuildSettings After = Before;
		if (S.bApplyGenerate) After.bGenerateLightmapUVs = S.bGenerate;
		if (S.bApplySource) After.SrcLightmapIndex = S.SourceIndex;
		if (S.bApplyDestination) After.DstLightmapIndex = S.DestinationIndex;
		if (S.bApplyMinResolution) After.MinLightmapResolution = S.MinResolution;
		P.LODs.Add(LOD);
		P.Before.Add(Before);
		P.After.Add(After);
		const bool bBuildChanged = !(Before == After);
		P.bChanged |= bBuildChanged;
		Lines.Add(DescribeBuild(LOD, Before));
		if (bBuildChanged) Lines.Add(TEXT("  변경 → ") + DescribeBuild(LOD, After));

		const int32 Channels = SourceChannels(Mesh, LOD);
		if (After.bGenerateLightmapUVs)
		{
			if (After.SrcLightmapIndex < 0 || After.SrcLightmapIndex >= Channels)
			{
				P.Errors.Add(FString::Printf(TEXT("LOD%d: 소스 UV%d가 없습니다 (원본 채널 수 %d)."), LOD, After.SrcLightmapIndex, Channels));
			}
			if (After.DstLightmapIndex < 0 || After.DstLightmapIndex > 7 || After.DstLightmapIndex > Channels)
			{
				P.Errors.Add(FString::Printf(TEXT("LOD%d: 목적 채널은 기존 채널 또는 바로 다음 채널이어야 합니다 (0~7)."), LOD));
			}
			if (After.DstLightmapIndex < Channels && bBuildChanged)
			{
				P.Warnings.Add(FString::Printf(TEXT("LOD%d: 원본 UV%d를 재배치합니다. 수작업 UV 여부를 확인하세요."), LOD, After.DstLightmapIndex));
				if (!S.bAllowOverwrite) P.Errors.Add(TEXT("기존 목적 UV 덮어쓰기 허용이 필요합니다."));
			}
			if (After.SrcLightmapIndex == After.DstLightmapIndex)
			{
				P.Warnings.Add(FString::Printf(TEXT("LOD%d: 소스와 목적 채널이 같습니다. 텍스처 UV 용도로 쓰는 채널인지 확인하세요."), LOD));
			}
			if (P.AfterCoordinate != After.DstLightmapIndex)
			{
				P.Warnings.Add(FString::Printf(TEXT("LOD%d: 생성 UV%d와 라이트맵 사용 UV%d가 다릅니다."), LOD, After.DstLightmapIndex, P.AfterCoordinate));
			}
			if (P.AfterResolution < After.MinLightmapResolution)
			{
				P.Warnings.Add(FString::Printf(TEXT("LOD%d: 실제 해상도가 UV 여백 계산 기준보다 낮습니다."), LOD));
			}
		}
		else if (S.bApplySource || S.bApplyDestination || S.bApplyMinResolution)
		{
			P.Warnings.Add(FString::Printf(TEXT("LOD%d: UV 생성이 OFF라 생성 옵션은 현재 사용되지 않습니다."), LOD));
		}
	}

	// Check the mesh-wide lighting channel against LOD0, even when editing another LOD.
	const int32 LOD0PlanIndex = P.LODs.IndexOfByKey(0);
	const FMeshBuildSettings& LOD0 = LOD0PlanIndex == INDEX_NONE ? Mesh->GetSourceModel(0).BuildSettings : P.After[LOD0PlanIndex];
	const bool bWillGenerateChannel = LOD0.bGenerateLightmapUVs && LOD0.DstLightmapIndex == P.AfterCoordinate;
	if (P.AfterCoordinate >= Mesh->GetNumTexCoords(0) && !bWillGenerateChannel)
	{
		P.Errors.Add(TEXT("LOD0에 라이트맵 사용 UV 채널이 없고 생성 계획도 없습니다."));
	}
	if (Mesh->IsHiResMeshDescriptionValid())
	{
		P.Warnings.Add(TEXT("별도 Nanite 고해상도 원본이 있습니다. 이 툴은 일반 Source LOD만 변경합니다."));
	}
	P.bChanged |= P.BeforeCoordinate != P.AfterCoordinate || P.BeforeResolution != P.AfterResolution;
	Lines.Add(FString::Printf(TEXT("사용 UV: %d → %d | 해상도: %d → %d"),
		P.BeforeCoordinate, P.AfterCoordinate, P.BeforeResolution, P.AfterResolution));
	for (const FString& Warning : P.Warnings) Lines.Add(TEXT("주의: ") + Warning);
	for (const FString& Error : P.Errors) Lines.Add(TEXT("적용 불가: ") + Error);
	P.Summary = FString::Join(Lines, TEXT("\n"));
	return P;
}

bool LightmapUV::MatchesSnapshot(UStaticMesh* Mesh, const FLightmapUVPlan& P)
{
	if (!Mesh || Mesh->GetNumSourceModels() != P.SourceModelCount
		|| Mesh->GetLightMapCoordinateIndex() != P.BeforeCoordinate || Mesh->GetLightMapResolution() != P.BeforeResolution)
	{
		return false;
	}
	for (int32 I = 0; I < P.LODs.Num(); ++I)
	{
		if (!Mesh->IsSourceModelValid(P.LODs[I]) || !(Mesh->GetSourceModel(P.LODs[I]).BuildSettings == P.Before[I])) return false;
	}
	return true;
}

FString LightmapUV::Describe(UStaticMesh* Mesh)
{
	if (!Mesh) return TEXT("메시를 불러오지 못했습니다.");
	TArray<FString> Lines;
	for (int32 LOD = 0; LOD < Mesh->GetNumSourceModels(); ++LOD)
	{
		Lines.Add(DescribeBuild(LOD, Mesh->GetSourceModel(LOD).BuildSettings));
	}
	Lines.Add(FString::Printf(TEXT("라이트맵 사용 UV%d | 기본 해상도 %d"), Mesh->GetLightMapCoordinateIndex(), Mesh->GetLightMapResolution()));
	return FString::Join(Lines, TEXT("\n"));
}

FString LightmapUV::LightingStatus()
{
	const auto GetCVar = [](const TCHAR* Name, int32 Fallback)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetInt() : Fallback;
	};
	const int32 StaticLighting = GetCVar(TEXT("r.AllowStaticLighting"), -1);
	const int32 GI = GetCVar(TEXT("r.DynamicGlobalIlluminationMethod"), -1);
	FString Result = TEXT("UV 준비: 가능 (조명 베이크와 별개)\n");
	Result += StaticLighting == 0 ? TEXT("프로젝트: Allow Static Lighting OFF — 정적 라이트맵 베이크/사용 불가.\n")
		: StaticLighting == 1 ? TEXT("프로젝트: Allow Static Lighting ON.\n") : TEXT("프로젝트: 정적 조명 설정을 확인할 수 없습니다.\n");
	Result += GI == 1 ? TEXT("기본 GI: Lumen — 베이크된 정적 조명은 표시되지 않습니다.\n") : TEXT("기본 GI: Lumen 이외의 방식 (또는 확인 불가).\n");
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World && World->GetWorldSettings())
	{
		Result += World->GetWorldSettings()->bForceNoPrecomputedLighting
			? TEXT("현재 레벨: Force No Precomputed Lighting ON — 베이크 차단.\n")
			: TEXT("현재 레벨: Force No Precomputed Lighting OFF.\n");
	}
	else Result += TEXT("현재 레벨: 확인 불가.\n");
	Result += TEXT("프로젝트/레벨 설정은 자동 변경하지 않습니다. 재시작 미적용 설정, 플랫폼, PP Volume, 배치별 Mobility/해상도는 별도 확인이 필요합니다.");
	return Result;
}

bool LightmapUV::Apply(UStaticMesh* Mesh, const FLightmapUVPlan& P, FString& OutResult)
{
	if (!P.Errors.IsEmpty() || !MatchesSnapshot(Mesh, P))
	{
		OutResult = TEXT("건너뜀: 오류 또는 미리보기 이후 변경된 설정. 다시 검사하세요.");
		return false;
	}
	if (!P.bChanged)
	{
		OutResult = TEXT("동일하여 생략");
		return false;
	}
	UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	// Follow the engine subsystem's close/rebuild/reopen pattern for open Static Mesh editors.
	const bool bWasOpen = Editors && Editors->FindEditorForAsset(Mesh, false);
	if (bWasOpen) Editors->CloseAllEditorsForAsset(Mesh);
	Mesh->Modify();
	for (int32 I = 0; I < P.LODs.Num(); ++I) Mesh->GetSourceModel(P.LODs[I]).BuildSettings = P.After[I];
	Mesh->SetLightMapCoordinateIndex(P.AfterCoordinate);
	Mesh->SetLightMapResolution(P.AfterResolution);
	// PostEditChange rebuilds render data and invalidates lighting; wait before reporting completion.
	Mesh->PostEditChange();
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
	Mesh->MarkPackageDirty();
	if (bWasOpen) Editors->OpenEditorForAsset(Mesh);
	OutResult = TEXT("변경·재빌드 완료 (저장 전). UV 품질/조명 베이크는 별도 확인하세요.");
	for (int32 I = 0; I < P.LODs.Num(); ++I)
	{
		if (P.After[I].bGenerateLightmapUVs && Mesh->GetNumTexCoords(P.LODs[I]) <= P.After[I].DstLightmapIndex)
		{
			OutResult = TEXT("재빌드 후 목적 UV가 없습니다. Output Log 확인 필요 (변경은 저장 전 상태).");
			return false;
		}
	}
	return true;
}
