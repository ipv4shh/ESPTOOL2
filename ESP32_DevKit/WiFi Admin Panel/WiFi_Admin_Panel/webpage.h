#pragma once
#include <Arduino.h>

const String webpage = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESPTOOL2 | CONTROL CONSOLE</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --primary-color: #ff4122;
      --bg-color: #050508;
      --panel-bg: #09090b;
      --border-color: #27272a;
      --text-main: #e4e4e7;
      --text-muted: #71717a;
      --green-status: #10b981;
      --red-status: #ef4444;
      --amber-status: #f59e0b;
    }
    * { margin:0; padding:0; box-sizing:border-box; font-family:'JetBrains Mono',monospace; }
    body { background:var(--bg-color); color:var(--text-main); display:flex; justify-content:center; align-items:center; min-height:100vh; padding:16px; }
    .container { background:var(--panel-bg); padding:20px; border-radius:8px; max-width:680px; width:100%; border:1px solid var(--border-color); text-align:center; box-shadow:0 4px 24px rgba(0,0,0,0.4); }
    .ascii-art { font-size:8px; line-height:1.1; color:var(--primary-color); text-align:center; margin-bottom:16px; white-space:pre; display:block; overflow-x:auto; }
    @media(min-width:480px){ .ascii-art{font-size:10px;} }
    .sub { color:var(--text-muted); font-size:10px; margin-bottom:16px; letter-spacing:1px; text-transform:uppercase; }
    .connection-status { display:flex; align-items:center; justify-content:space-between; background:#020203; border:1px solid var(--border-color); padding:8px 12px; border-radius:4px; margin-bottom:12px; font-size:11px; }
    .status-info { display:flex; align-items:center; gap:8px; }
    .status-dot { width:6px; height:6px; border-radius:50%; display:inline-block; transition:background 0.3s ease; }
    .status-dot.offline { background:var(--red-status); }
    .status-dot.online { background:var(--green-status); }
    .status-dot.testing { background:var(--amber-status); }
    .btn-check,.btn-clear { background:transparent; border:1px solid var(--border-color); color:var(--text-main); padding:4px 10px; border-radius:2px; cursor:pointer; font-size:11px; transition:all 0.2s ease; }
    .btn-check:hover,.btn-clear:hover { border-color:var(--text-muted); background:rgba(255,255,255,0.01); }
    .current-target { display:flex; align-items:center; justify-content:space-between; background:rgba(245,158,11,0.08); border:1px solid rgba(245,158,11,0.25); padding:8px 12px; border-radius:4px; margin-bottom:12px; font-size:11px; color:var(--amber-status); }
    .tabs { display:flex; gap:4px; margin-bottom:12px; background:#020203; padding:2px; border-radius:4px; border:1px solid var(--border-color); overflow-x:auto; }
    .tab { flex:1; padding:6px; border-radius:2px; border:none; background:transparent; color:var(--text-muted); font-size:11px; font-weight:600; cursor:pointer; text-align:center; white-space:nowrap; transition:all 0.2s ease; }
    .tab:hover { color:var(--text-main); }
    .tab.active { background:rgba(255,65,34,0.08); color:var(--primary-color); border:1px solid rgba(255,65,34,0.15); }
    .tab-content { display:none; }
    .tab-content.active { display:block; }
    .grid { display:grid; grid-template-columns:1fr 1fr; gap:6px; margin-bottom:16px; }
    .attack-btn { padding:10px; border-radius:2px; border:1px solid var(--border-color); background:transparent; color:var(--text-main); cursor:pointer; text-align:center; display:flex; flex-direction:column; align-items:center; transition:all 0.2s ease; }
    .attack-btn:hover { border-color:var(--primary-color); box-shadow:0 0 12px rgba(255,65,34,0.15); }
    .attack-btn.selected { border-color:var(--primary-color); background:rgba(255,65,34,0.05); }
    .attack-btn .label { font-size:11px; font-weight:600; }
    .action-row { display:flex; gap:6px; margin-bottom:12px; }
    .action-row button { flex:1; padding:10px; border:1px solid var(--border-color); border-radius:4px; font-size:11px; font-weight:700; cursor:pointer; background:transparent; color:var(--text-main); transition:all 0.2s ease; }
    .btn-start { border-color:var(--primary-color)!important; color:var(--primary-color)!important; }
    .btn-start:hover { background:rgba(255,65,34,0.05); }
    .btn-stop:hover { border-color:var(--text-muted); }
    .btn-multi { border-color:var(--amber-status)!important; color:var(--amber-status)!important; }
    .btn-multi:hover { background:rgba(245,158,11,0.05); }
    .status { text-align:center; padding:10px; background:#020203; border-radius:4px; border:1px solid var(--border-color); font-size:11px; color:var(--text-muted); min-height:36px; display:flex; justify-content:center; align-items:center; margin-bottom:12px; }
    .console-header { display:flex; justify-content:space-between; align-items:center; margin:12px 0 6px; font-size:10px; color:var(--text-muted); }
    .console-box { background:#020203; border:1px solid var(--border-color); border-radius:4px; padding:10px; font-size:11px; line-height:1.4; color:#34d399; height:120px; overflow-y:auto; white-space:pre-wrap; text-align:left; }
    .footer { text-align:center; margin-top:16px; font-size:9px; color:var(--text-muted); }
    .footer a { color:var(--text-muted); text-decoration:none; }
    @media(max-width:480px){ .grid{grid-template-columns:1fr;} }
    .dashboard-section { background:#020203; border:1px solid var(--border-color); border-radius:4px; padding:10px; margin-top:12px; text-align:left; }
    .dashboard-box { margin-top:6px; max-height:160px; overflow-y:auto; }
    .scan-table { width:100%; border-collapse:collapse; font-size:11px; }
    .scan-table th,.scan-table td { padding:4px 6px; text-align:left; border-bottom:1px solid var(--border-color); }
    .scan-table th { color:var(--text-muted); font-weight:600; font-size:9px; }
    .client-card { background:transparent; border:1px solid var(--border-color); border-radius:2px; padding:6px 10px; margin-bottom:3px; display:flex; justify-content:space-between; align-items:center; }
    .client-mac { font-weight:700; color:#34d399; font-size:11px; }
    .client-status { font-size:9px; padding:1px 3px; border-radius:2px; background:rgba(16,185,129,0.05); color:#10b981; border:1px solid rgba(16,185,129,0.1); }
    .modal { display:none; position:fixed; z-index:1000; left:0; top:0; width:100%; height:100%; background-color:rgba(0,0,0,0.85); align-items:center; justify-content:center; }
    .modal-content { background-color:#0c0c0e; margin:auto; padding:16px; border:1px solid var(--primary-color); border-radius:4px; width:85%; max-width:450px; color:#fca5a5; font-size:11px; position:relative; text-align:left; }
    .close-btn { color:var(--text-muted); position:absolute; top:4px; right:10px; font-size:18px; font-weight:bold; cursor:pointer; }
    .btn-select { background:transparent; border:1px solid var(--green-status); color:var(--green-status); padding:2px 8px; border-radius:2px; cursor:pointer; font-size:10px; transition:all 0.2s ease; font-family:'JetBrains Mono',monospace; }
    .btn-select:hover { background:rgba(16,185,129,0.1); }
    .subtab { padding:4px 12px; border:1px solid var(--border-color); background:transparent; color:var(--text-muted); font-size:10px; cursor:pointer; border-radius:2px; transition:all 0.2s ease; font-family:'JetBrains Mono',monospace; }
    .subtab.active { border-color:var(--primary-color); color:var(--primary-color); background:rgba(255,65,34,0.08); }
    .target-subtabs { display:flex; gap:4px; margin-bottom:8px; }
    .target-panel { display:none; max-height:200px; overflow-y:auto; }
    .target-panel.active { display:block; }
    .multi-check { display:block; padding:6px 8px; margin-bottom:2px; border-radius:2px; cursor:pointer; font-size:11px; color:var(--text-main); transition:background 0.2s ease; }
    .multi-check:hover { background:rgba(255,255,255,0.03); }
    .multi-check input { accent-color:var(--primary-color); margin-right:8px; }
  </style>
</head>
<body>
<div class="container">
  <pre class="ascii-art">
 _____ ____  ____ _____ ___   ___  _     ____  
| ____/ ___||  _ \_   _/ _ \ / _ \| |   |___ \ 
|  _| \___ \| |_) || || | | | | | | |     __) |
| |___ ___) |  __/ | || |_| | |_| | |___ / __/ 
|_____|____/|_|    |_| \___/ \___/|_____|_____|</pre>
  <div class="sub">Multi-band Wireless Console v1.2</div>

  <div class="connection-status" id="conn-status">
    <div class="status-info">
      <span class="status-dot offline" id="status-dot"></span>
      <span id="status-text">Slave ESP32-S3: OFFLINE</span>
    </div>
    <button class="btn-check" onclick="checkConnection()">REFRESH</button>
  </div>

  <div id="current-target" class="current-target" style="display:none;">
    <span id="target-text">No target</span>
    <button class="btn-clear" onclick="clearTarget()">CLEAR</button>
  </div>

  <div class="tabs" id="tabs">
    <button class="tab active" data-tab="wifi">Wi-Fi</button>
    <button class="tab" data-tab="ble">Bluetooth</button>
    <button class="tab" data-tab="ghz">2.4GHz</button>
    <button class="tab" data-tab="subghz">Sub-GHz</button>
    <button class="tab" data-tab="ir">IR</button>
    <button class="tab" data-tab="targets">Targets</button>
  </div>

  <div id="tab-wifi" class="tab-content active">
    <div class="grid">
      <button class="attack-btn selected" data-attack="beacon" onclick="selectAttack(this)"><span class="label">Beacon Spam</span></button>
      <button class="attack-btn" data-attack="deauth" onclick="selectAttack(this)"><span class="label">Deauth</span></button>
      <button class="attack-btn" data-attack="probe" onclick="selectAttack(this)"><span class="label">Probe Flood</span></button>
      <button class="attack-btn" data-attack="wifi_scan" onclick="selectAttack(this)"><span class="label">Wi-Fi Scan</span></button>
      <button class="attack-btn" data-attack="evil_twin" onclick="selectAttack(this)"><span class="label">Evil Twin</span></button>
      <button class="attack-btn" data-attack="powerful_jammer" onclick="selectAttack(this)"><span class="label">Powerful Jammer</span></button>
    </div>
  </div>

  <div id="tab-ble" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ble_scan" onclick="selectAttack(this)"><span class="label">BLE Scan</span></button>
      <button class="attack-btn" data-attack="ble_spoofer" onclick="selectAttack(this)"><span class="label">BLE Spoofer</span></button>
      <button class="attack-btn" data-attack="ble_jammer" onclick="selectAttack(this)"><span class="label">BLE Jammer</span></button>
      <button class="attack-btn" data-attack="sour_apple" onclick="selectAttack(this)"><span class="label">Sour Apple</span></button>
    </div>
  </div>

  <div id="tab-ghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ghz_scan" onclick="selectAttack(this)"><span class="label">Spectrum Analyzer</span></button>
      <button class="attack-btn" data-attack="protokill" onclick="selectAttack(this)"><span class="label">Protokill</span></button>
    </div>
  </div>

  <div id="tab-subghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="subghz_scan" onclick="selectAttack(this)"><span class="label">Sub-GHz Scanner</span></button>
      <button class="attack-btn" data-attack="subghz_replay" onclick="selectAttack(this)"><span class="label">Replay</span></button>
      <button class="attack-btn" data-attack="subghz_jammer" onclick="selectAttack(this)"><span class="label">Jammer</span></button>
    </div>
  </div>

  <div id="tab-ir" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ir_scan" onclick="selectAttack(this)"><span class="label">IR Scanner</span></button>
      <button class="attack-btn" data-attack="ir_replay" onclick="selectAttack(this)"><span class="label">IR Replay</span></button>
    </div>
  </div>

  <div id="tab-targets" class="tab-content">
    <div class="target-subtabs">
      <button class="subtab active" onclick="showTargetType('wifi',this)">Wi-Fi</button>
      <button class="subtab" onclick="showTargetType('ble',this)">BLE</button>
      <button class="subtab" onclick="showTargetType('subghz',this)">Sub-GHz</button>
      <button class="subtab" onclick="showTargetType('ir',this)">IR</button>
    </div>
    <div id="target-wifi" class="target-panel active"><div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run Wi-Fi Scan to populate targets</div></div>
    <div id="target-ble" class="target-panel"><div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run BLE Scan to populate targets</div></div>
    <div id="target-subghz" class="target-panel"><div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run Sub-GHz Scan to populate targets</div></div>
    <div id="target-ir" class="target-panel"><div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run IR Scan to populate targets</div></div>
  </div>

  <div style="display:flex;align-items:center;justify-content:flex-start;gap:8px;margin-bottom:4px;font-size:11px;padding-left:2px;">
    <input type="checkbox" id="boost-mode" onchange="toggleBoostMode(this)" style="accent-color:var(--primary-color);">
    <label for="boost-mode" style="cursor:pointer;color:var(--text-muted);">Enable Boost Mode</label>
  </div>
  <div style="display:flex;align-items:center;justify-content:flex-start;gap:8px;margin-bottom:12px;font-size:11px;padding-left:2px;">
    <input type="checkbox" id="extreme-mode" onchange="toggleExtremeMode(this)" style="accent-color:#ef4444;">
    <label for="extreme-mode" style="cursor:pointer;color:#ef4444;font-weight:700;">&#9888; EXTREME</label>
  </div>

  <div class="action-row">
    <button class="btn-start" onclick="sendCmd('start')">START</button>
    <button class="btn-stop" onclick="sendCmd('stop')">STOP</button>
    <button class="btn-multi" onclick="openMultiAttack()">MULTI ATTACK</button>
  </div>

  <div class="status" id="status">STATUS: READY</div>

  <div class="dashboard-section" id="dashboard-section" style="display:none;">
    <div class="console-header" style="margin-top:0;">
      <span id="dashboard-title">SCAN RESULTS</span>
    </div>
    <div class="dashboard-box" id="dashboard-box"></div>
  </div>

  <div class="console-header">
    <span>CONSOLE LOGS</span>
    <button class="btn-clear" onclick="clearLogs()">CLEAR</button>
  </div>
  <div class="console-box" id="console-box">Awaiting connection...</div>

  <div class="footer">ESPTOOL2 v1.2 &middot; <a href="#" onclick="location.reload()">Reload Console</a></div>
</div>

<div id="modal-container" class="modal">
  <div class="modal-content">
    <span class="close-btn" onclick="closeModal()">&times;</span>
    <div id="modal-text"></div>
  </div>
</div>

<div id="multi-modal" class="modal">
  <div class="modal-content" style="border-color:var(--amber-status);">
    <span class="close-btn" onclick="closeMultiModal()">&times;</span>
    <div style="color:var(--primary-color);font-weight:700;font-size:13px;margin-bottom:12px;">MULTI ATTACK</div>
    <div style="max-height:280px;overflow-y:auto;">
      <label class="multi-check"><input type="checkbox" value="beacon"> Beacon Spam</label>
      <label class="multi-check"><input type="checkbox" value="deauth"> Deauth</label>
      <label class="multi-check"><input type="checkbox" value="probe"> Probe Flood</label>
      <label class="multi-check"><input type="checkbox" value="evil_twin"> Evil Twin</label>
      <label class="multi-check"><input type="checkbox" value="powerful_jammer"> Powerful Jammer</label>
      <label class="multi-check"><input type="checkbox" value="ble_jammer"> BLE Jammer</label>
      <label class="multi-check"><input type="checkbox" value="ble_spoofer"> BLE Spoofer</label>
      <label class="multi-check"><input type="checkbox" value="sour_apple"> Sour Apple</label>
      <label class="multi-check"><input type="checkbox" value="subghz_jammer"> Sub-GHz Jammer</label>
    </div>
    <div style="margin-top:12px;display:flex;gap:8px;align-items:center;">
      <select id="multi-mode" style="flex:0 0 auto;padding:6px 10px;background:#020203;border:1px solid var(--border-color);color:var(--text-main);font-size:11px;border-radius:4px;outline:none;font-family:'JetBrains Mono',monospace;">
        <option value="normal">Normal</option>
        <option value="boost">Boost</option>
        <option value="extreme">&#9888; Extreme</option>
      </select>
      <button class="btn-start" onclick="startMultiAttack()" style="flex:1;">LAUNCH</button>
    </div>
  </div>
</div>

<script>
  let selectedAttack='beacon';
  let isBoostMode=false;
  let isExtremeMode=false;
  let currentTarget=null;
  const warnings={
    subghz_scan:'External CC1101 module is required for Sub-GHz scan!',
    subghz_replay:'External CC1101 module is required for Sub-GHz Replay!',
    subghz_jammer:'External CC1101 module is required for Sub-GHz Jammer!',
    ir_scan:'Connected IR Receiver (e.g. TSOP) is required for IR Scan!',
    ir_replay:'Connected IR LED is required for IR Replay!'
  };
  const reportingAttacks=['wifi_scan','ble_scan','evil_twin','subghz_scan','ir_scan','ghz_scan'];

  document.querySelectorAll('.tab').forEach(tab=>{
    tab.addEventListener('click',function(){
      document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
      this.classList.add('active');
      document.getElementById('tab-'+this.dataset.tab).classList.add('active');
      if(this.dataset.tab==='targets') updateTargetTables();
    });
  });

  function selectAttack(btn){
    document.querySelectorAll('.attack-btn').forEach(b=>b.classList.remove('selected'));
    btn.classList.add('selected');
    selectedAttack=btn.dataset.attack;
    document.getElementById('status').innerHTML='TARGET: '+btn.querySelector('.label').textContent.toUpperCase();
    document.getElementById('dashboard-box').innerHTML='';
    if(warnings[selectedAttack]) showModal(warnings[selectedAttack]);
    updateDashboardUI();
  }

  function toggleBoostMode(cb){
    if(cb.checked){
      if(confirm("WARNING: Boost Mode increases transmitter output and packet rate. Proceed?")){
        isBoostMode=true;
      }else{cb.checked=false;isBoostMode=false;}
    }else{isBoostMode=false;}
  }

  function toggleExtremeMode(cb){
    if(cb.checked){
      if(confirm("\u26a0 EXTREME MODE: Maximum packet rate, minimal delays. High risk of overheating. Are you sure?")){
        isExtremeMode=true;
        document.getElementById('boost-mode').checked=true;
        isBoostMode=true;
      }else{cb.checked=false;isExtremeMode=false;}
    }else{isExtremeMode=false;}
  }

  function showModal(text){document.getElementById('modal-text').innerHTML=text;document.getElementById('modal-container').style.display='flex';}
  function closeModal(){document.getElementById('modal-container').style.display='none';}
  function openMultiAttack(){document.getElementById('multi-modal').style.display='flex';}
  function closeMultiModal(){document.getElementById('multi-modal').style.display='none';}

  function startMultiAttack(){
    var checks=document.querySelectorAll('#multi-modal input[type=checkbox]:checked');
    var attacks=[];
    for(var i=0;i<checks.length;i++) attacks.push(checks[i].value);
    if(attacks.length===0){alert('Select at least one attack');return;}
    var mode=document.getElementById('multi-mode').value;
    fetch('/multi_attack',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({attacks:attacks,mode:mode})})
      .then(function(r){return r.text();})
      .then(function(resp){document.getElementById('status').innerHTML='STATUS: '+resp.toUpperCase();closeMultiModal();})
      .catch(function(){document.getElementById('status').innerHTML='ERROR: FAILED TO SEND';});
  }

  // Target management
  function selectWifiTarget(idx){var n=wifiNetworks[idx];selectTarget('wifi',{ssid:n.ssid,bssid:n.bssid,rssi:n.rssi,channel:n.channel,security:n.enc,wps:n.wps});}
  function selectBleTarget(idx){var d=bleDevices[idx];selectTarget('ble',{name:d.name,addr:d.addr,rssi:d.rssi});}
  function selectSubghzTarget(idx){var d=subghzDevices[idx];selectTarget('subghz',{freq:d.freq,rssi:d.rssi,mod:d.mod});}
  function selectIrTarget(idx){var d=irDevices[idx];selectTarget('ir',{proto:d.proto,addr:d.addr,cmd:d.cmd,len:d.len});}

  function selectTarget(type,data){
    data.type=type;
    fetch('/select_target',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
      .then(function(r){return r.text();})
      .then(function(resp){
        currentTarget=data;
        updateTargetDisplay();
        document.getElementById('status').innerHTML='TARGET SELECTED: '+(data.ssid||data.name||data.freq||data.proto);
      });
  }

  function clearTarget(){
    fetch('/clear_target').then(function(r){return r.text();}).then(function(){
      currentTarget=null;
      updateTargetDisplay();
      document.getElementById('status').innerHTML='STATUS: TARGET CLEARED';
    });
  }

  function updateTargetDisplay(){
    var bar=document.getElementById('current-target');
    var text=document.getElementById('target-text');
    if(currentTarget){
      bar.style.display='flex';
      var label=currentTarget.type.toUpperCase()+': ';
      if(currentTarget.type==='wifi') label+=currentTarget.ssid+' (BSSID: '+currentTarget.bssid+')';
      else if(currentTarget.type==='ble') label+=currentTarget.name+' ('+currentTarget.addr+')';
      else if(currentTarget.type==='subghz') label+=currentTarget.freq;
      else if(currentTarget.type==='ir') label+=currentTarget.proto+' '+currentTarget.cmd;
      text.innerHTML=label;
    }else{bar.style.display='none';}
  }

  function showTargetType(type,btn){
    document.querySelectorAll('.target-panel').forEach(function(p){p.style.display='none';p.classList.remove('active');});
    document.querySelectorAll('.subtab').forEach(function(s){s.classList.remove('active');});
    var el=document.getElementById('target-'+type);
    if(el){el.style.display='block';el.classList.add('active');}
    if(btn) btn.classList.add('active');
    updateTargetTables();
  }

  function updateTargetTables(){
    var wp=document.getElementById('target-wifi');
    if(wp&&wp.classList.contains('active')){
      if(wifiNetworks.length===0){wp.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run Wi-Fi Scan to populate targets</div>';}
      else{var h='<table class="scan-table"><thead><tr><th>SSID</th><th>CH</th><th>BSSID</th><th>RSSI</th><th>SEC</th><th></th></tr></thead><tbody>';
        for(var i=0;i<wifiNetworks.length;i++){var n=wifiNetworks[i];h+='<tr><td style="font-weight:600;color:#fff;">'+n.ssid+'</td><td style="color:#34d399;">'+n.channel+'</td><td style="color:var(--text-muted);">'+n.bssid+'</td><td>'+n.rssi+' dBm</td><td>'+n.enc+'</td><td><button class="btn-select" onclick="selectWifiTarget('+i+')">SELECT</button></td></tr>';}
        h+='</tbody></table>';wp.innerHTML=h;}}
    var bp=document.getElementById('target-ble');
    if(bp&&bp.classList.contains('active')){
      if(bleDevices.length===0){bp.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run BLE Scan to populate targets</div>';}
      else{var h='<table class="scan-table"><thead><tr><th>NAME</th><th>MAC</th><th>RSSI</th><th>TYPE</th><th></th></tr></thead><tbody>';
        for(var i=0;i<bleDevices.length;i++){var d=bleDevices[i];h+='<tr><td style="font-weight:600;color:#fff;">'+d.name+'</td><td style="color:var(--text-muted);">'+d.addr+'</td><td>'+d.rssi+' dBm</td><td style="color:var(--primary-color);">'+d.type+'</td><td><button class="btn-select" onclick="selectBleTarget('+i+')">SELECT</button></td></tr>';}
        h+='</tbody></table>';bp.innerHTML=h;}}
    var sp=document.getElementById('target-subghz');
    if(sp&&sp.classList.contains('active')){
      if(subghzDevices.length===0){sp.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run Sub-GHz Scan to populate targets</div>';}
      else{var h='<table class="scan-table"><thead><tr><th>FREQ</th><th>RSSI</th><th>MOD</th><th></th></tr></thead><tbody>';
        for(var i=0;i<subghzDevices.length;i++){var d=subghzDevices[i];h+='<tr><td style="font-weight:600;color:#fff;">'+d.freq+'</td><td style="color:var(--primary-color);">'+d.rssi+'</td><td style="color:#34d399;">'+d.mod+'</td><td><button class="btn-select" onclick="selectSubghzTarget('+i+')">SELECT</button></td></tr>';}
        h+='</tbody></table>';sp.innerHTML=h;}}
    var ip=document.getElementById('target-ir');
    if(ip&&ip.classList.contains('active')){
      if(irDevices.length===0){ip.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">Run IR Scan to populate targets</div>';}
      else{var h='<table class="scan-table"><thead><tr><th>PROTO</th><th>ADDR</th><th>CMD</th><th>LEN</th><th></th></tr></thead><tbody>';
        for(var i=0;i<irDevices.length;i++){var d=irDevices[i];h+='<tr><td style="font-weight:600;color:#fff;">'+d.proto+'</td><td style="color:var(--primary-color);">'+d.addr+'</td><td style="color:#34d399;">'+d.cmd+'</td><td>'+d.len+'</td><td><button class="btn-select" onclick="selectIrTarget('+i+')">SELECT</button></td></tr>';}
        h+='</tbody></table>';ip.innerHTML=h;}}
  }

  let wifiNetworks=[];
  let bleDevices=[];
  let evilTwinClients=[];
  let subghzDevices=[];
  let irDevices=[];
  let nrfChannels=[];
  let isClearing=false;

  function parseLogs(text){
    var lines=text.split('\n');
    var wifiTemp=[],bleTemp=[],evilTemp=[],subghzTemp=[],irTemp=[];
    var nrfTemp=[].concat(nrfChannels);
    lines.forEach(function(line){
      if(line.includes('[WIFI_DEV]')){
        var parts=line.split('[WIFI_DEV]')[1].split('|');
        if(parts.length>=7) wifiTemp.push({ssid:parts[0],rssi:parseInt(parts[1]),enc:parts[2],bssid:parts[3],wps:parts[4],clients:parseInt(parts[5]),channel:parseInt(parts[6])});
        else if(parts.length>=4) wifiTemp.push({ssid:parts[0],rssi:parseInt(parts[1]),enc:parts[2],bssid:parts[3],wps:'WPS_DISABLED',clients:0,channel:1});
      }
      else if(line.includes('[BLE_DEV]')){
        var parts=line.split('[BLE_DEV]')[1].split('|');
        if(parts.length>=6) bleTemp.push({name:parts[0],addr:parts[1],rssi:parseInt(parts[2]),type:parts[3],percent:parts[4],bar:parts[5]});
        else if(parts.length>=3) bleTemp.push({name:parts[0],addr:parts[1],rssi:parseInt(parts[2]),type:'Unknown',percent:'50',bar:'IIIII.....'});
      }
      else if(line.includes('[EVIL_CLIENT]')){
        var mac=line.split('[EVIL_CLIENT]')[1].trim();
        if(mac&&evilTemp.indexOf(mac)<0) evilTemp.push(mac);
      }
      else if(line.includes('[SUBGHZ_DEV]')){
        var parts=line.split('[SUBGHZ_DEV]')[1].split('|');
        if(parts.length>=3) subghzTemp.push({freq:parts[0],rssi:parts[1],mod:parts[2]});
      }
      else if(line.includes('[IR_DEV]')){
        var parts=line.split('[IR_DEV]')[1].split('|');
        if(parts.length>=4) irTemp.push({proto:parts[0],addr:parts[1],cmd:parts[2],len:parts[3]});
      }
      else if(line.includes('[NRF_DEV]')){
        var parts=line.split('[NRF_DEV]')[1].split('|');
        if(parts.length>=3){var ch=parseInt(parts[0]);nrfTemp[ch]={ch:ch,dbm:parts[1],graph:parts[2]};}
      }
    });
    wifiNetworks=wifiTemp;bleDevices=bleTemp;evilTwinClients=evilTemp;subghzDevices=subghzTemp;irDevices=irTemp;nrfChannels=nrfTemp;
    updateDashboardUI();
    updateTargetTables();
  }

  function updateDashboardUI(){
    var dashSection=document.getElementById('dashboard-section');
    var dashBox=document.getElementById('dashboard-box');
    var dashTitle=document.getElementById('dashboard-title');
    if(reportingAttacks.indexOf(selectedAttack)<0){dashSection.style.display='none';return;}
    dashSection.style.display='block';
    if(selectedAttack==='wifi_scan'){
      dashTitle.innerHTML='WIFI NETWORKS DETECTED';
      if(wifiNetworks.length===0){dashBox.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">AWAITING WIFI SCAN RESULTS...</div>';}
      else{var html='<table class="scan-table"><thead><tr><th>SSID</th><th>CH</th><th>BSSID</th><th>RSSI</th><th>SECURITY</th><th>WPS</th><th>CLIENTS</th></tr></thead><tbody>';
        wifiNetworks.forEach(function(net){
          var wpsColor=net.wps==='WPS_ENABLED'?'#10b981':'var(--text-muted)';
          var wpsLabel=net.wps==='WPS_ENABLED'?'YES':'NO';
          html+='<tr><td style="font-weight:600;color:#fff;">'+net.ssid+'</td><td style="color:#34d399;">'+net.channel+'</td><td style="color:var(--text-muted);">'+net.bssid+'</td><td>'+net.rssi+' dBm</td><td><span style="color:'+(net.enc==='OPEN'?'var(--primary-color)':'var(--text-muted)')+';">'+net.enc+'</span></td><td style="color:'+wpsColor+';font-weight:bold;">'+wpsLabel+'</td><td style="text-align:center;font-weight:bold;color:#fca5a5;">'+net.clients+'</td></tr>';
        });html+='</tbody></table>';dashBox.innerHTML=html;}}
    else if(selectedAttack==='ble_scan'){
      dashTitle.innerHTML='BLE DEVICES DETECTED';
      if(!document.getElementById('ble-filter-container')){
        dashBox.innerHTML='<div id="ble-filter-container" style="margin-bottom:8px;"><input type="text" id="ble-filter" oninput="updateDashboardUI()" placeholder="Filter by name..." style="width:100%;padding:8px;background:#020203;border:1px solid var(--border-color);color:var(--text-main);font-size:11px;border-radius:4px;outline:none;"></div><div id="ble-table-container"></div>';}
      var filterInput=document.getElementById('ble-filter');
      var filter=filterInput?filterInput.value.toLowerCase():'';
      var tableBox=document.getElementById('ble-table-container');
      var filtered=bleDevices.filter(function(dev){return dev.name.toLowerCase().indexOf(filter)>=0;});
      if(filtered.length===0){tableBox.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">NO MATCHING DEVICES</div>';}
      else{var html='<table class="scan-table"><thead><tr><th>DEVICE</th><th>TYPE</th><th>MAC</th><th>RSSI</th><th>LEVEL</th></tr></thead><tbody>';
        filtered.forEach(function(dev){html+='<tr><td style="font-weight:600;color:#fff;">'+dev.name+'</td><td style="color:var(--primary-color);font-weight:bold;font-size:10px;">'+dev.type.toUpperCase()+'</td><td style="color:var(--text-muted);">'+dev.addr+'</td><td>'+dev.rssi+' dBm ('+dev.percent+'%)</td><td style="color:#34d399;">['+dev.bar+']</td></tr>';});
        html+='</tbody></table>';tableBox.innerHTML=html;}}
    else if(selectedAttack==='evil_twin'){
      dashTitle.innerHTML='EVIL TWIN CONNECTED CLIENTS';
      if(evilTwinClients.length===0){dashBox.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">NO CLIENTS CONNECTED</div>';}
      else{var html='<div style="display:grid;grid-template-columns:1fr;gap:4px;">';
        evilTwinClients.forEach(function(client){html+='<div class="client-card"><span class="client-mac">'+client+'</span><span class="client-status">CONNECTED</span></div>';});
        html+='</div>';dashBox.innerHTML=html;}}
    else if(selectedAttack==='subghz_scan'){
      dashTitle.innerHTML='SUB-GHZ SIGNALS DETECTED';
      if(subghzDevices.length===0){dashBox.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">AWAITING SUB-GHZ SCAN...</div>';}
      else{var html='<table class="scan-table"><thead><tr><th>FREQUENCY</th><th>SIGNAL</th><th>MODULATION</th></tr></thead><tbody>';
        subghzDevices.forEach(function(dev){html+='<tr><td style="font-weight:600;color:#fff;">'+dev.freq+'</td><td style="color:var(--primary-color);font-weight:bold;">'+dev.rssi+'</td><td style="color:#34d399;">'+dev.mod+'</td></tr>';});
        html+='</tbody></table>';dashBox.innerHTML=html;}}
    else if(selectedAttack==='ir_scan'){
      dashTitle.innerHTML='IR SIGNALS CAPTURED';
      if(irDevices.length===0){dashBox.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">AWAITING IR TRANSMISSION...</div>';}
      else{var html='<table class="scan-table"><thead><tr><th>PROTOCOL</th><th>ADDRESS</th><th>COMMAND</th><th>LENGTH</th></tr></thead><tbody>';
        irDevices.forEach(function(dev){html+='<tr><td style="font-weight:600;color:#fff;">'+dev.proto+'</td><td style="color:var(--primary-color);font-weight:bold;">'+dev.addr+'</td><td style="color:#34d399;">'+dev.cmd+'</td><td style="color:var(--text-muted);">'+dev.len+' bits</td></tr>';});
        html+='</tbody></table>';dashBox.innerHTML=html;}}
    else if(selectedAttack==='ghz_scan'){
      dashTitle.innerHTML='NRF24L01 SPECTRUM ANALYZER';
      if(nrfChannels.length===0){dashBox.innerHTML='<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">AWAITING NRF24 SPECTRUM DATA...</div>';}
      else{var html='<div style="max-height:200px;overflow-y:auto;font-size:10px;line-height:1.2;text-align:left;padding:4px;border:1px solid var(--border-color);background:#020203;">';
        nrfChannels.forEach(function(c){if(c) html+='Ch '+String(c.ch).padEnd(3)+' | '+c.dbm.padEnd(8)+' | <span style="color:var(--primary-color);">'+c.graph+'</span><br>';});
        html+='</div>';dashBox.innerHTML=html;}}
  }

  function sendCmd(cmd){
    if(cmd==='start'){wifiNetworks=[];bleDevices=[];evilTwinClients=[];updateDashboardUI();}
    var status=document.getElementById('status');
    status.innerHTML='PENDING: SENDING COMMAND TO SLAVE...';
    var attackParam=selectedAttack;
    if(cmd==='start'&&isExtremeMode) attackParam=selectedAttack+'_extreme';
    else if(cmd==='start'&&isBoostMode) attackParam=selectedAttack+'_boost';
    if(cmd==='stop'){
      fetch('/stop_all').then(function(r){return r.text();}).then(function(data){status.innerHTML='STATUS: '+data.toUpperCase();}).catch(function(){status.innerHTML='ERROR: MASTER UNREACHABLE';});
      return;
    }
    fetch('/cmd?q='+cmd+'&attack='+attackParam)
      .then(function(r){return r.text();})
      .then(function(data){status.innerHTML='STATUS: '+data.toUpperCase();})
      .catch(function(){status.innerHTML='ERROR: MASTER ESP32 UNREACHABLE';});
  }

  function checkConnection(){
    var dot=document.getElementById('status-dot');
    var text=document.getElementById('status-text');
    dot.className='status-dot testing';
    text.innerHTML='Slave ESP32-S3: PINGING...';
    fetch('/cmd?q=ping&attack=')
      .then(function(){
        setTimeout(function(){
          fetch('/status').then(function(r){return r.text();}).then(function(data){
            var parts=data.split('|');
            var st=parts[0].trim();
            var mac=parts[1]?parts[1].trim():'';
            if(st==='online'){dot.className='status-dot online';text.innerHTML='Slave ESP32-S3: ONLINE'+(mac?' ('+mac+')':'');}
            else{dot.className='status-dot offline';text.innerHTML='Slave ESP32-S3: OFFLINE';}
          }).catch(function(){dot.className='status-dot offline';text.innerHTML='Slave ESP32-S3: OFFLINE';});
        },1200);
      })
      .catch(function(){dot.className='status-dot offline';text.innerHTML='Slave ESP32-S3: OFFLINE';});
  }

  function updateConsole(){
    if(isClearing) return;
    fetch('/logs').then(function(r){return r.text();}).then(function(data){
      if(isClearing) return;
      parseLogs(data);
      var filteredLogs=data.split('\n').filter(function(line){
        return line.indexOf('[WIFI_DEV]')<0&&line.indexOf('[BLE_DEV]')<0&&line.indexOf('[EVIL_CLIENT]')<0&&line.indexOf('[SUBGHZ_DEV]')<0&&line.indexOf('[IR_DEV]')<0&&line.indexOf('[NRF_DEV]')<0;
      }).join('\n');
      var consoleBox=document.getElementById('console-box');
      if(filteredLogs.trim()===''){consoleBox.innerHTML='Logs are empty. Standby...';}
      else{consoleBox.innerHTML=filteredLogs;consoleBox.scrollTop=consoleBox.scrollHeight;}
    }).catch(function(err){console.error("Console update error:",err);});
  }

  function clearLogs(){
    isClearing=true;
    document.getElementById('console-box').innerHTML='Clearing server logs...';
    wifiNetworks=[];bleDevices=[];evilTwinClients=[];subghzDevices=[];irDevices=[];
    updateDashboardUI();
    fetch('/clear_logs').then(function(r){return r.text();}).then(function(){
      document.getElementById('console-box').innerHTML='Console cleared.';
      setTimeout(function(){isClearing=false;},600);
    }).catch(function(err){
      document.getElementById('console-box').innerHTML='Error clearing logs.';
      console.error(err);isClearing=false;
    });
  }

  setTimeout(checkConnection,1000);
  setInterval(updateConsole,1500);
</script>
</body>
</html>
)rawliteral";
