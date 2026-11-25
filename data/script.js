'use strict';

const $ = id => document.getElementById(id),
  text = (id, v) => $(id).textContent = v ?? '—';

let online = false,
  busy = false,
  config = null,
  polling = false,
  toastTimer,
  sample = null;


// API 요청
async function api(path, method = 'GET', body) {
  const controller = new AbortController(),
    timer = setTimeout(() => controller.abort(), 8000);

  try {
    const r = await fetch('/api' + path, {
      method,
      credentials: 'same-origin',
      cache: 'no-store',
      signal: controller.signal,

      headers: {
        Accept: 'application/json',
        ...(body !== undefined
          ? { 'Content-Type': 'application/json' }
          : {})
      },

      ...(body !== undefined
        ? { body: JSON.stringify(body) }
        : {})
    });

    if (!r.ok) {
      throw Error('HTTP ' + r.status);
    }

    const data = await r.json();

    if (data.success === false) {
      throw Error(data.message || '요청 실패');
    }

    return data;
  } finally {
    clearTimeout(timer);
  }
}


// 알림 메시지
function notify(message) {
  text('toast', message);

  $('toast').hidden = false;

  clearTimeout(toastTimer);

  toastTimer = setTimeout(
    () => $('toast').hidden = true,
    4500
  );
}


// 버튼 활성화 및 비활성화
function controls() {
  for (const id of [
    'on',
    'off',
    'saveName',
    'backup',
    'restore',
    'reboot',
    'reset',
    'scan'
  ]) {
    $(id).disabled = !online || busy;
  }

  document.querySelectorAll('form button[type=submit]')
    .forEach(b => {
      b.disabled =
        !online ||
        busy ||
        switching ||
        !config;
    });
}


// 연결 끊김 처리
function disconnected() {
  online = false;
  sample = null;

  text('connection', '연결 끊김');

  $('connection').classList.remove('online');

  text('state', 'UNKNOWN');
  text('gpio', 'GPIO0 —');

  $('powerIcon').classList.remove('active');

  text('remaining', '상태 확인 불가');

  $('progress').value = 0;

  for (const id of [
    'staStatus',
    'staIp',
    'apStatus',
    'apIp',
    'rssi',
    'uptime',
    'mode'
  ]) {
    text(id, '—');
  }

  controls();
}


// 대시보드 상태 표시
function render(s) {
  const r = s.relay || {},
    w = s.wifi || {},
    sys = s.system || {},
    i = r.inching || {};

  if (typeof r.state !== 'boolean') {
    throw Error('잘못된 상태 응답');
  }

  online = true;

  text('connection', '연결됨');

  $('connection').classList.add('online');

  text('state', r.state ? 'ON' : 'OFF');
  text('gpio', 'GPIO0 ' + (r.state ? 'LOW' : 'HIGH'));

  $('powerIcon').classList.toggle('active', r.state);

  text(
    'mode',
    i.enabled
      ? '인칭 모드 · ' + (i.durationMs / 1000) + '초'
      : '일반 모드'
  );

  sample = i.running
    ? {
      remaining: Number(i.remainingMs) || 0,
      duration: Number(i.activeDurationMs || i.durationMs) || 1,
      at: performance.now()
    }
    : null;

  if (!sample) {
    text('remaining', '대기 중');

    $('progress').value = 0;
  }

  text(
    'staStatus',
    w.staConnected ? '연결됨' : '연결 안 됨'
  );

  text('staIp', w.staIp);

  text(
    'apStatus',
    w.apEnabled ? '활성' : '비활성'
  );

  text('apIp', w.apIp);

  text(
    'rssi',
    w.staConnected && w.rssi != null
      ? w.rssi + ' dBm'
      : '—'
  );

  text(
    'uptime',
    sys.uptime != null
      ? sys.uptime + '초'
      : '—'
  );

  text(
    'updated',
    '마지막 갱신: ' + new Date().toLocaleTimeString()
  );

  controls();
}


