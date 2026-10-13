# Tuto 튜토리얼 이벤트 전달

## 데이터와 실행 범위

Shooter/Partner의 Event DT는 같은 EventID를 가진다. 서버는 동일한 ID를 둘에게 보내고 각 클라이언트의 EventSubsystem이 자기 역할 DA를 선택해 TTS, Upper, GIF를 해석한다. 비어 있는 참조 태그는 해당 영역을 표시하지 않는다. WorldGuideID는 두 DA가 공유하는 위치 안내 DT를 참조한다.

Trigger에는 수신 역할 설정을 두지 않는다. `FTuto_EventTriggerData`는 EventTag만 가진다. 이벤트 실행 여부는 Actor의 이벤트 항목별로 한 번 기록하며 PairID별 실행 기록은 없다. 잘못된 수신자 연결을 막기 위한 기존 PlayerState/Controller 연결 확인은 유지한다.

서버가 양쪽 Relay에 요청을 넣는 데 성공해야 항목을 소모한다. 클라이언트 표시 완료를 의미하지는 않는다. 완료·취소·Overlap 통지는 최초 실행 기록과 별개로 처리할 수 있다.

## Volume

`ATuto_EventTriggerVolume` 하나를 사용한다. 단일 EventTag, TriggerBox, WorldGuideTarget이 있으며 EnteringRole, TargetRole, 편집용 이벤트 배열, bTriggerOnce 설정은 없다. 항상 Actor 수명 동안 한 번 실행한다.

```text
로컬 Pawn의 root Overlap / 초기 겹침 확인
 → 소유 AFirstPersonPlayerController.NotifyTutorialVolumeEntered
 → Reliable ServerNotifyTutorialVolumeEntered
 → 서버에서 실제 Controller의 Pawn·PlayerState·Volume 위치 확인
 → 두 수신자가 준비되면 동일 EventID 전달
 → 서버 수락 통지와 Volume 실행 상태 복제
```

레벨에 배치된 Volume 자체에는 클라이언트가 Server RPC를 호출하지 않는다. 서버는 Overlap 콜백으로 실행하지 않고 소유 Controller의 진입 통지를 받는다. 두 클라이언트의 중복 진입은 서버의 단일 실행 상태로 합쳐진다. 수신자가 아직 준비되지 않으면 실행 상태를 소모하지 않으며, 로컬 Pawn이 계속 겹쳐 있는 동안 재시도한다.

EventTag가 설정된 Volume은 먼저 이벤트 전달을 확정하고 WorldGuideTarget의 Overlap 완료를 처리한다. EventTag가 비어 있으면 위치 안내의 도착 완료만 처리할 수 있다. WorldGuide DT가 Manual인 대상은 Overlap으로 완료되지 않는다.

## 상호작용 / 상태 이벤트

다음 기존 게임플레이 성공 시점에서 서버의 공통 전달 함수를 호출한다.

| 클래스 | 이벤트 항목 | 발생 시점 |
| --- | --- | --- |
| Tuto_Node | 0 / 1 | 최초 상호작용 시도 / 획득 확정 |
| Tuto_StatMachine | 0 / 1 | 해킹 잠금 해제 / 실제 양쪽 UI 종료 완료 |
| Tuto_Pistol | 0 | 획득 확정 |
| Tuto_Suit | 0 | 예약 수락이 아닌 실제 슈트·무기 지급 확정 |
| Tuto_InteractionDoorSwitch | 0 | 문 활성화 성공 |
| Tuto_CombatObserver | 0 / 1 / 2 | 증원 호출 / 최초 저항 / 방 클리어 |

서로 다른 게임플레이 발생 시점이 있는 Actor만 EventTriggers를 보관한다. 각 항목에는 EventTag만 설정한다. Volume의 위치 하나에 여러 이벤트를 넣지는 않는다.

