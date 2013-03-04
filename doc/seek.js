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

function smode(mode) {
  if (mode == 1) {
    hide('stepper_setup');
    show('stepper_active');
    numsteps=parseInt(document.getElementById('numsteps').value, 10);
    if (numsteps > 100 || numsteps < 1) numsteps = 15;
  } else {
    show('stepper_setup');
    hide('stepper_active');
    document.getElementById('numsteps').value = numsteps;
    numsteps=0;
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
  var val = Math.ceil(500 * frame / lastframe);
  document.getElementById('knob').style.width=val+'px';
}

function settc(frame) {
  var s = Math.floor(frame/fps);
  var hour = Math.floor(s/3600);
  var min = Math.floor(s/60)%60;
  var sec = Math.floor(s%60);
  var frames = Math.floor(frame%fps);
  updateText('frames', PadDigits(frames, 2));
  updateText('sec', PadDigits(sec, 2));
  updateText('min', PadDigits(min, 2));
  updateText('hour', PadDigits(hour, 2));
}

function seek(i, f) {
  if (curframe == f) return;
  curframe = f;
  document.getElementById('sframe').src=base_url+'?file='+i+'&frame='+f+'&w=-1&h=300&format=jpeg';
}

function movestep(e) {
  if (numsteps < 1) return;
  var mouse_x = e.clientX;
  var xoff = document.getElementById('slider').offsetLeft;
  var frame = Math.floor((Math.floor((mouse_x - xoff) / (501/numsteps))+.5) * lastframe / numsteps);
  if (frame == curframe) return;
  setslider(frame);
  settc(frame);
  seek(fileid, frame);
}

function moveknob(e) {
  if (numsteps > 0) return;
  var mouse_x = e.clientX;
  var xoff = document.getElementById('slider').offsetLeft;
  var frame = Math.floor((mouse_x-xoff) * lastframe / 501);
  if (frame == curframe) return;
  setslider(frame);
  settc(frame);
  seek(fileid, frame);
}