// 주기적인 상태 조회
async function poll() {
  if (polling || busy) {
    return;
  }

  polling = true;

  try {
    render(await api('/status'));
  } catch (e) {
    disconnected();
  } finally {
    polling = false;
  }
}

setInterval(poll, 1000);


// 인칭 모드 남은 시간 표시
setInterval(() => {
  if (!sample) {
    return;
  }

  const left = Math.max(
    0,
    sample.remaining - (performance.now() - sample.at)
  );

  text(
    'remaining',
    left > 0
      ? (left / 1000).toFixed(2) + '초 남음'
      : '해제 확인 중'
  );

  $('progress').value = Math.min(
    1,
    left / sample.duration
  );
}, 100);


// 공통 작업 처리
async function action(fn) {
  if (busy || !online) {
    return;
  }

  busy = true;

  controls();

  try {
    await fn();
  } catch (e) {
    notify('요청 실패: ' + e.message);
  } finally {
    busy = false;

    controls();

    await poll();
  }
}


// 릴레이 ON / OFF
$('on').onclick = () => action(async () => {
  await api('/relay', 'POST', {
    state: true
  });
});

$('off').onclick = () => action(async () => {
  await api('/relay', 'POST', {
    state: false
  });
});


// 설정 입력 필드 상태 동기화
function syncFields() {
  $('duration').disabled = !$('inch').checked;
  $('trigger').disabled = !$('inch').checked;
  $('boot').disabled = $('inch').checked;

  text(
    'bootHint',
    $('inch').checked
      ? '인칭 모드에서는 부팅 시 OFF가 적용됩니다.'
      : ''
  );

  $('staticFields').hidden = $('dhcp').checked;

  for (const el of $('staticFields').querySelectorAll('input')) {
    el.disabled = $('dhcp').checked;
  }
}


// 설정값 입력 필드에 반영
function fill(c) {
  const r = c.relay || {},
    i = r.inching || {},
    w = c.wifi || {},
    s = w.sta || {},
    a = w.ap || {},
    d = c.device || {};

  // 릴레이 설정
  $('inch').checked = !!i.enabled;
  $('duration').value = (i.durationMs ?? 1000) / 1000;
  $('trigger').value = i.retriggerPolicy || 'restart';
  $('boot').value = r.bootMode || 'OFF';

  // STA 설정
  $('ssid').value = s.ssid || '';
  $('staPass').value = '';
  $('dhcp').checked = s.dhcp !== false;

  for (const k of ['ip', 'subnet', 'gateway', 'dns']) {
    $(k).value = s[k] || '';
  }

  $('hostname').value = d.hostname || '';

  // AP 설정
  $('apEnabled').checked = a.enabled !== false;
  $('apSsid').value = a.ssid || '';
  $('apPass').value = '';
  $('apAddress').value = a.ip || '192.168.4.1';
  $('maxClients').value = a.maxClients || 4;

  // 장치 설정
  $('deviceName').value = d.name || '';

  syncFields();
  captureAll();
}


// 설정 불러오기
async function loadConfig() {
  config = await api('/config');

  fill(config);
  controls();
}


// 작업 확인 대화상자
async function confirmAction(message) {
  text('confirmText', message);

  const d = $('confirm');

  d.showModal();

  return new Promise(resolve => {
    d.addEventListener(
      'close',
      () => resolve(d.returnValue === 'ok'),
      { once: true }
    );
  });
}


// 탭별 설정 필드 목록
const fields = {
  relay: [
    'inch',
    'duration',
    'trigger',
    'boot'
  ],

  network: [
    'ssid',
    'staPass',
    'dhcp',
    'ip',
    'subnet',
    'gateway',
    'dns',
    'hostname',
    'apEnabled',
    'apSsid',
    'apPass',
    'apAddress',
    'maxClients'
  ],

  system: [
    'deviceName'
  ]
};

const snapshots = {};

let activeTab = 'home',
  switching = false;


// 설정 상태 스냅샷 생성
function snapshot(tab) {
  return JSON.stringify(
    (fields[tab] || []).map(id => {
      const el = $(id);

      return el.type === 'checkbox'
        ? el.checked
        : el.type === 'number' &&
          el.value !== '' &&
          Number.isFinite(el.valueAsNumber)
          ? el.valueAsNumber
          : el.value;
    })
  );
}


