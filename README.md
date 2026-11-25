# ESP-01S Relay — PlatformIO backend development build

기존 data/ 3개 파일은 변경하지 않았습니다. SRS v1.1의 8개 UI API 및 GET relay/wifi status를 구현한 개발용 소스입니다. 실제 빌드 및 실물 검증 전에는 부하를 연결하지 마십시오.

## 시작
1. VS Code + PlatformIO에서 이 폴더를 엽니다. 실물 Flash가 1MB인지 확인합니다.
2. 부하를 분리한 상태에서 `pio run`, `pio run -t upload`, `pio run -t uploadfs`를 실행합니다. uploadfs는 기존 설정을 덮어씁니다.
3. 정상 부팅 모드로 재시작 후 `pio device monitor` (115200)에서 AP 비밀번호와 HTTP admin 비밀번호를 확인합니다. 초기 AP 이름은 ESP01S-칩ID, 주소는 192.168.4.1입니다.
4. AP에 접속하고 http://192.168.4.1/ 을 엽니다. HTTP Basic 인증 사용자 admin 및 시리얼에 출력된 별도 비밀번호를 입력합니다.
5. 네트워크 설정에서 공유기 정보를 입력합니다. 비밀번호 공란은 기존 값 유지입니다. AP 설정 변경은 응답 후 약 750ms 뒤 적용합니다.

## 정책 및 제한
- GPIO0 Active Low. 부트로더 구간 안전은 보장하지 않습니다. 인칭 활성/비활성 전환 시 OFF, 실행 중 시간 변경은 다음 ON부터 적용. RESTORE 상태는 2초 지연 저장하므로 갑작스러운 전원 차단 시 오차가 있습니다.
- Wi-Fi 검색은 비동기이며 첫 요청은 빈 배열을 반환할 수 있습니다. 잠시 뒤 기존 검색 버튼을 다시 누르면 캐시된 결과를 반환합니다(30초 캐시, 최대 24개).
- AP 비활성화는 STA 연결 중에만 적용하며 연결이 끊기면 AP를 다시 시작합니다. STA 연결 성공 자체가 관리 접근 가능성을 보장하지는 않습니다.
- 관리자 인증은 별도 무작위 비밀번호를 내부 config에 저장하는 Basic 방식입니다. 명세의 passwordHash 방식 및 인증 설정 UI는 미구현입니다. HTTP는 암호화되지 않으며 시리얼에도 비밀번호가 출력됩니다. 신뢰 가능한 내부망에서만 사용하십시오.
- JSON 본문 16KB 상한, 파싱 용량 6KB(문서별), 일부 API는 더 작습니다. ESP8266WebServer가 본문을 먼저 수신하므로 네트워크 수준의 메모리 제한은 아닙니다. Origin 및 Fetch Metadata, JSON Content-Type 검사를 적용했지만 보안 시험은 미수행입니다.
- LittleFS 자동 포맷 금지. 파일 누락 또는 마운트 실패 시 안전 모드(ON 거부) 및 최소 복구 페이지. 복구는 UART 업로드를 사용합니다. 설정 초기화는 웹 파일을 유지합니다.
- 설정 스키마 v1만 지원하며 이전 autoOff 설정 마이그레이션은 미구현입니다. 확장 toggle/connect/disconnect API는 미구현입니다.
- 기존 UI의 OFF 버튼 busy 제한 및 오래된 polling 응답 문제는 그대로 남습니다. ESP8266WebServer와 Flash 작업은 동기식이므로 인칭 타이머의 엄격한 실시간 보장은 불가능합니다. 100ms 정확도, 저전압, 접점 정격, 전원 차단 복구를 실측해야 합니다.
- 플랫폼 및 ArduinoJson 버전은 개발 기준으로 선택했으며 실제 빌드로 확인되지 않았습니다. 본 환경에는 PlatformIO/xtensa 컴파일러가 없어 컴파일, 업로드, 브라우저 통합 시험을 수행하지 못했습니다.

## 시험 체크리스트
- [ ] pio run / buildfs 성공 및 Flash/RAM 여유 확인
- [ ] T-01~T-06: 부팅 OFF, STA 장애/AP, 재접속, 저장/복구, 파일 장애
- [ ] T-07~T-10: 인증/잘못된 JSON, 설정 파일 차단, 초기화, 장시간 반복
- [ ] T-11~T-12: 100ms/1초/1시간, restart/ignore, OFF, Wi-Fi 단절, 재부팅, millis wrap
- [ ] T-13~T-15: 4개 UI 탭, 동시 요청, CSRF, 백업/복원, 배포
