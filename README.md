# cpp-chatroom

C++17로 만든 콘솔 기반 TCP 채팅 프로젝트입니다.  
Windows/MSVC 환경에서 빌드했으며, 길이 프레이밍(length-prefixed framing)을 적용해 TCP에서 발생하는 “메시지 경계 문제”를 해결했습니다.

---

## Features
- TCP 기반 서버/클라이언트
- 멀티 클라이언트 접속 지원 (서버에서 accept 후 클라이언트별 thread 처리)
- 브로드캐스트 채팅 (한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달)
- **길이 프레이밍 적용**: `[4바이트 길이][payload]` 형태로 메시지를 전송/수신
- `send_all`, `recv_exact` 유틸로 부분 송신/부분 수신 처리

---

## Why framing?
TCP는 “메시지 단위”가 아니라 “바이트 스트림”이라서,
- `recv()` 한 번에 보낸 만큼 다 오지 않을 수 있고(부분 수신)
- 여러 메시지가 붙어서 오거나 쪼개져서 올 수 있습니다.

그래서 본 프로젝트는 메시지 앞에 4바이트 길이를 붙이는 방식으로
메시지 경계를 명확히 했습니다.

---

## Project Structure

```text
cpp-chatroom/
  CMakeLists.txt
  src/
    net/        # 소켓 초기화/종료, send_all/recv_exact, close 등 플랫폼 래퍼
    common/     # framing(길이 프레이밍)
    server/     # 채팅 서버 (멀티 클라이언트 + 브로드캐스트)
    client/     # 채팅 클라이언트 (송신/수신 스레드)
```

---

## Build (MSVC + VSCode CMake Tools)
1. VSCode에서 프로젝트 폴더를 엽니다.
2. `CMake: Select a Kit`에서 MSVC x64(amd64) 키트를 선택합니다.
3. `CMake: Configure`
4. `CMake: Build`

빌드 결과물은 보통 아래 경로에 생성됩니다.
- `build/Debug/chat_server.exe`
- `build/Debug/chat_client.exe`

---

## Run
PowerShell 기준 예시입니다.

### 1) 서버 실행
```powershell
.\build\Debug\chat_server.exe 9000
```

### 2) 클라이언트 실행(터미널 여러 개)
```powershell
.\build\Debug\chat_client.exe 127.0.0.1 9000 nick1
.\build\Debug\chat_client.exe 127.0.0.1 9000 nick2
```

클라이언트에서 메시지를 입력하면 다른 클라이언트에서 수신됩니다.
종료는 /quit 입력.

---

## Protocol

메시지 포맷: [uint32 length (big-endian)][payload bytes]

payload는 현재 "[" + nickname + "] " + message 형태의 문자열을 전송합니다.

---

## Notes

Windows에서 처음 실행 시 방화벽 팝업이 뜨면 Private network 허용이 필요할 수 있습니다.

파일 인코딩 경고(C4819)가 뜨면 소스 파일을 UTF-8(with BOM)로 저장하면 해결됩니다.Protocol
