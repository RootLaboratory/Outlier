# WorldGuide 위치 UI 설정

PIE 세팅 확인용 명령과 로그 읽기는 [WorldGuide_Debug.md](WorldGuide_Debug.md)를 참고한다.

위치 UI 원본: https://docs.google.com/spreadsheets/d/1cpQctrN8GcvHBgx9o3mFQtLQBEZxvs6WzZQSIyc56MQ/edit?gid=1956982749#gid=1956982749

## 데이터 생성

`OutlierEditor Win64 Development` 빌드 후 에디터에서 공용 DT를 생성하거나 아래 원본 CSV를 가져온다. 기존 Shooter/Partner Event DA의 `WorldGuideTable`에 같은 DT를 연결하고, 마커 WBP와 Manager BP를 구성한다.

- 공용 DT: `/Game/Datas/Tutorial/Common/DT_CommonWorldGuideData`
- Row Structure: `OutlierWorldGuideDataRow`
- 기존 Shooter/Partner Event DA의 `WorldGuideTable`에 같은 DT 연결
- 기본 마커 WBP: `/Game/Blueprints/Widget/FirstPerson/Tutorial/WBP_WorldGuideWidget`
- Manager BP: `/Game/Blueprints/Widget/FirstPerson/Tutorial/BP_OutlierWorldGuideManager`
- 원본 CSV: `Content/Datas/Tutorial/SourceCSV/Common/DT_OutlierWorldGuide.csv`, UTF-8 BOM
- 기존 역할별 Event DT와 Event CSV에 `WorldGuideID` 참조 추가

DT는 WidgetID, EndCondition, TriggerType 세 필드만 가진다. 위치 UI 13개 모두 CHAIN이며, 도착 안내 003/013은 Overlap, 나머지는 Manual이다. 갈림길 004는 상호작용/진입 중 선택된 상황의 코드에서 완료하는 Manual이다. 기존 Event CSV에 WorldGuideID 열이 없어도 가져올 수 있도록 선택적 열로 처리했다.

## 레벨 배치

1. Persistent Level에 `BP_OutlierWorldGuideManager`를 하나 배치한다.
   Manager의 `Shooter Event Data Asset`에는 Shooter DA, `Partner Event Data Asset`에는 Partner DA를 각각 지정한다. 두 DA의 Event DT를 함께 탐색하며, 두 DA의 WorldGuideTable은 같은 공용 DT를 참조해야 한다. 비워 둔 슬롯만 프로젝트 설정의 해당 역할 DA로 대체한다. 기존 `Event Data Asset` 바인딩은 Shooter 슬롯에 유지된다.
2. Tuto DoorSwitch/Pistol/Node/Suit/StatMachine/EventTriggerVolume/CombatObserver는 기본 `WorldGuideTarget` 컴포넌트를 제공한다. 기존 Tuto BP에도 상속되어 보이므로 별도로 추가할 필요가 없다. 일반 Actor의 BP에는 `OutlierWorldGuideTargetComponent`를 추가한다.
3. 컴포넌트의 WidgetID를 지정한다. 같은 ID를 가진 컴포넌트는 모두 표시된다.
4. `WidgetClass`에 표시할 위젯 BP를 지정한다. 제공된 기본 마커나 기존 UserWidget BP를 사용할 수 있다.
5. 컴포넌트 위치는 월드 앵커다. `WorldOffset`은 이 위치에 더하는 월드 단위 오프셋이다. 예를 들어 Z=80이면 앵커 위 80cm에 표시한다. Actor 회전/스케일로 WorldOffset 방향이 바뀌지 않는다.
6. `UI Draw Size`로 화면 UI 크기를 지정하고, `Pivot`으로 투영 위치에 대한 정렬을 설정한다. 기본 Pivot은 (0.5, 0.5), 즉 중앙 정렬이다. 화면의 DPI 스케일이 적용되며 거리나 컴포넌트 Scale로 UI 크기가 바뀌지 않는다.
7. Manager의 OutlineStencilValue를 현재 Outlier PostProcess 머티리얼이 읽는 값에 맞춘다. 기본값은 기존 Outline에서 사용하는 1이다.

진행 Queue는 서버의 Manager가 하나만 관리한다. 활성 Context를 모든 클라이언트에 복제하고, 클라이언트는 해당 ID로 등록된 대상마다 독립적인 WidgetComponent를 생성한다. MainUI/InfoCanvas에 의존하지 않는다. 컴포넌트가 나중에 등록돼도 현재 활성 ID와 같으면 바로 표시한다.

WidgetComponent는 **Screen 공간**을 사용한다. Actor의 월드 앵커를 화면으로 투영한 UI로 표시하므로 월드 조명, 위젯 평면 재질의 Emissive, Bloom/노출 영향을 받지 않는다. 별도 3D 회전 빌보딩은 필요 없으며 항상 화면을 정면으로 향한다. 실제 3D 평면으로 렌더링하지 않으므로 월드 물체의 가림은 적용하지 않는다. 앵커 추적은 매 프레임 갱신하고, 위젯은 로컬 플레이어의 화면에 연결하며 입력을 가로채지 않는다. 대상 Mesh의 Outline은 기존 월드 Stencil 방식으로 유지한다.

