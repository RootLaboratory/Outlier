# WorldGuide 세팅 디버그

최신 코드 빌드 후 PIE 플레이어 창의 콘솔에서 실행한다. 상세 출력은 Output Log의 `[WorldGuideDebug]`로 검색한다. 커맨드와 서버 디버그 RPC는 비Shipping 빌드에서만 동작한다. 이번 추가 코드는 아직 빌드·실행 검증하지 않았다.

## 먼저 표시 확인

```text
Outlier.WorldGuide.Debug 1
Outlier.WorldGuide.Dump
Outlier.WorldGuide.Reset
Outlier.WorldGuide.Show 001
Outlier.WorldGuide.Mark 10
```

Show는 `UI.Info.World.LOC_TUT_001` 같은 전체 태그와 `001`, `004_1` 같은 접미사 모두 받는다. WorldGuide만 Queue에 추가하며 Upper/GIF/TTS는 호출하지 않는다. 이미 다른 안내가 활성 상태면 대기하므로 Reset 또는 Clear 후 테스트한다.

실제 EventID → 양쪽 DT 표시 → WorldGuide 연결을 함께 확인하려면:

```text
Outlier.Event.Send Event.006_Broken_Wall
```

## 명령

| 명령 | 동작 |
| --- | --- |
| `Outlier.WorldGuide.Debug 1` / `0` | 등록, EventID 해석, Queue, 활성화, 표시 생성/해제, 완료/취소, 복제 로그 켜기/끄기 |
| `Outlier.WorldGuide.Dump` | 현재 창의 Manager·DT·활성 안내·대상·WBP·크기·Mesh Stencil과 설정 문제 출력 |
| `Outlier.WorldGuide.Dump server` | 서버의 대기 Queue, 완료 ID, 체인, 대상 상태 출력 |
| `Outlier.WorldGuide.Show 001` | 안내 ID를 서버 Queue에 추가 |
| `Outlier.WorldGuide.Complete` | 서버의 현재 안내를 완료하고 체인 진행 |
| `Outlier.WorldGuide.Complete 004 005` | 004를 완료하고 005 분기를 선택 |
| `Outlier.WorldGuide.Complete 004 004_1` | 004를 완료하고 선택 통로 분기를 선택 |
| `Outlier.WorldGuide.Overlap 003` | Overlap 종료조건인 ID만 완료. ID를 생략하면 현재 안내 사용 |
| `Outlier.WorldGuide.Cancel` | 현재 안내 취소. ID 지정 가능, 다음 체인은 실행하지 않음 |
| `Outlier.WorldGuide.Clear` | 활성 안내와 대기 Queue 제거. 완료 기록은 유지 |
| `Outlier.WorldGuide.Reset` | 활성 안내·대기 Queue·완료 기록 제거 |
| `Outlier.WorldGuide.Mark 10` | 10초 동안 로컬 대상 앵커에 구와 Actor 이름·WidgetID 표시. 활성=초록, 비활성=노랑 |

클라이언트에서 Show/Complete/Overlap/Cancel/Clear/Reset을 입력해도 소유 Controller RPC를 통해 서버에서 실행한다. Dump는 로컬 시각화 확인용이며, `Dump server`는 서버 상태 확인용이다. 서버 전용 프로세스의 출력은 그 서버 로그에서 확인한다.

Reset은 WorldGuide만 초기화한다. Tuto Volume/Actor의 게임플레이 일회성 실행 기록은 유지한다. 같은 이벤트를 다시 표시하려면 Reset 후 Show 또는 Event.Send를 쓰고, 실제 Volume 진입을 처음부터 시험하려면 PIE를 재시작한다.

## Dump 읽기

- `ISSUE`: Manager 누락/중복, DT 미연결, DT에 없는 ID, 대상 미등록, WidgetClass 누락/추상 클래스, 잘못된 DrawSize, 활성 대상의 숨겨진 Actor, Stencil 지원 설정 문제.
- `GuideActive=1`: 해당 컴포넌트가 안내 대상으로 활성화됨. 실제 위젯 생성과 별개다.
- `WidgetCreated=0`: 활성 상태여도 WidgetClass 미설정이나 위젯 생성 실패로 화면에 표시할 인스턴스가 없다.
- `Root=None`: 생성된 위젯의 루트가 없거나 아직 위젯 인스턴스가 없다. WBP 디자인과 WidgetCreated를 함께 확인한다.
- `ComponentVisible`, `OwnerHidden`: 위젯 컴포넌트·소유 Actor의 표시 상태.
- `Space=Screen`, `UISize`, `Pivot`: 화면 UI 방식, DPI 적용 전 UI 크기, 투영 위치에 대한 정렬.
- `WorldOffset`, `Anchor`, `WidgetLocation`: 월드 오프셋, 대상 컴포넌트 위치, 오프셋을 더한 실제 투영 위치.
- `Mesh CustomDepth/Stencil`: 소유 Actor Mesh의 실제 설정. Mesh 없는 Volume은 마커만 표시하므로 `No owner mesh`가 정상이다.
- `EmptyIDs`: 기본 컴포넌트 중 WidgetID가 비어 있는 수. 이벤트 발생 전용 Actor 등에서는 정상이며 ISSUE로 취급하지 않는다.

Mark는 WorldOffset을 더한 실제 투영 앵커를 표시하므로 마커가 안 보일 때 위치부터 확인할 수 있다. 마커는 Screen 공간이라 회전 빌보딩 없이 항상 화면을 정면으로 향하고 UI 크기를 유지한다.