// 현재 탭 설정 상태 저장
function capture(tab) {
  snapshots[tab] = snapshot(tab);
}


// 모든 탭 설정 상태 저장
function captureAll() {
  for (const tab of Object.keys(fields)) {
    capture(tab);
  }
}


// 설정 변경 여부 확인
function hasChanges(tab) {
  return Object.prototype.hasOwnProperty.call(snapshots, tab) &&
    snapshot(tab) !== snapshots[tab];
}


// 이전 설정 상태 복원
function restoreTab(tab) {
  if (!Object.prototype.hasOwnProperty.call(snapshots, tab)) {
    return;
  }

  const values = JSON.parse(snapshots[tab]);

  fields[tab].forEach((id, i) => {
    const el = $(id);

    if (el.type === 'checkbox') {
      el.checked = values[i];
    } else {
      el.value = values[i];
    }
  });

  syncFields();
}


// 설정 입력 이벤트 등록
for (const f of ['relayForm', 'networkForm']) {
  $(f).addEventListener('input', syncFields);
}


// 탭 전환 처리
for (const b of document.querySelectorAll('[data-tab]')) {
  b.onclick = async () => {
    const next = b.dataset.tab;

    if (next === activeTab || switching || busy) {
      return;
    }

    switching = true;

    try {
      // 저장하지 않은 변경사항 확인
      if (hasChanges(activeTab)) {
        if (!await confirmAction(
          '저장하지 않은 변경사항을 취소할까요?'
        )) {
          return;
        }

        restoreTab(activeTab);
      }

      // 메뉴 선택 상태 변경
      for (const t of document.querySelectorAll('[data-tab]')) {
        t.setAttribute(
          'aria-selected',
          String(t === b)
        );
      }

      // 선택된 화면 표시
      for (const section of document.querySelectorAll('main>section')) {
        section.hidden = section.id !== next;
      }

      activeTab = next;

      capture(next);

      const panel = $(next);

      panel.inert = true;

      if (next !== 'home') {
        try {
          await loadConfig();

          // 시스템 정보 조회
          if (next === 'system') {
            const info = await api('/system/info');

            $('info').replaceChildren();

            for (const [k, v] of Object.entries(info)) {
              if (typeof v === 'object') {
                continue;
              }

              const dt = document.createElement('dt'),
                dd = document.createElement('dd');

              dt.textContent = k;
              dd.textContent = String(v);

              $('info').append(dt, dd);
            }
          }
        } catch (e) {
          notify('조회 실패: ' + e.message);
        }
      }
    } finally {
      $(activeTab).inert = false;

      switching = false;

      controls();
    }
  };
}


// 설정 취소 버튼
for (const b of document.querySelectorAll('.cancel')) {
  b.onclick = () => restoreTab(activeTab);
}


// 공통 설정 저장
async function save(patch) {
  await api('/config', 'PUT', patch);

  capture(activeTab);

  await loadConfig();

  notify('설정 저장 완료');
}


// 릴레이 설정 저장
$('relayForm').onsubmit = e => {
  e.preventDefault();

  action(() => save({
    relay: {
      bootMode: $('inch').checked
        ? 'OFF'
        : $('boot').value,

      inching: {
        enabled: $('inch').checked,

        durationMs: Math.round(
          Number($('duration').value) * 1000
        ),

        retriggerPolicy: $('trigger').value
      }
    }
  }));
};


// IP 주소 유효성 검사
const validIp = v =>
  /^\d{1,3}(\.\d{1,3}){3}$/.test(v) &&
  v.split('.').every(x => Number(x) <= 255);