기존 Tuto Actor에 컴포넌트를 붙일 수 있다. 별도 바인딩 Actor나 대상 슬롯 테이블은 필요 없다. Stencil은 소유 Actor의 Mesh에 적용하며, WidgetComponent 자체는 Outline 대상에서 제외한다. 같은 Mesh를 사용하는 복수 안내 컴포넌트는 마지막 사용자가 해제될 때 원래 상태를 복구한다. 종료 시 다른 효과가 이미 다른 Stencil을 썼다면 그 값을 덮어쓰지 않는다.

Tuto 기본 컴포넌트의 WidgetID/WidgetClass는 비워둔다. 실제 안내 대상만 값을 지정하면 된다. 이미 수동으로 추가한 안내 컴포넌트가 있다면 기존 것을 사용하고 새 기본 컴포넌트는 비워두어도 된다. 같은 Actor의 여러 컴포넌트에 같은 ID를 지정하면 마커가 여러 개 생긴다. CombatObserver의 관찰 로직은 서버에서만 실행하고, 대상 컴포넌트 표시와 서버의 Actor 제거를 반영하도록 클라이언트에도 Actor를 로드·복제한다. Observer는 전투 종료 시 제거되므로 그 이후의 목적지 표시는 다른 대상 Actor에 지정한다.

## 호출과 완료

`Tuto_EventTriggerInterface::TriggerEvent`는 기존 Upper/GIF/TTS 전송과 함께 WorldGuide를 서버에서 한 번만 요청한다. 두 수신자에게 전송하는 Relay마다 공유 Queue를 추가하지 않는다.

```cpp
FOutlierWorldGuideContext Context;
Context.WidgetID = WidgetID;
Context.OriginEventTag = EventTag; // 선택 사항
Manager->EnqueueGuide(Context);   // 서버

TargetComponent->CompleteGuide();      // 실제 상호작용 성공/획득 확정
TargetComponent->NotifyGuideOverlap(); // Overlap 행만 종료
Manager->CancelWidget(WidgetID);       // 체인 없이 제거
```

Tuto DoorSwitch/Pistol/Node/Suit/StatMachine에는 실제 상호작용 성공·획득 확정 시 소유 컴포넌트의 Manual 완료 호출을 연결했다. Suit는 비동기 예약 수락이 아니라 실제 획득 확정, Node는 획득 완료 경로를 사용한다. Tuto EventTriggerVolume은 필터링된 Pawn 진입 시 소유 컴포넌트의 Overlap 완료를 호출한다. 일반 원본 Actor 클래스는 수정하지 않는다.

기존 이벤트 단위 종료 API에는 `ETuto_EventRegion::WorldGuide`와 `All`을 추가했다. 기존 `Both`는 Upper/GIF 의미를 유지한다. WorldGuide만 종료하면 소유 클라이언트 HUD가 준비되기를 기다리지 않고 서버 Manager에 직접 처리한다. 체인 후속을 포함한 원래 이벤트의 현재/대기 항목을 종료할 때는 EventTag를, 정확한 안내 하나를 종료할 때는 WidgetID를 사용한다.

## 체인과 분기

Manager BP의 ChainContexts에 무조건 이어지는 다음 WidgetID를 설정한다.

```text
001 → 002 → 003 → 004
004: 액터가 다음 ID를 선택
    서버실 패널 성공 → 005
    선택 통로 진입   → 004_1 → 004_2 → 004_3 → 005
005 → 006 → 007 → 008 → 009 → 013
```

분기 시에는 다음 ID를 인자로 지정한다. DT에는 분기 조건이나 액터 참조를 넣지 않는다.

```cpp
TargetComponent->CompleteGuide(SelectedNextWidgetID);
// 또는 Manager->CompleteWidget(CurrentWidgetID, SelectedNextWidgetID);
```

동일한 EVT_TUT_015_ROUTE_SPLIT의 두 시트 행은 기본 갈림길 004와 연구 일지 004_1로 분리했다. Event DT의 기본 연결은 004이고, 선택 통로 액터가 004_1을 선택한다. 완료된 안내는 다시 표시하지 않고 체인의 다음 안내를 처리하며, 취소는 다음 안내를 실행하지 않는다. Context의 방문 ID 기록은 순환 체인을 막는다. 대기 중 또는 요청 전에 이미 달성한 안내도 완료 상태를 기록하여 표시 시 건너뛴다.

완료는 해당 ID의 모든 시각화에 적용된다. 같은 ID 대상 중 하나가 완료했다고 그 대상의 위젯만 남기거나 지우는 별도 진행 상태를 만들지 않는다.

## 배치 확인

PIE에서 두 플레이어의 동일 안내 표시, 상호작용 완료·도착 Overlap, 체인·분기 진행을 확인한다. 같은 WidgetID의 복수 대상, 늦은 대상 등록, 종료 시 Stencil 복구도 확인한다. 실제 원격 클라이언트 통신과 각 레벨의 배치는 별도로 검증한다.
