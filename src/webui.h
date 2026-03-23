#pragma once
#include <pgmspace.h>

static const char WEBUI_HTML[] PROGMEM = R"HTMLEOF(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Light Painting</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d0d0d;color:#ddd;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
  padding:14px;max-width:480px;margin:auto}
h1{text-align:center;color:#ccc;margin-bottom:16px;font-size:1.15em;letter-spacing:3px;
  font-weight:300;text-transform:uppercase}

.status-bar{display:flex;gap:8px;margin-bottom:14px}
.badge{flex:1;text-align:center;border-radius:8px;padding:9px 4px;font-size:.78em;
  font-weight:600;letter-spacing:1.5px;text-transform:uppercase}
.on{background:#0f2e0f;color:#5d5;border:1px solid #1a4a1a}
.off{background:#1a1a1a;color:#555;border:1px solid #2a2a2a}

.card{background:#181818;border-radius:12px;padding:16px;margin-bottom:10px;
  border:1px solid #242424}
.card h2{font-size:.68em;text-transform:uppercase;color:#555;margin-bottom:14px;
  letter-spacing:1.5px;font-weight:500}

.row{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;
  gap:10px;font-size:.88em}
.row:last-child{margin-bottom:0}
.row span.lbl{white-space:nowrap;color:#999}
.row span.val{min-width:42px;text-align:right;color:#bbb;font-size:.82em}
input[type=range]{flex:1;accent-color:#aaa;min-width:0;opacity:.8}

.btn-group{display:flex;gap:5px;flex:1;justify-content:flex-end;flex-wrap:wrap}
.btn-choice{background:#212121;color:#666;border:1px solid #2e2e2e;border-radius:7px;
  padding:6px 11px;font-size:.82em;cursor:pointer;transition:all .12s;
  min-width:34px;text-align:center;user-select:none}
.btn-choice:hover{background:#2a2a2a;color:#999;border-color:#3a3a3a}
.btn-choice.active{background:#3a3a3a;color:#e8e8e8;border-color:#555;font-weight:600}

.mode-tabs{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:14px}
.mode-tab{background:#212121;color:#555;border:1px solid #2e2e2e;border-radius:8px;
  padding:10px 4px;font-size:.78em;cursor:pointer;text-align:center;
  transition:all .12s;letter-spacing:.5px}
.mode-tab:hover{background:#2a2a2a;color:#888}
.mode-tab.active{background:#3a3a3a;color:#e8e8e8;border-color:#555;font-weight:600}

.color-row{display:flex;gap:20px;margin-bottom:2px;align-items:center}
.color-box{display:flex;flex-direction:column;align-items:center;gap:6px;
  font-size:.72em;color:#666;letter-spacing:.5px}
input[type=color]{width:52px;height:38px;border:1px solid #333;border-radius:8px;
  cursor:pointer;background:#212121;padding:2px}

select{background:#212121;color:#ccc;border:1px solid #333;border-radius:7px;
  padding:6px 10px;font-size:.88em;flex:1;outline:none}
select:focus{border-color:#555}

.divider{height:1px;background:#222;margin:10px 0}
.sec{display:none}.sec.vis{display:block}

.preset-row{margin-bottom:8px}
.preset-row:last-child{margin-bottom:0}
.preset-name{background:#212121;color:#ccc;border:1px solid #333;border-radius:7px;
  padding:6px 10px;font-size:.85em;flex:1;outline:none;min-width:0}
.preset-name:focus{border-color:#555}

.mode-banner{display:flex;align-items:center;justify-content:space-between;
  background:#111;border:1px solid #2a2a2a;border-radius:10px;padding:10px 14px;
  margin-bottom:14px}
.mode-banner span{font-size:.72em;text-transform:uppercase;letter-spacing:2px;color:#666}
.mode-banner strong{font-size:.82em;letter-spacing:1.5px}
.mode-simple-view{display:none}
.mode-expert-view{display:none}
.show-simple .mode-simple-view{display:block}
.show-expert .mode-expert-view{display:block}
.btn-mode{background:#212121;color:#888;border:1px solid #333;border-radius:7px;
  padding:6px 13px;font-size:.78em;cursor:pointer;white-space:nowrap}
.btn-mode:hover{background:#2a2a2a;color:#bbb}

.bat-bar{display:flex;gap:5px;margin-bottom:6px}
.bat-seg{flex:1;height:16px;border-radius:4px;background:#1e1e1e;border:1px solid #2a2a2a;
  transition:background .3s}
.bat-seg.s0.on{background:#c80000}
.bat-seg.s1.on{background:#dc5000}
.bat-seg.s2.on{background:#c8c800}
.bat-seg.s3.on{background:#50dc00}
.bat-seg.s4.on{background:#00c800}
.bat-voltage{font-size:.75em;color:#666;text-align:right;letter-spacing:.5px}

/* Mini indicateur batterie dans le header */
.bat-mini{display:flex;align-items:center;gap:5px;font-size:.7em;color:#555;
  letter-spacing:.5px;margin-bottom:14px;justify-content:flex-end}
.bat-mini-segs{display:flex;gap:2px;align-items:center}
.bat-mini-seg{width:7px;height:11px;border-radius:2px;background:#1e1e1e;
  border:1px solid #2a2a2a;transition:background .3s}
.bat-mini-seg.s0.on{background:#c80000}
.bat-mini-seg.s1.on{background:#dc5000}
.bat-mini-seg.s2.on{background:#c8c800}
.bat-mini-seg.s3.on{background:#50dc00}
.bat-mini-seg.s4.on{background:#00c800}
.bat-mini-cap{width:3px;height:6px;background:#2a2a2a;border-radius:0 1px 1px 0}
.bat-mini-txt{color:#444}
.bat-mini-charge{color:#5af;animation:blink-charge .9s step-end infinite}
@keyframes blink-charge{0%,100%{opacity:1}50%{opacity:.25}}
</style>
</head>
<body class="show-simple">
<h1>&#9733; Light Painting</h1>

<!-- ======== BANDEAU MODE ======== -->
<div class="mode-banner">
  <span>Mode <strong id="mode-label">Simple</strong></span>
  <div class="btn-mode" id="btn-mode-toggle" onclick="toggleMode()">Mode Expert →</div>
</div>

<div class="status-bar">
  <div id="st-lum" class="badge off">Lumière off</div>
  <div id="st-ptr" class="badge off">Pointeur off</div>
</div>

<!-- Mini indicateur batterie (commun aux deux vues) -->
<div class="bat-mini" id="bat-mini">
  <span class="bat-mini-segs">
    <div class="bat-mini-seg s0" id="bm0"></div>
    <div class="bat-mini-seg s1" id="bm1"></div>
    <div class="bat-mini-seg s2" id="bm2"></div>
    <div class="bat-mini-seg s3" id="bm3"></div>
    <div class="bat-mini-seg s4" id="bm4"></div>
    <div class="bat-mini-cap"></div>
  </span>
  <span class="bat-mini-txt" id="bat-mini-txt">—V —%</span>
  <span class="bat-mini-charge" id="bat-mini-charge" style="display:none">⚡</span>
</div>

<!-- ======== VUE SIMPLE ======== -->
<div class="mode-simple-view">

  <div class="card">
    <h2>Couleur principale</h2>
    <div class="color-row">
      <div class="color-box">
        <input type="color" id="c1-s" value="#ff0000" oninput="onC1Simple()">
        <span>Couleur</span>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>Réglages</h2>
    <div class="row">
      <span class="lbl">Nb LEDs</span>
      <div class="btn-group" id="grp-leds-s">
        <div class="btn-choice" onclick="setNbLeds(0)">10</div>
        <div class="btn-choice" onclick="setNbLeds(1)">30</div>
        <div class="btn-choice active" onclick="setNbLeds(2)">39</div>
        <div class="btn-choice" onclick="setNbLeds(3)">40</div>
        <div class="btn-choice" onclick="setNbLeds(4)">50</div>
      </div>
    </div>
    <div class="row">
      <span class="lbl">Luminosité</span>
      <div class="btn-group" id="grp-lum-s">
        <div class="btn-choice" onclick="setLum(0)" title="1%">1%</div>
        <div class="btn-choice" onclick="setLum(1)" title="5%">5%</div>
        <div class="btn-choice" onclick="setLum(2)" title="10%">10%</div>
        <div class="btn-choice" onclick="setLum(3)" title="25%">25%</div>
        <div class="btn-choice active" onclick="setLum(4)" title="50%">50%</div>
        <div class="btn-choice" onclick="setLum(5)" title="80%">80%</div>
        <div class="btn-choice" onclick="setLum(6)" title="100%">100%</div>
      </div>
    </div>
    <div class="row">
      <span class="lbl">Blink C1</span>
      <div class="btn-group" id="grp-blink1-s">
        <div class="btn-choice active" onclick="setBlink1Simple(0)">Fix</div>
        <div class="btn-choice"        onclick="setBlink1Simple(1)">5 Hz</div>
        <div class="btn-choice"        onclick="setBlink1Simple(2)">25 Hz</div>
        <div class="btn-choice"        onclick="setBlink1Simple(3)">50 Hz</div>
        <div class="btn-choice"        onclick="setBlink1Simple(4)">75 Hz</div>
      </div>
    </div>
  </div>

</div><!-- fin mode-simple-view -->

<!-- ======== VUE EXPERT ======== -->
<div class="mode-expert-view">

<!-- ======== COMMUN ======== -->
<div class="card">
  <h2>Réglages communs</h2>
  <div class="row">
    <span class="lbl">Nb LEDs</span>
    <div class="btn-group" id="grp-leds">
      <div class="btn-choice" onclick="setNbLeds(0)">10</div>
      <div class="btn-choice" onclick="setNbLeds(1)">30</div>
      <div class="btn-choice active" onclick="setNbLeds(2)">39</div>
      <div class="btn-choice" onclick="setNbLeds(3)">40</div>
      <div class="btn-choice" onclick="setNbLeds(4)">50</div>
    </div>
  </div>
  <div class="row">
    <span class="lbl">Luminosité</span>
    <div class="btn-group" id="grp-lum">
      <div class="btn-choice" onclick="setLum(0)" title="1%">1%</div>
      <div class="btn-choice" onclick="setLum(1)" title="5%">5%</div>
      <div class="btn-choice" onclick="setLum(2)" title="10%">10%</div>
      <div class="btn-choice" onclick="setLum(3)" title="25%">25%</div>
      <div class="btn-choice active" onclick="setLum(4)" title="50%">50%</div>
      <div class="btn-choice" onclick="setLum(5)" title="80%">80%</div>
      <div class="btn-choice" onclick="setLum(6)" title="100%">100%</div>
    </div>
  </div>
  <div class="row">
    <span class="lbl" style="min-width:68px">Btn1 fréq.</span>
    <div class="btn-group" id="grp-blink1">
      <div class="btn-choice active" onclick="setBlink1(0)">Fix</div>
      <div class="btn-choice"        onclick="setBlink1(1)">5 Hz</div>
      <div class="btn-choice"        onclick="setBlink1(2)">25 Hz</div>
      <div class="btn-choice"        onclick="setBlink1(3)">50 Hz</div>
      <div class="btn-choice"        onclick="setBlink1(4)">75 Hz</div>
    </div>
  </div>
  <div class="row">
    <span class="lbl" style="min-width:68px">Btn2 fréq.</span>
    <div class="btn-group" id="grp-blink2">
      <div class="btn-choice active" onclick="setBlink2(0)">Fix</div>
      <div class="btn-choice"        onclick="setBlink2(1)">5 Hz</div>
      <div class="btn-choice"        onclick="setBlink2(2)">25 Hz</div>
      <div class="btn-choice"        onclick="setBlink2(3)">50 Hz</div>
      <div class="btn-choice"        onclick="setBlink2(4)">75 Hz</div>
    </div>
  </div>
</div>

<!-- ======== COULEURS ======== -->
<div class="card">
  <h2>Couleurs</h2>
  <div class="color-row">
    <div class="color-box">
      <input type="color" id="c1" value="#ff0000" oninput="onC1()">
      <span>C1 — base</span>
    </div>
    <div class="color-box">
      <input type="color" id="c2" value="#0000ff" oninput="onC2()">
      <span>C2 — point</span>
    </div>
  </div>
</div>

<!-- ======== MODE ======== -->
<div class="card">
  <h2>Mode</h2>
  <div class="mode-tabs">
    <div class="mode-tab active" id="tab-0" onclick="setAnim(0)">Statique</div>
    <div class="mode-tab"        id="tab-1" onclick="setAnim(1)">Rainbow</div>
    <div class="mode-tab"        id="tab-2" onclick="setAnim(2)">Pattern</div>
  </div>

  <!-- STATIQUE -->
  <div class="sec vis" id="sec-0">
    <div class="row">
      <span class="lbl">Densité</span>
      <select id="densite" onchange="onDensite(this.value)">
        <option value="1">Toutes les LEDs</option>
        <option value="2">1 sur 2</option>
        <option value="3">1 sur 3</option>
      </select>
    </div>
  </div>

  <!-- ARC-EN-CIEL -->
  <div class="sec" id="sec-1">
    <div class="row">
      <span class="lbl">Vitesse</span>
      <div class="btn-group" id="grp-rain">
        <div class="btn-choice"        onclick="setRain(0)">1 ms</div>
        <div class="btn-choice"        onclick="setRain(1)">3 ms</div>
        <div class="btn-choice"        onclick="setRain(2)">5 ms</div>
        <div class="btn-choice"        onclick="setRain(3)">10 ms</div>
        <div class="btn-choice active" onclick="setRain(4)">20 ms</div>
      </div>
    </div>
  </div>

  <!-- PATTERN -->
  <div class="sec" id="sec-2">
    <div class="row">
      <span class="lbl">Slot 1 <span style="font-size:.75em;color:#444">(appui)</span></span>
      <select id="patternSlot1" onchange="send({patternSlot1:+this.value})"></select>
    </div>
    <div class="row">
      <span class="lbl">Slot 2 <span style="font-size:.75em;color:#444">(appui suivant)</span></span>
      <select id="patternSlot2" onchange="send({patternSlot2:+this.value})"></select>
    </div>
    <div class="row">
      <span class="lbl">Actif</span>
      <div class="btn-group" id="grp-patslot">
        <div class="btn-choice active" onclick="setPatSlot(0)">Slot 1</div>
        <div class="btn-choice"        onclick="setPatSlot(1)">Slot 2</div>
      </div>
    </div>
    <div class="divider"></div>
    <div class="row">
      <span class="lbl">Vitesse défilé</span>
      <span class="val" id="lbl-pvit">60 ms</span>
      <input type="range" id="patternVitesse" min="10" max="500" value="60" oninput="onPVit()">
    </div>
    <div class="row">
      <span class="lbl">Mode</span>
      <select id="patternDefilant" onchange="send({patternDefilant:this.value==='true'})">
        <option value="true">Défilant</option>
        <option value="false">Statique</option>
      </select>
    </div>
  </div>
</div>

<!-- ======== POINT / REPERE ======== -->
<div class="card">
  <h2>Point / Repère <small style="color:#3a3a3a;font-size:.85em">— GPIO27</small></h2>
  <div class="row">
    <span class="lbl">Position</span>
    <div class="btn-group" id="grp-pos">
      <div class="btn-choice active" onclick="setPos(0)">Centre</div>
      <div class="btn-choice"        onclick="setPos(1)">◀ Gche</div>
      <div class="btn-choice"        onclick="setPos(2)">Dte ▶</div>
      <div class="btn-choice"        onclick="setPos(3)">◀ ▶</div>
    </div>
  </div>
  <div class="row">
    <span class="lbl">Taille</span>
    <div class="btn-group" id="grp-point">
      <div class="btn-choice active" onclick="setPoint(0)">1</div>
      <div class="btn-choice"        onclick="setPoint(1)">10</div>
      <div class="btn-choice"        onclick="setPoint(2)">30</div>
      <div class="btn-choice"        onclick="setPoint(3)">50</div>
    </div>
  </div>
  <div class="led-visu" id="led-visu"></div>
</div>

<!-- ======== PRESETS ======== -->
<div class="card">
  <h2>Presets</h2>

  <div class="preset-row" id="preset-row-0">
    <div class="row">
      <input class="preset-name" id="pname-0" type="text" value="General 1"
        onchange="renamePreset(0)" placeholder="Nom du preset">
      <div class="btn-group" style="flex:none">
        <div class="btn-choice" onclick="savePreset(0)" title="Sauvegarder état actuel">&#9650; Sauver</div>
        <div class="btn-choice btn-load" onclick="loadPreset(0)" title="Charger ce preset">&#9660; Charger</div>
      </div>
    </div>
  </div>

  <div class="preset-row" id="preset-row-1">
    <div class="row">
      <input class="preset-name" id="pname-1" type="text" value="General 2"
        onchange="renamePreset(1)" placeholder="Nom du preset">
      <div class="btn-group" style="flex:none">
        <div class="btn-choice" onclick="savePreset(1)">&#9650; Sauver</div>
        <div class="btn-choice btn-load" onclick="loadPreset(1)">&#9660; Charger</div>
      </div>
    </div>
  </div>

  <div class="preset-row" id="preset-row-2">
    <div class="row" style="margin-bottom:0">
      <input class="preset-name" id="pname-2" type="text" value="Test"
        onchange="renamePreset(2)" placeholder="Nom du preset">
      <div class="btn-group" style="flex:none">
        <div class="btn-choice" onclick="savePreset(2)">&#9650; Sauver</div>
        <div class="btn-choice btn-load" onclick="loadPreset(2)">&#9660; Charger</div>
      </div>
    </div>
  </div>
</div>

</div><!-- fin mode-expert-view -->

<script>
var animIdx      = 0;
var nbLedsIdx    = 2;
var pointIdx     = 0;
var posPoint     = 0;
var pointColor   = '#0000ff';
var modeExpertJS = false;  // etat reel du mode, synchronise depuis l'ESP
var NB_LEDS_CHOICES = [10, 30, 39, 40, 50];
var TAILLE_POINT    = [1, 10, 30, 50];
// Valeurs en attente pour patternSlot1/2 (arrivées avant fillPatterns)
var pendingSlot1 = null;
var pendingSlot2 = null;
var patternsFilled = false;  // les options ne sont créées qu'une seule fois

// Init visu 50 cellules
(function(){
  var v=document.getElementById('led-visu');
  for(var i=0;i<50;i++){
    var d=document.createElement('div');
    d.className='lv-cell'; d.id='lvc-'+i;
    v.appendChild(d);
  }
})();

function toHex(r,g,b){
  return '#'+[r,g,b].map(function(x){return x.toString(16).padStart(2,'0');}).join('');
}
function hexRgb(h){
  return{r:parseInt(h.slice(1,3),16),g:parseInt(h.slice(3,5),16),b:parseInt(h.slice(5,7),16)};
}
function send(obj){
  fetch('/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)});
}
function setActiveBtn(groupId,idx){
  var g=document.getElementById(groupId);
  if(!g) return;
  g.querySelectorAll('.btn-choice').forEach(function(b,i){
    b.className='btn-choice'+(i===idx?' active':'');
  });
}

// Mode Simple / Expert
function applyModeUI(expert){
  modeExpertJS = expert;
  document.body.className = expert ? 'show-expert' : 'show-simple';
  document.getElementById('mode-label').textContent = expert ? 'Expert' : 'Simple';
  document.getElementById('btn-mode-toggle').textContent = expert ? '← Mode Simple' : 'Mode Expert →';
}
function toggleMode(){
  var next = !modeExpertJS;
  fetch('/setmode',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({modeExpert:next})});
  applyModeUI(next);
}

// Batterie : mini barre discrète en haut
function applyBattery(pct, v, charging){
  var segs = (pct===0) ? 0 : Math.ceil(pct/20);   // 0-5
  for(var i=0;i<5;i++){
    var el=document.getElementById('bm'+i);
    if(el){ el.classList.toggle('on', i<segs); }
  }
  var txt=document.getElementById('bat-mini-txt');
  if(txt) txt.textContent = v.toFixed(1)+'V '+pct+'%';
  var chEl=document.getElementById('bat-mini-charge');
  if(chEl){ chEl.style.display = charging ? 'inline' : 'none'; }
}

// Communs
function setNbLeds(idx){
  nbLedsIdx=idx;
  setActiveBtn('grp-leds',idx);
  setActiveBtn('grp-leds-s',idx);
  send({idxNbLeds:idx}); updateVisuPoint();
}
function setLum(idx){
  setActiveBtn('grp-lum',idx);
  setActiveBtn('grp-lum-s',idx);
  send({niveauLuminosite:idx});
}

// Couleurs
function onC1Simple(){
  var x=hexRgb(document.getElementById('c1-s').value);
  document.getElementById('c1').value=document.getElementById('c1-s').value;
  send({r1:x.r,g1:x.g,b1:x.b});
}
function onC1(){
  document.getElementById('c1-s').value=document.getElementById('c1').value;
  var x=hexRgb(document.getElementById('c1').value);send({r1:x.r,g1:x.g,b1:x.b});
}
function onC2(){
  pointColor=document.getElementById('c2').value;
  var x=hexRgb(pointColor);
  send({r2:x.r,g2:x.g,b2:x.b}); updateVisuPoint();
}

// Densité synchronisée
function onDensite(val){
  document.getElementById('densite').value=val;
  send({densite:+val});
}

// Modes
function setAnim(idx){
  animIdx=idx;
  for(var i=0;i<3;i++){
    document.getElementById('tab-'+i).className='mode-tab'+(i===idx?' active':'');
    document.getElementById('sec-'+i).className='sec'+(i===idx?' vis':'');
  }
  send({animation:idx});
}

function setBlink1(idx){
  setActiveBtn('grp-blink1',idx);
  send({idxFreqBlink:idx});
}
function setBlink1Simple(idx){
  setActiveBtn('grp-blink1-s',idx);
  send({idxFreqBlinkSimple:idx});
}
function setBlink2(idx){
  setActiveBtn('grp-blink2',idx);
  send({idxFreqBlinkC2:idx});
}
function setRain(idx) { setActiveBtn('grp-rain',idx);  send({idxVitesseRainbow:idx}); }

// Pattern
function onPVit(){
  var v=+document.getElementById('patternVitesse').value;
  document.getElementById('lbl-pvit').textContent=v+' ms';
  send({patternVitesse:v});
}
function setPatSlot(idx){ setActiveBtn('grp-patslot',idx); send({patternActif:idx}); }

// Remplit les deux selects avec les noms (une seule fois), puis applique les valeurs en attente
function fillPatterns(noms){
  if(!patternsFilled){
    patternsFilled=true;
    ['patternSlot1','patternSlot2'].forEach(function(id){
      var sel=document.getElementById(id);
      sel.innerHTML='';
      noms.forEach(function(n,i){
        var o=document.createElement('option');
        o.value=i; o.textContent=n; sel.appendChild(o);
      });
    });
  }
  // Appliquer les valeurs en attente maintenant que les options existent
  if(pendingSlot1!==null){ document.getElementById('patternSlot1').value=pendingSlot1; pendingSlot1=null; }
  if(pendingSlot2!==null){ document.getElementById('patternSlot2').value=pendingSlot2; pendingSlot2=null; }
}

// Point
function setPos(idx){
  posPoint=idx; setActiveBtn('grp-pos',idx);
  send({posPoint:idx}); updateVisuPoint();
}
function setPoint(idx){
  pointIdx=idx; setActiveBtn('grp-point',idx);
  send({idxTaillePoint:idx}); updateVisuPoint();
}
function updateVisuPoint(){
  var nbLeds  = NB_LEDS_CHOICES[nbLedsIdx];
  var tp      = TAILLE_POINT[pointIdx];
  if(tp>nbLeds) tp=nbLeds;
  var barStart= 0;
  var barEnd  = nbLeds;
  var zones=[];
  if(posPoint===0){
    var c=Math.floor(barStart+nbLeds/2);
    var s=Math.max(0,c-Math.floor(tp/2));
    zones.push([s,Math.min(50,s+tp)]);
  }else if(posPoint===1){
    zones.push([barStart,Math.min(50,barStart+tp)]);
  }else if(posPoint===2){
    zones.push([Math.max(0,barEnd-tp),barEnd]);
  }else{
    zones.push([barStart,Math.min(50,barStart+tp)]);
    zones.push([Math.max(0,barEnd-tp),barEnd]);
  }
  for(var i=0;i<50;i++){
    var cell=document.getElementById('lvc-'+i);
    if(!cell) continue;
    var inZ=zones.some(function(z){return i>=z[0]&&i<z[1];});
    if(inZ)                         cell.style.background=pointColor;
    else if(i>=barStart&&i<barEnd)  cell.style.background='#282828';
    else                            cell.style.background='#141414';
  }
}

// Sync état complet depuis ESP
function applyState(s){
  document.getElementById('c1').value=toHex(s.r1,s.g1,s.b1);
  document.getElementById('c1-s').value=toHex(s.r1,s.g1,s.b1);
  document.getElementById('c2').value=toHex(s.r2,s.g2,s.b2);
  pointColor=toHex(s.r2,s.g2,s.b2);

  if(s.idxNbLeds!==undefined){ nbLedsIdx=s.idxNbLeds; setActiveBtn('grp-leds',nbLedsIdx); setActiveBtn('grp-leds-s',nbLedsIdx); }
  if(s.niveauLuminosite!==undefined){ setActiveBtn('grp-lum',s.niveauLuminosite); setActiveBtn('grp-lum-s',s.niveauLuminosite); }
  if(s.densite!==undefined){
    document.getElementById('densite').value=s.densite;
  }
  if(s.idxFreqBlink!==undefined){ setActiveBtn('grp-blink1',s.idxFreqBlink); }
  if(s.idxFreqBlinkSimple!==undefined){ setActiveBtn('grp-blink1-s',s.idxFreqBlinkSimple); }
  if(s.idxFreqBlinkC2!==undefined) setActiveBtn('grp-blink2',s.idxFreqBlinkC2);
  if(s.idxVitesseRainbow!==undefined) setActiveBtn('grp-rain',s.idxVitesseRainbow);

  // Pattern : stocker les valeurs en attente, puis remplir les selects
  if(s.patternSlot1!==undefined) pendingSlot1=s.patternSlot1;
  if(s.patternSlot2!==undefined) pendingSlot2=s.patternSlot2;
  if(s.patternsNoms && s.patternsNoms.length) fillPatterns(s.patternsNoms);

  if(s.patternActif!==undefined) setActiveBtn('grp-patslot',s.patternActif);
  if(s.patternVitesse!==undefined){
    document.getElementById('patternVitesse').value=s.patternVitesse;
    document.getElementById('lbl-pvit').textContent=s.patternVitesse+' ms';
  }
  if(s.patternDefilant!==undefined)
    document.getElementById('patternDefilant').value=s.patternDefilant?'true':'false';

  if(s.idxTaillePoint!==undefined){ pointIdx=s.idxTaillePoint; setActiveBtn('grp-point',pointIdx); }
  if(s.posPoint!==undefined){ posPoint=s.posPoint; setActiveBtn('grp-pos',posPoint); }
  if(s.animation!==undefined && s.animation!==animIdx) setAnim(s.animation);

  if(s.batteryPercent!==undefined && s.batteryVoltage!==undefined)
    applyBattery(s.batteryPercent, s.batteryVoltage, !!s.charging);

  var lum=document.getElementById('st-lum');
  lum.textContent=s.lumiere?'Lumière on':'Lumière off';
  lum.className='badge '+(s.lumiere?'on':'off');
  var ptr=document.getElementById('st-ptr');
  ptr.textContent=s.pointeur?'Pointeur on':'Pointeur off';
  ptr.className='badge '+(s.pointeur?'on':'off');

  updateVisuPoint();

  if(s.modeExpert!==undefined) applyModeUI(s.modeExpert);
}

function poll(){
  fetch('/state').then(function(r){return r.json();}).then(applyState).catch(function(){});
}
setInterval(poll,500);
poll();

// ---- Presets ----
var presetsFilled = false;

function loadPresets(){
  fetch('/presets').then(function(r){return r.json();}).then(function(d){
    d.presets.forEach(function(p){
      var row=document.getElementById('preset-row-'+p.slot);
      if(!row) return;
      var inp=row.querySelector('.preset-name');
      if(!presetsFilled && inp) inp.value=p.nom;
      var btn=row.querySelector('.btn-load');
      if(btn) btn.disabled=!p.ok;
      if(btn) btn.style.opacity=p.ok?'1':'0.35';
    });
    presetsFilled=true;
  }).catch(function(){});
}

function savePreset(slot){
  var inp=document.getElementById('pname-'+slot);
  var nom=inp?inp.value.trim():'';
  if(!nom) nom='Preset '+(slot+1);
  fetch('/preset/save',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({slot:slot,nom:nom})})
  .then(function(){ presetsFilled=false; loadPresets(); });
}

function loadPreset(slot){
  fetch('/preset/load',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({slot:slot})})
  .then(function(r){return r.json();}).then(function(s){
    applyState(s);
  });
}

function renamePreset(slot){
  var inp=document.getElementById('pname-'+slot);
  if(!inp) return;
  fetch('/preset/rename',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({slot:slot,nom:inp.value.trim()})});
}

loadPresets();
setInterval(loadPresets, 5000);
</script>
</body>
</html>)HTMLEOF";