// 네트워크 설정 저장
$('networkForm').onsubmit = async e => {
  e.preventDefault();

  const sta = {
    ssid: $('ssid').value,
    dhcp: $('dhcp').checked
  };

  const ap = {
    enabled: $('apEnabled').checked,
    ssid: $('apSsid').value,
    ip: $('apAddress').value,
    maxClients: Number($('maxClients').value)
  };

  // 고정 IP 설정 검사
  if (!sta.dhcp) {
    for (const k of ['ip', 'subnet', 'gateway', 'dns']) {
      sta[k] = $(k).value;

      if (!validIp(sta[k])) {
        return notify(k + ' 주소 오류');
      }
    }
  }

  // AP IP 주소 검사
  if (!validIp(ap.ip)) {
    return notify('AP IP 주소 오류');
  }

  // 비밀번호가 입력된 경우에만 변경
  if ($('staPass').value) {
    sta.password = $('staPass').value;
  }

  if ($('apPass').value) {
    ap.password = $('apPass').value;
  }

  // 네트워크 변경 확인
  if (!await confirmAction(
    '네트워크 변경으로 연결이 끊길 수 있습니다. AP 주소: ' +
    ap.ip +
    ' · 적용할까요?'
  )) {
    return;
  }

  action(async () => {
    await api('/config', 'PUT', {
      wifi: {
        sta,
        ap
      },

      device: {
        hostname: $('hostname').value
      }
    });

    $('staPass').value = '';
    $('apPass').value = '';

    capture('network');

    notify('저장 응답 수신. 필요하면 새 IP로 재접속하세요.');
  });
};


// Wi-Fi 네트워크 검색
$('scan').onclick = () => action(async () => {
  const result = await api('/wifi/scan');

  $('networks').replaceChildren();

  for (const n of result.networks || []) {
    const b = document.createElement('button');

    b.type = 'button';
    b.className = 'secondary';
    b.textContent = n.ssid + ' · ' + n.rssi + ' dBm';

    b.onclick = () => {
      $('ssid').value = n.ssid;
    };

    $('networks').append(b);
  }

  if (!result.networks?.length) {
    notify('검색 결과 없음 또는 검색 진행 중');
  }
});


// 장치 이름 저장
$('saveName').onclick = () => action(() => save({
  device: {
    name: $('deviceName').value
  }
}));


// 설정 백업
$('backup').onclick = () => action(async () => {
  const c = await api('/config');

  // 민감정보 제거
  function redact(o) {
    for (const k of Object.keys(o)) {
      if (/pass|secret|token|hash|credential/i.test(k)) {
        delete o[k];
      } else if (o[k] && typeof o[k] === 'object') {
        redact(o[k]);
      }
    }
  }

  redact(c);

  const url = URL.createObjectURL(
    new Blob(
      [JSON.stringify(c, null, 2)],
      { type: 'application/json' }
    )
  );

  const a = document.createElement('a');

  a.href = url;
  a.download = 'relay-config.json';

  a.click();

  setTimeout(
    () => URL.revokeObjectURL(url),
    1000
  );
});


// 설정 복원
$('restore').onclick = async () => {
  const f = $('restoreFile').files[0];

  if (!f || f.size > 16384) {
    return notify('16KB 이하 JSON 파일을 선택하세요.');
  }

  if (await confirmAction(
    '설정을 복원할까요? 네트워크가 변경될 수 있습니다.'
  )) {
    action(async () => {
      const c = JSON.parse(await f.text());

      if (!c || Array.isArray(c) || typeof c !== 'object') {
        throw Error('잘못된 설정');
      }

      await api('/config', 'PUT', c);

      notify('복원 응답 수신');
    });
  }
};


// 재부팅 및 공장 초기화
for (const [id, path, msg] of [
  [
    'reboot',
    '/system/reboot',
    '재부팅할까요?'
  ],
  [
    'reset',
    '/system/reset',
    '모든 사용자 설정을 초기화할까요?'
  ]
]) {
  $(id).onclick = async () => {
    if (await confirmAction(msg)) {
      action(async () => {
        await api(path, 'POST', {});

        config = null;

        captureAll();
        disconnected();

        notify('요청 접수. 장치 재접속을 기다려 주세요.');
      });
    }
  };
}


// 초기화
syncFields();
captureAll();
controls();
poll();