```cpp
TriggerEvent(EventIndex, SourcePlayerState); // 같은 EventID를 양쪽에 전달
CompleteEvent(EventIndex, SourcePlayerState);
CancelEvent(EventIndex, SourcePlayerState, ETuto_EventRegion::Gif);
NotifyEventOverlap(EventIndex, SourcePlayerState, ETuto_EventRegion::WorldGuide);
```

StatMachine의 페어·Gameplay Generation별 UI 종료 확인은 게임플레이 세션 검증으로 유지한다. 이 검증과 이벤트를 PairID마다 재실행하는 기록은 별개다. 일반 원본 상호작용 클래스는 그대로 사용하며 Tuto 자식의 성공 지점만 이벤트에 연결한다.

## 네트워크와 표시

공통 ITuto_EventTriggerInterface는 서버에서 양쪽 Controller를 먼저 확인하고 각각 소유자 전용 EventRelay를 만든다. Relay의 복제 Queue는 EventTag/Operation/Region/Source를 값으로 보관한다. 따라서 픽업 Actor가 제거되어도 이벤트가 사라지지 않는다.

각 클라이언트는 LocalPlayer·역할이 준비되면 EventSubsystem.SendEvent(EventID)를 호출한다. 이 경로는 Upper/GIF의 DT 종료조건과 시간을 그대로 사용한다. Relay의 ServerAcknowledgeEvents는 받은 요청을 처리했다는 ACK이며, 진입 통지나 게임플레이 완료 통지가 아니다.

WorldGuide Manager에는 서버에서 이벤트를 한 번 전달한다. Manager가 공유 FIFO와 완료 상태를 관리하며 활성 Context를 모든 클라이언트에 복제한다. 같은 WidgetID의 대상은 모두 표시한다. TargetComponent의 위치·WidgetClass·크기·빌보딩·Stencil 처리 및 배치는 `WorldGuide/WorldGuide_Setup.md`를 따른다.

Tuto 7종은 기본 WorldGuideTarget 컴포넌트를 제공한다. 빈 WidgetID는 등록하지 않는다. 이벤트를 발생시키는 EventTag와 Actor 위에 표시할 WidgetID는 서로 다른 설정이다.

## Tool / Console

Event Tool의 역할 선택은 DT 미리보기용이다. Request from은 서버 요청을 시작할 PIE 플레이어를 고른다. Automatic requester는 사용 가능한 플레이어 하나만 선택한다. 서버에 EventID를 한 번 요청하므로 PIE 창마다 양쪽에 중복 전달하지 않는다.

`Outlier.Event.Send <EventTag>`도 같은 Controller 요청 경로를 사용한다. Tool/Console 미리보기 요청은 비Shipping 환경에서만 허용하며 서버에서 Event DT의 ID를 확인한다. 미리보기는 반복 실행할 수 있고 게임플레이 Volume의 일회성 상태를 변경하지 않는다.

## 기존 에셋 이전 / 검증

OutlierEventTriggerVolume C++ 클래스는 제거했다. DefaultEngine.ini의 Class Redirect가 기존 클래스 참조를 Tuto_EventTriggerVolume으로 연결한다. 기존 Outlier Volume의 단일 EventTag는 유지된다. 기존 Tuto Volume의 EventTriggers[0]는 로드 시 단일 EventTag로 옮긴다. 숨겨진 deprecated 필드는 이전 데이터 읽기 전용이며 새 에셋에는 저장하지 않는다.

빌드 후 기존 BP와 배치 레벨을 열어 EventTag·TriggerBox 범위·WorldGuideTarget 설정을 확인하고 저장한다. 이전에 수동으로 추가한 TargetComponent와 새 기본 컴포넌트에 같은 ID를 중복 설정하지 않는다.

PIE에서 동일 EventID의 양쪽 전달, Actor 항목별 최초 한 번 실행, 수신자 준비 후 재시도, Volume 중복 진입 처리를 확인한다. 상호작용 성공·슈트 획득 확정·양쪽 UI 종료 시점과 위치 안내의 도착 완료도 확인한다.
