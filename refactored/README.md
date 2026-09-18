# Refactored ATM Simulator

수업 제출본을 기반으로 동작 오류, 입력 검증, 동시성, 메모리 소유권, 테스트 가능성을 개선한 버전입니다.

## Design goals

1. **Correctness** — 음수 거래, 현금 재초기화, 범위 초과 같은 실제 버그 제거
2. **Deterministic ownership** — Queue 메시지의 할당/전달/해제를 명확하게 정의
3. **RTOS-aware synchronization** — 공유 자원을 mutex로 보호하고 semaphore fallback 제공
4. **Testability** — RTOS에 의존하지 않는 거래 규칙을 별도 모듈로 분리
5. **Reproducibility** — 외부 uC/OS-II 환경과 빌드 방법을 명시

## Modules

```text
include/
├── atm_types.h
├── atm_domain.h
└── atm_storage.h

src/
├── atm_app.c
├── atm_domain.c
└── atm_storage.c

tests/
└── test_atm_domain.c
```

## Key changes

### 1. Withdrawal commit point

원본에서는 Transaction Task가 잔액/한도를 검사한 뒤 Cash Task에서 실제 계좌를 차감했습니다. 두 시점 사이에 상태가 달라질 수 있는 TOCTOU 구조였습니다.

개선 버전은 Cash Task에서 다음 순서로 처리합니다.

```text
CashBoxLock
  → ATM cash check
  → AccountLock
  → account lookup
  → balance / daily limit validation
  → account debit + daily_used update
  → cash_stock debit
  → persistence
  → unlock
```

### 2. Memory ownership

```text
Producer allocates
    ↓
OSQPost success
    ↓
Consumer owns
    ↓
forward or release
```

Queue 전송 실패 시 producer가 즉시 메시지를 반환합니다.

`OS_MEM_EN > 0`이면 Card/Auth/Transaction 메시지는 `OSMemCreate/Get/Put` 기반 고정 크기 memory partition을 사용합니다. 비활성 환경에서는 기존 포트 호환성을 위해 heap으로 fallback합니다.

### 3. Mutex and priority inversion

`OS_MUTEX_EN > 0`이면 `OSMutexCreate/Pend/Post`를 사용합니다.

- Account mutex ceiling priority: 2
- Cash-box mutex ceiling priority: 3

Mutex가 비활성화된 기존 실습 설정에서는 binary semaphore로 fallback합니다.

### 4. Input and parsing

- `scanf()` 대신 `fgets() + strtol()`
- 거래 금액은 반드시 `> 0`
- 파일 `sscanf()`에 field width 적용
- parse 결과 개수 확인
- 카드 배열은 capacity를 먼저 검사

### 5. Logging

실패 거래도 시도한 금액을 유지하고, 이체 거래는 대상 계좌를 함께 기록합니다.

## Task priorities

µC/OS-II는 숫자가 작을수록 우선순위가 높습니다.

| Task/Object | Priority | Purpose |
|---|---:|---|
| Account mutex ceiling | 2 | 계좌 공유 데이터 보호 |
| Cash mutex ceiling | 3 | ATM 현금 공유 상태 보호 |
| CashDispenserTask | 4 | 출금 commit |
| TransactionTask | 5 | 거래 처리 |
| AuthTask | 6 | PIN 인증 |
| CardReaderTask | 7 | 카드 입력 |
| ResetTask | 11 | maintenance |
| LogTask | 12 | 비핵심 로그 기록 |

## Test

uC/OS-II가 없어도 핵심 거래 규칙은 테스트할 수 있습니다.

```bash
cd refactored
make clean
make all
```

검증 항목:

- 0/음수 거래 거부
- 정상 출금
- 잔액 부족
- 일일 출금 한도
- 입금
- 이체
- 동일 계좌 이체 거부
- PIN 검증
- 일일 사용량 초기화
- 계좌 조회

## Static analysis

```bash
make lint
```

`cppcheck`가 필요하며 GitHub Actions에서도 동일한 검사를 수행합니다.

## µC/OS-II Win32 build

기본 외부 경로:

```text
C:/software/ucos-II
```

Visual Studio Developer Command Prompt + GNU Make 환경에서:

```bash
make app-msvc
```

다른 위치라면:

```bash
make app-msvc UCOS_ROOT=C:/path/to/ucos-II
```

외부 µC/OS-II source/port는 저장소에 vendoring하지 않습니다.

## Demo-only behavior

`ResetTask`는 시연을 위해 1분마다 `daily_used`를 초기화합니다. 실제 시스템에서는 실제 날짜 경계나 서버 정책으로 처리해야 합니다.

카드 선택 역시 시뮬레이션을 위해 샘플 카드 중 무작위 선택합니다. 실제 임베디드 장치라면 카드 리더 드라이버/입력 이벤트가 source가 됩니다.
