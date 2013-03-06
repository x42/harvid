var curframe=-1;
var mode=0;
var base_url="/";

function show(foo) {
  document.getElementById(foo).style.display = "block";
}

function hide(foo) {
  document.getElementById(foo).style.display = "none";
}

function updateText (elemid, val) {
  var tn = document.createTextNode(val);
  document.getElementById(elemid).replaceChild(tn, document.getElementById(elemid).firstChild);
}

function PadDigits(n, totalDigits) {
  n = n.toString();
  var pd = '';
  if (totalDigits > n.length) {
    for (i=0; i < (totalDigits-n.length); i++) { pd += '0'; }
  }
  return pd + n.toString();
}

function settc(f) {
  var fpsi = Math.ceil(fps);
  var hour,min,sec,frame, s;
  if (Math.floor(fps * 100) == 2997) {
    var D = Math.floor(f / 17982);
    var M = f % 17982;
    var n = f + 18 * D + 2 * Math.floor((M - 2) / 1798);
    s = Math.floor(n/30);
    frame = Math.floor(n%30);
  } else {
    s = Math.floor(f/fpsi);
    frame = f%fpsi;
  }
  hour  = Math.floor(s/3600);
  min   = Math.floor(s/60)%60;
  sec   = Math.floor(s%60);
  updateText('frame', PadDigits(frame, 2));
  updateText('sec',   PadDigits(sec, 2));
  updateText('min',   PadDigits(min, 2));
  updateText('hour',  PadDigits(hour, 2));
}

function seek(i, f) {
  if (curframe == f) return;
  curframe = f;
  document.getElementById('sframe').src=base_url+'?file='+i+'&frame='+f+'&w=-1&h=300&format=jpeg60';
}

function setslider(frame) {
  var val = 1 + Math.round(500.0 * frame / lastframe);
  if (val < 1 || val > 501) return;
  document.getElementById('knob').style.width = val+'px';
}

function movestep(e) {
  var mouse_x = e.clientX;
  var xoff = document.getElementById('slider').offsetLeft;
  var spos = mouse_x - xoff;
  if (mode == 1) {
    spos = Math.floor(spos/5) * 5;
  }
  if (spos < 0 ) spos = 0;
  if (spos > 500 ) spos = 500;
  var frame = Math.round(spos * lastframe / 500.0);
  if (frame == curframe) return;
  setslider(frame);
  settc(frame);
  seek(fileid, frame);
}

function smode(m) {
  if (m == 1) {
    hide('stepper_setup');
    show('stepper_active');
    mode = 1;
    document.getElementById('slider').onmousemove = movestep;
  } else {
    show('stepper_setup');
    hide('stepper_active');
    mode = 0;
    document.getElementById('slider').onmousemove = null;
  }
}
