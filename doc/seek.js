var curframe=-1;
var base_url="/";
var numsteps=0;

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

function smode(m) {
  if (m == 1) {
    hide('stepper_setup');
    show('stepper_active');
    document.getElementById('slider').onmousemove = movestep;
  } else {
    show('stepper_setup');
    hide('stepper_active');
    document.getElementById('slider').onmousemove = null;
  }
}

function PadDigits(n, totalDigits) {
  n = n.toString();
  var pd = '';
  if (totalDigits > n.length) {
    for (i=0; i < (totalDigits-n.length); i++) { pd += '0'; }
  }
  return pd + n.toString();
}

function setslider(frame) {
  var val = Math.ceil(500.0 * frame / lastframe);
  document.getElementById('knob').style.width=val+'px';
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
    frame = Math.floor(f%fpsi);
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

function movestep(e) {
  var mouse_x = e.clientX;
  var xoff = document.getElementById('slider').offsetLeft;
  var frame = Math.floor((1+mouse_x-xoff) * lastframe / 500);
  if (frame == curframe) return;
  setslider(frame);
  settc(frame);
  seek(fileid, frame);
}
