# cpp-chatroom

C++17로 만든 **미니 채팅 서비스**입니다.  
배포했을 때 사용자가 **원하는 방식만 골라서(TCP / WebSocket / Gateway)** 바로 빌드·실행할 수 있도록 구조를 분리했습니다.

- **core**: 채팅 도메인 로직(닉네임/방/명령 처리)
- **transport**: 연결 방식(TCP / WS / Gateway)만 담당
- **apps**: 실행 파일(서버/클라이언트)

메시지는 **JSON(nlohmann/json)** 형태로 주고받습니다.  
TCP는 **길이 프레이밍(length-prefixed framing)** 으로 메시지 경계를 구분합니다.  
WebSocket(WS)은 **Boost.Beast** 기반(옵션)입니다.

---

## 주요 기능

- JSON 프로토콜 지원: `hello`, `chat`, `join`, `nick`, `who`
- **닉네임 중복 자동 해결**: `name`, `name_2`, `name_3` … 형태로 자동 할당
- **서버 로그 저장**: `logs/` 폴더에 일자별 로그 파일 생성
- **끊긴 클라이언트 자동 정리**: 전송 실패/연결 종료 시 세션 제거
- 빌드 옵션으로 선택 가능
  - TCP만
  - WS 서버만
  - TCP 서버 + WS 게이트웨이(WS↔TCP 브릿지)

---

## 프로젝트 폴더 구조

```text
src/
  core/
    chat_core.h/.cpp        # 유저/방/명령 처리 (JSON in/out)
    protocol.h              # 메시지 스키마/버전/req_id/에러 헬퍼
    connection.h            # 전송 계층(transport)과 무관한 연결 인터페이스
    logger.h                # 로거 함수 타입 정의
  transport/
    tcp/
      tcp_server.h/.cpp     # TCP accept/recv/send -> core로 디스패치
    ws/
      ws_server.h/.cpp      # (옵션) WebSocket 서버 (Boost.Beast)
    gateway/
      ws_gateway.h/.cpp     # (옵션) WS <-> TCP 브릿지(중계)
  apps/
    chatd_tcp_main.cpp      # TCP 서버 실행 파일
    chatd_ws_main.cpp       # (옵션) WS 서버 실행 파일
    chat_gateway_main.cpp   # (옵션) WS 게이트웨이 실행 파일
    chat_client_main.cpp    # 콘솔 클라이언트 실행 파일
```

---

## 요구 사항

### 공통(TCP)
- CMake 3.16 이상
- C++17 컴파일러(MSVC/clang/gcc)
- `nlohmann/json` (git submodule로 포함)

### WebSocket / Gateway(선택)
- Boost (최소 `Boost::system` 필요)
- Boost.Beast는 헤더 기반이지만 링크는 `Boost::system`이 필요합니다.

---

## 시작하기

### 1) submodule 포함해서 클론
```bash
git clone --recurse-submodules <YOUR_REPO_URL>
cd cpp-chatroom
```

이미 클론했다면:
```bash
git submodule update --init --recursive
```

---

## 빌드 방법(CMake 옵션)

### A) TCP만 빌드(기본값)
```bash
cmake -S . -B build -DCHAT_ENABLE_TCP=ON -DCHAT_ENABLE_WS=OFF -DCHAT_ENABLE_GATEWAY=OFF
cmake --build build --config Debug
```

생성되는 실행 파일:
- `chatd_tcp`
- `chat_client`

---

### B) WS 서버만 빌드(Boost 필요)
```bash
cmake -S . -B build -DCHAT_ENABLE_TCP=OFF -DCHAT_ENABLE_WS=ON -DCHAT_ENABLE_GATEWAY=OFF
cmake --build build --config Debug
```

생성되는 실행 파일:
- `chatd_ws`

---

