// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// APresetPlayerStart::PresetId, DT_PresetNode(FPresetNodeProvideRow)의 RowName,
// PreSetLoadWidget의 EOutlierStage -> FName 변환이 전부 이 이름들만 참조한다.
// DT_PresetNode.uasset의 실제 행 이름은 앞자리 0 없이 Level1~Level4 이므로 반드시 이 표기를 따른다.
namespace OutlierPresetStageIds
{
	// 로비에서 WP 아레나로 처음 들어갈 때 쓰는 기본 진입 지점. 예전엔 "선택 없음" 의미의
	// None이었지만, 이제 그 자체로 유효한 프리셋(초기 시작점)이라 Start로 부른다.
	inline const FName Start(TEXT("Start"));
	inline const FName Level1(TEXT("Level1"));
	inline const FName Level2(TEXT("Level2"));
	inline const FName Level3(TEXT("Level3"));
	inline const FName Level4(TEXT("Level4"));

	// 정의된 전체 목록 — 에디터 드롭다운(meta=(GetOptions=...))처럼 "고정된 후보군"이
	// 필요한 곳에서 재사용한다. 새 스테이지를 추가하면 여기 한 곳만 늘리면 된다.
	inline TArray<FName> All()
	{
		return { Start, Level1, Level2, Level3, Level4 };
	}
}