### C) TCP 서버 + WS 게이트웨이(Boost 필요)
```bash
cmake -S . -B build -DCHAT_ENABLE_TCP=ON -DCHAT_ENABLE_WS=OFF -DCHAT_ENABLE_GATEWAY=ON
cmake --build build --config Debug
```

생성되는 실행 파일:
- `chatd_tcp`
- `chat_gateway`
- `chat_client`

---

## 실행 방법

### A) TCP 서버 + 콘솔 클라이언트

서버 실행:
```bash
./build/Debug/chatd_tcp 9000
```

클라이언트 실행(여러 터미널에서 여러 번 실행 가능):
```bash
./build/Debug/chat_client
```

클라이언트 명령어:
- `/who` : 현재 방 사용자 목록
- `/join <room>` : 방 이동
- `/nick <new>` : 닉네임 변경(중복이면 자동 suffix)
- `/quit` : 종료

---

### B) WS 서버(단독)

서버 실행:
```bash
./build/Debug/chatd_ws 9001
```

WS 접속 주소:
- `ws://127.0.0.1:9001`

---

### C) TCP 서버 + WS 게이트웨이(브라우저/WS클라이언트 연결용)

터미널 1: TCP 서버 실행
```bash
./build/Debug/chatd_tcp 9000
```

터미널 2: 게이트웨이 실행
```bash
./build/Debug/chat_gateway 9001 127.0.0.1 9000
```

WS 접속 주소(클라이언트는 게이트웨이에 연결):
- `ws://127.0.0.1:9001`

게이트웨이는 **WS 텍스트 프레임(JSON 문자열)** 을 받아서  
내부 TCP 서버에 **길이 프레이밍**으로 전달하고, 반대 방향도 그대로 중계합니다.

---

## 프로토콜(JSON)

모든 메시지는 JSON 오브젝트입니다.

권장 필드:
- `v`: 프로토콜 버전(기본 1)
- `type`: 메시지 종류
- `req_id`(선택): 요청-응답 매칭용
- `code`, `text`: 에러 응답용

### 클라이언트 → 서버

#### 1) hello (최초 1회)
```json
{"v":1,"type":"hello","nick":"jaeho","req_id":"h1"}
```

#### 2) chat
```json
{"v":1,"type":"chat","text":"hello world"}
```

#### 3) join (방 이동)
```json
{"v":1,"type":"join","room":"lobby","req_id":"j1"}
```

#### 4) nick (닉 변경)
```json
{"v":1,"type":"nick","nick":"newname","req_id":"n1"}
```

#### 5) who (방 사용자 확인)
```json
{"v":1,"type":"who","req_id":"w1"}
```

---

### 서버 → 클라이언트

#### hello_ok
```json
{"v":1,"type":"hello_ok","nick":"jaeho_2","room":"lobby","req_id":"h1"}
```

#### system
```json
{"v":1,"type":"system","text":"jaeho joined lobby"}
```

#### chat
```json
{"v":1,"type":"chat","room":"lobby","from":"jaeho","text":"hi"}
```

#### who_ok
```json
{"v":1,"type":"who_ok","room":"lobby","users":["jaeho","mina"],"req_id":"w1"}
```

#### error
```json
{"v":1,"type":"error","code":"BAD_REQ","text":"missing type","req_id":"..."}
```

---

## 로그(Logs)

서버 실행 시 `logs/` 폴더가 생성되며, 날짜별 로그 파일이 쌓입니다.
- TCP 서버: `logs/chat_YYYYMMDD.txt`
- WS 서버: `logs/ws_chat_YYYYMMDD.txt`

---

## Windows / MSVC 참고

- WS/Gateway 빌드 시 CMake가 Boost를 찾을 수 있어야 합니다.
- vcpkg를 쓰는 경우, CMake configure 단계에서 toolchain을 지정해야 할 수 있습니다.
  - 예) `-DCMAKE_TOOLCHAIN_FILE=<vcpkg 경로>/scripts/buildsystems/vcpkg.cmake`
