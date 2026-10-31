#ifndef __ARVIEWPAGE_H__
#define __ARVIEWPAGE_H__

// The /ar page (ARView.cpp). The game arrives as an MJPEG stream read with fetch and drawn into a canvas,
// which is the flat view and the board's texture. With WebXR immersive-ar (Chrome on Android) the board
// is put on a table found by hit-test, with three.js from a CDN; taps on it become mouse events through
// /click. Without WebXR the flat view takes clicks. {GAME_W} and {GAME_H} are filled in by the server.
static const char AR_PAGE[] = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PvZ en AR</title>
<style>
:root{color-scheme:dark;--bg:#12100e;--panel:rgba(20,17,14,.8);--line:#4d453d;--text:#f3eee7;--muted:#b4aa9f;--accent:#93d46b;--ink:#142409}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%}
body{background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
#flat{display:flex;flex-direction:column;align-items:center;gap:12px;padding:12px 16px 20px}
#flat header{width:min(100%,960px);display:flex;justify-content:space-between;align-items:baseline;gap:12px}
#flat h1{font-size:18px;margin:0;font-weight:650}
#status{color:var(--muted);font-size:13px}
#screen{width:min(100%,calc((100vh - 210px)*4/3),960px);aspect-ratio:4/3;background:#000;border-radius:10px;overflow:hidden}
#game{width:100%;height:100%;display:block;touch-action:none;cursor:pointer}
#enterAr{font:inherit;font-weight:650;font-size:17px;padding:14px 30px;border-radius:12px;border:0;background:var(--accent);color:var(--ink);cursor:pointer}
#note{margin:0;max-width:640px;color:var(--muted);font-size:13px;text-align:center}
#overlay{display:none;position:fixed;inset:0;background:transparent;color:var(--text)}
#overlay.on{display:block}
.bar{position:absolute;left:0;right:0;display:flex;flex-wrap:wrap;gap:8px;justify-content:center;align-items:center;padding:10px 12px}
#top{top:0;padding-top:max(10px,env(safe-area-inset-top));justify-content:space-between;flex-wrap:nowrap}
#controls{bottom:0;padding-bottom:max(14px,env(safe-area-inset-bottom))}
#mode{background:var(--panel);border-radius:14px;padding:8px 14px;font-size:14px}
#overlay button{font:inherit;min-width:48px;min-height:48px;padding:0 14px;border-radius:12px;border:1px solid var(--line);background:var(--panel);color:var(--text);touch-action:manipulation}
#overlay button.main{background:var(--accent);color:var(--ink);border-color:transparent;font-weight:650}
#overlay button:active{filter:brightness(1.35)}
#overlay button:disabled{opacity:.45}
.group{display:flex;gap:4px;align-items:center;background:var(--panel);border-radius:14px;padding:4px}
.group button{border:0!important;background:transparent!important}
.group span{min-width:74px;text-align:center;font-variant-numeric:tabular-nums;font-size:14px}
</style></head><body>
<div id="flat">
<header><h1>PvZ en AR</h1><span id="status">Conectando...</span></header>
<div id="screen"><canvas id="game" aria-label="El juego en vivo"></canvas></div>
<button id="enterAr" hidden>Entrar en AR</button>
<p id="note"></p>
</div>
<div id="overlay">
<div id="top" class="bar"><span id="mode"></span><button id="exit">Salir</button></div>
<div id="controls" class="bar">
<div class="group"><button data-act="tilt-" aria-label="Menos inclinado">&#9660;</button><span id="tiltv">0 grados</span><button data-act="tilt+" aria-label="Mas inclinado">&#9650;</button></div>
<div class="group"><button data-act="size-" aria-label="Mas chico">&minus;</button><span id="sizev">40 cm</span><button data-act="size+" aria-label="Mas grande">+</button></div>
<button data-act="rot">Girar 90</button>
<button data-act="place">Recolocar</button>
<button data-act="mode" id="modeBtn" class="main" disabled>Fijar</button>
</div>
</div>
<script>
'use strict';
const GW={GAME_W},GH={GAME_H};
const THREE_URL='https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js';
const auth='c='+encodeURIComponent(new URLSearchParams(location.search).get('c')||'');
const $=id=>document.getElementById(id);

// helpers: begin (no DOM, tested in node)
function uvToGame(u,v,w,h){
	return{x:Math.min(w-1,Math.max(0,Math.floor(u*w))),y:Math.min(h-1,Math.max(0,Math.floor((1-v)*h)))};
}
function findHeaderEnd(b){
	for(let i=0;i+3<b.length;i++)if(b[i]===13&&b[i+1]===10&&b[i+2]===13&&b[i+3]===10)return i;
	return -1;
}
// Splits multipart/x-mixed-replace bytes (each part with a Content-Length) into JPEGs.
class MjpegSplitter{
	constructor(){this.buf=new Uint8Array(0);}
	push(chunk){
		const b=new Uint8Array(this.buf.length+chunk.length);
		b.set(this.buf);b.set(chunk,this.buf.length);this.buf=b;
		const out=[];
		for(;;){
			const end=findHeaderEnd(this.buf);
			if(end<0)break;
			let head='';
			for(let i=0;i<end;i++)head+=String.fromCharCode(this.buf[i]);
			const m=/content-length:\s*(\d+)/i.exec(head),start=end+4;
			if(!m){this.buf=this.buf.slice(start);continue;}
			const len=Number(m[1]);
			if(this.buf.length<start+len)break;
			out.push(this.buf.slice(start,start+len));
			this.buf=this.buf.slice(start+len);
		}
		if(this.buf.length>(8<<20))this.buf=new Uint8Array(0);
		return out;
	}
}
// The ray (origin o, direction d) against the horizontal plane at height y, or null.
function rayOnPlaneY(o,d,y){
	if(d.y>-1e-3)return null;
	const t=(y-o.y)/d.y;
	if(t<0||t>20)return null;
	return{x:o.x+d.x*t,y:y,z:o.z+d.z*t};
}
// The board: a wooden slab with the lawn raised a little on it, tilting on its near edge (+z, toward the
// player). Sizes in meters for a 40 cm wide board; layout() scales, tilts and turns it.
function buildBoard(T,map,shadowMap,gw,gh){
	const W0=0.40,D0=W0*gh/gw,B=0.014,TH=0.016,LIFT=0.004,EDGE=D0/2+B;
	const root=new T.Group(),turn=new T.Group(),scaler=new T.Group(),hinge=new T.Group(),slab=new T.Group();
	root.add(turn);turn.add(scaler);scaler.add(hinge);hinge.add(slab);
	hinge.position.z=EDGE;slab.position.z=-EDGE;
	const base=new T.Mesh(new T.BoxGeometry(W0+2*B,TH,D0+2*B),new T.MeshStandardMaterial({color:0x6e4526,roughness:0.7}));
	base.position.y=TH/2;slab.add(base);
	const plinth=new T.Mesh(new T.BoxGeometry(W0,LIFT,D0),new T.MeshStandardMaterial({color:0x22170d,roughness:0.9}));
	plinth.position.y=TH+LIFT/2;slab.add(plinth);
	const screen=new T.Mesh(new T.PlaneGeometry(W0,D0),new T.MeshBasicMaterial({map:map,toneMapped:false}));
	screen.rotation.x=-Math.PI/2;screen.position.y=TH+LIFT+0.0005;slab.add(screen);
	const shadow=new T.Mesh(new T.PlaneGeometry(W0+0.11,D0+0.11),new T.MeshBasicMaterial({map:shadowMap,transparent:true,depthWrite:false}));
	shadow.rotation.x=-Math.PI/2;shadow.position.y=0.001;shadow.renderOrder=-1;scaler.add(shadow);
	function layout(st){
		const a=st.tilt*Math.PI/180,c=Math.cos(a);
		scaler.scale.setScalar(st.width/W0);
		hinge.rotation.x=a;
		turn.rotation.y=st.yaw+st.rot*Math.PI/2;
		shadow.scale.set(1,Math.max(0.35,c),1);
		shadow.position.z=EDGE*(1-c);
		root.updateMatrixWorld(true);
	}
	return{root:root,screen:screen,layout:layout,width:W0};
}
// helpers: end

const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const canvas=$('game');
canvas.width=GW;canvas.height=GH;
const ctx=canvas.getContext('2d');
ctx.fillStyle='#000';ctx.fillRect(0,0,GW,GH);

// The picture: a stream read with fetch, or single frames if that fails
const frameListeners=[];
let pending=null,decoding=false,lastFrameAt=0,lastError=0,polling=false,streamFrames=0;
function decode(blob){
	if(self.createImageBitmap)return createImageBitmap(blob);
	return new Promise((ok,bad)=>{const u=URL.createObjectURL(blob),i=new Image();i.onload=()=>{URL.revokeObjectURL(u);ok(i);};i.onerror=()=>{URL.revokeObjectURL(u);bad(new Error('jpeg'));};i.src=u;});
}
async function drawLatest(){
	decoding=true;
	while(pending){
		const bytes=pending;pending=null;
		try{
			const img=await decode(new Blob([bytes],{type:'image/jpeg'}));
			ctx.drawImage(img,0,0,GW,GH);
			if(img.close)img.close();
			lastFrameAt=performance.now();
			for(const f of frameListeners)f();
		}catch(e){}
	}
	decoding=false;
}
function onJpeg(bytes){pending=bytes;if(!decoding)drawLatest();}
async function readStream(){
	const res=await fetch('/stream?'+auth+'&t='+Date.now(),{cache:'no-store'});
	if(!res.ok||!res.body){lastError=res.status;throw new Error('stream '+res.status);}
	lastError=0;
	const reader=res.body.getReader(),split=new MjpegSplitter();
	for(;;){
		const r=await reader.read();
		if(r.done)return;
		for(const j of split.push(r.value)){streamFrames++;onJpeg(j);}
	}
}
async function feed(){
	let fails=0;
	for(;;){
		if(fails<3){
			const before=streamFrames;
			try{await readStream();}catch(e){}
			fails=streamFrames>before?0:fails+1;
			await sleep(800);
		}else{
			polling=true;
			try{
				const r=await fetch('/frame.jpg?'+auth+'&t='+Date.now(),{cache:'no-store'});
				lastError=r.ok||r.status===503?0:r.status;
				if(r.ok)onJpeg(new Uint8Array(await r.arrayBuffer()));
			}catch(e){}
			await sleep(70);
		}
	}
}
setInterval(()=>{
	const live=performance.now()-lastFrameAt<1500;
	$('status').textContent=lastError===403?'El codigo no vale: abre la direccion que sale en el juego':
		live?(polling?'En vivo (imagenes sueltas)':'En vivo'):'Sin imagen, reconectando...';
},500);
feed();

// Mouse events for the game, one request at a time so they arrive in order
const outbox=[];let sending=false;
function sendMouse(a,x,y){
	const last=outbox[outbox.length-1];
	if(a==='move'&&last&&last.a==='move')outbox[outbox.length-1]={a,x,y};
	else outbox.push({a,x,y});
	if(!sending)pump();
}
async function pump(){
	sending=true;
	while(outbox.length){
		const m=outbox.shift();
		try{await fetch('/click?'+auth+'&a='+m.a+'&x='+m.x+'&y='+m.y,{cache:'no-store'});}catch(e){}
	}
	sending=false;
}

// Flat view: the canvas takes clicks and drags
let flatDown=false,flatMoveAt=0;
function flatPoint(e){
	const r=canvas.getBoundingClientRect();
	return uvToGame((e.clientX-r.left)/r.width,1-(e.clientY-r.top)/r.height,GW,GH);
}
canvas.addEventListener('pointerdown',e=>{
	if(e.button!==0)return;
	e.preventDefault();
	canvas.setPointerCapture(e.pointerId);
	flatDown=true;
	const p=flatPoint(e);sendMouse('down',p.x,p.y);
});
canvas.addEventListener('pointermove',e=>{
	const now=performance.now();
	if(now-flatMoveAt<50)return;
	flatMoveAt=now;
	const p=flatPoint(e);sendMouse('move',p.x,p.y);
});
function flatUp(e){if(!flatDown)return;flatDown=false;const p=flatPoint(e);sendMouse('up',p.x,p.y);}
canvas.addEventListener('pointerup',flatUp);
canvas.addEventListener('pointercancel',flatUp);
canvas.addEventListener('contextmenu',e=>e.preventDefault());

// AR
let THREE=null,arOn=false;
async function checkAR(){
	const note=$('note');
	const port=location.port||'80';
	if(!window.isSecureContext){
		note.textContent='La AR necesita https o localhost. Por cable: adb reverse tcp:'+port+' tcp:'+port+' y abre http://localhost:'+port+'/ar'+location.search+' . Mientras, aqui se juega en plano: toca el juego.';
		return;
	}
	let ok=false;
	try{ok=!!navigator.xr&&await navigator.xr.isSessionSupported('immersive-ar');}catch(e){}
	if(!ok){
		note.textContent='Este navegador no tiene WebXR AR (hace falta Chrome en Android con ARCore). Se juega en plano: toca el juego.';
		return;
	}
	note.textContent='Cargando three.js...';
	try{THREE=await import(THREE_URL);}catch(e){
		note.textContent='No se pudo cargar three.js (el celular necesita internet para eso). Se juega en plano.';
		return;
	}
	$('enterAr').hidden=false;
	note.textContent='Toca Entrar en AR y apunta a una mesa con luz.';
}
$('enterAr').addEventListener('click',async()=>{
	if(arOn||!THREE)return;
	let session;
	try{
		session=await navigator.xr.requestSession('immersive-ar',{requiredFeatures:['hit-test'],optionalFeatures:['dom-overlay','anchors'],domOverlay:{root:$('overlay')}});
	}catch(e){$('note').textContent='No se pudo abrir la AR: '+e.message;return;}
	arOn=true;
	try{await runAR(session);}catch(e){$('note').textContent='La AR fallo: '+e.message;session.end().catch(()=>{});}
});
checkAR();

async function runAR(session){
	const T=THREE;
	const overlay=$('overlay'),hasOverlay=!!session.domOverlayState;
	overlay.classList.add('on');
	$('flat').style.display='none';

	const renderer=new T.WebGLRenderer({antialias:true,alpha:true});
	renderer.setPixelRatio(devicePixelRatio);
	renderer.setSize(innerWidth,innerHeight);
	renderer.xr.enabled=true;
	renderer.xr.setReferenceSpaceType('local');
	const scene=new T.Scene();
	const camera=new T.PerspectiveCamera(60,innerWidth/innerHeight,0.01,30);
	scene.add(new T.HemisphereLight(0xffffff,0x7d8a99,2.4));
	const light=new T.DirectionalLight(0xffffff,1.6);
	light.position.set(0.4,1,0.3);
	scene.add(light);

	// The live game is the canvas the feed draws into
	const tex=new T.CanvasTexture(canvas);
	tex.colorSpace=T.SRGBColorSpace;
	tex.anisotropy=Math.min(8,renderer.capabilities.getMaxAnisotropy());
	const onFrame=()=>{tex.needsUpdate=true;};
	frameListeners.push(onFrame);
	let ref=null,hitSource=null,reticleOk=false,anchor=null,anchorGen=0,anchorWanted=false,press=null;
	session.addEventListener('end',()=>{
		renderer.setAnimationLoop(null);
		if(hitSource)try{hitSource.cancel();}catch(e){}
		dropAnchor();
		if(press&&press.kind==='play')sendMouse('up',press.x,press.y);
		press=null;
		const i=frameListeners.indexOf(onFrame);
		if(i>=0)frameListeners.splice(i,1);
		tex.dispose();renderer.dispose();
		overlay.classList.remove('on');
		$('flat').style.display='';
		arOn=false;
	});

	// A soft shadow under the board, so it sits on the table
	const sc=document.createElement('canvas');sc.width=sc.height=128;
	const sg=sc.getContext('2d');
	sg.filter='blur(9px)';sg.fillStyle='rgba(0,0,0,0.55)';sg.fillRect(22,22,84,84);
	const board=buildBoard(T,tex,new T.CanvasTexture(sc),GW,GH),root=board.root,screen=board.screen;
	root.visible=false;scene.add(root);

	const reticle=new T.Mesh(new T.RingGeometry(0.045,0.058,40).rotateX(-Math.PI/2),new T.MeshBasicMaterial({color:0xffffff}));
	reticle.add(new T.Mesh(new T.CircleGeometry(0.008,16).rotateX(-Math.PI/2),new T.MeshBasicMaterial({color:0x93d46b})));
	reticle.matrixAutoUpdate=false;reticle.visible=false;scene.add(reticle);

	const st={mode:'place',tilt:0,rot:0,yaw:0,width:board.width,placed:false};
	window.__ar={T,root,screen,reticle,scene,camera,renderer,st,get ref(){return ref;},get eye(){return eye;},get anchor(){return anchor;}};	// for remote debugging
	function layout(){
		board.layout(st);
		$('tiltv').textContent=st.tilt+' grados';
		$('sizev').textContent=Math.round(st.width*100)+' cm';
	}
	let shownHint='';
	function showHint(t){if(t!==shownHint){shownHint=t;$('mode').textContent=t;}}
	function setMode(m){
		st.mode=m;
		showHint(m==='place'?'Apunta a la mesa hasta ver el circulo y toca para poner el tablero':
			m==='adjust'?'Ajustar: arrastra el tablero por la mesa, inclinalo o giralo. Luego Fijar':
			'Jugar: toca el tablero para hacer clic');
		$('modeBtn').textContent=m==='play'?'Ajustar':'Fijar';
		$('modeBtn').disabled=!st.placed;
	}
	const actions={
		'tilt-':()=>{st.tilt=Math.max(0,st.tilt-15);layout();},
		'tilt+':()=>{st.tilt=Math.min(60,st.tilt+15);layout();},
		'size-':()=>{st.width=Math.max(0.15,st.width/1.25);layout();},
		'size+':()=>{st.width=Math.min(1.6,st.width*1.25);layout();},
		rot:()=>{st.rot=(st.rot+1)%4;layout();},
		place:()=>setMode('place'),
		mode:()=>{if(st.placed)setMode(st.mode==='play'?'adjust':'play');},
	};
	overlay.querySelectorAll('[data-act]').forEach(b=>{b.onclick=()=>actions[b.dataset.act]();});
	$('exit').onclick=()=>session.end().catch(()=>{});
	// Taps on the buttons are not taps on the scene
	for(const el of [$('top'),$('controls')])el.addEventListener('beforexrselect',e=>e.preventDefault());
	layout();setMode('place');

	// Chrome on Android has WebXR Layers now, and three r160 then draws into a projection layer that the
	// AR compositor never shows. Hiding them makes three use the classic XRWebGLLayer, like Google's samples.
	try{Object.defineProperty(XRRenderState.prototype,'layers',{get(){return undefined;},configurable:true});}catch(e){}
	await renderer.xr.setSession(session);
	ref=renderer.xr.getReferenceSpace();
	hitSource=await session.requestHitTestSource({space:await session.requestReferenceSpace('viewer')});

	const m4=new T.Matrix4(),ray=new T.Ray(),rc=new T.Raycaster();
	const reticlePos=new T.Vector3(),eye=new T.Vector3();
	function rayFrom(pose){
		m4.fromArray(pose.transform.matrix);
		ray.origin.setFromMatrixPosition(m4);
		ray.direction.set(0,0,-1).transformDirection(m4);
		return ray;
	}
	function gameAt(r){
		rc.ray.copy(r);
		const h=rc.intersectObject(screen,false)[0];
		return h&&h.uv?uvToGame(h.uv.x,h.uv.y,GW,GH):null;
	}
	function dropAnchor(){
		anchorGen++;
		if(anchor){try{anchor.delete();}catch(e){}anchor=null;}
	}
	// Anchors keep the board on the table as ARCore refines its map; optional. Made on the next
	// animation frame, like the viewer pose: input event frames don't allow everything.
	function anchorHere(frame){
		const gen=++anchorGen,p=root.position;
		try{
			frame.createAnchor(new XRRigidTransform({x:p.x,y:p.y,z:p.z}),ref).then(a=>{
				if(gen!==anchorGen){a.delete();return;}
				anchor=a;
			}).catch(()=>{});
		}catch(e){}
	}

	session.addEventListener('selectstart',e=>{
		const pose=e.frame.getPose(e.inputSource.targetRaySpace,ref);
		if(!pose||press)return;
		const r=rayFrom(pose);
		if(st.mode==='place'){
			if(!reticleOk)return;
			dropAnchor();
			root.position.copy(reticlePos);
			st.yaw=Math.atan2(eye.x-reticlePos.x,eye.z-reticlePos.z);	// the near edge faces the player
			st.rot=0;st.placed=true;root.visible=true;
			layout();anchorWanted=true;
			setMode(hasOverlay?'adjust':'play');
			return;
		}
		if(st.mode==='adjust'){
			const p=rayOnPlaneY(r.origin,r.direction,root.position.y);
			if(!p)return;
			dropAnchor();
			press={src:e.inputSource,kind:'drag',dx:root.position.x-p.x,dz:root.position.z-p.z};
			return;
		}
		const g=gameAt(r);
		if(!g){if(!hasOverlay)setMode('place');return;}	// without buttons, a tap off the board places it again
		press={src:e.inputSource,kind:'play',x:g.x,y:g.y,at:performance.now()};
		sendMouse('down',g.x,g.y);
	});
	session.addEventListener('selectend',e=>{
		if(!press||press.src!==e.inputSource)return;
		const p=press;press=null;
		if(p.kind==='play')sendMouse('up',p.x,p.y);
		else anchorWanted=true;
	});
	renderer.setAnimationLoop((time,frame)=>{
		if(!frame)return;
		const vp=frame.getViewerPose(ref);
		if(vp)eye.set(vp.transform.position.x,vp.transform.position.y,vp.transform.position.z);
		if(anchorWanted&&!press){
			anchorWanted=false;
			if(frame.createAnchor)anchorHere(frame);
		}
		const hits=frame.getHitTestResults(hitSource);
		reticleOk=false;
		if(hits.length){
			const hp=hits[0].getPose(ref);
			if(hp){
				reticle.matrix.fromArray(hp.transform.matrix);
				reticlePos.set(hp.transform.position.x,hp.transform.position.y,hp.transform.position.z);
				reticleOk=true;
			}
		}
		reticle.visible=reticleOk&&st.mode==='place';
		if(st.mode==='place'&&!reticleOk)showHint('Mueve el celular despacio sobre la mesa hasta que salga el circulo');
		else if(st.mode==='place')showHint('Toca para poner el tablero donde esta el circulo');

		if(anchor&&!press){
			const ap=frame.getPose(anchor.anchorSpace,ref);
			if(ap){const q=ap.transform.position;root.position.set(q.x,q.y,q.z);root.updateMatrixWorld(true);}
		}
		if(press){
			const pose=frame.getPose(press.src.targetRaySpace,ref);
			if(pose){
				const r=rayFrom(pose);
				if(press.kind==='drag'){
					const p=rayOnPlaneY(r.origin,r.direction,root.position.y);
					if(p){root.position.x=p.x+press.dx;root.position.z=p.z+press.dz;root.updateMatrixWorld(true);}
				}else{
					const g=gameAt(r),now=performance.now();
					if(g&&Math.abs(g.x-press.x)+Math.abs(g.y-press.y)>=3&&now-press.at>=50){
						press.x=g.x;press.y=g.y;press.at=now;
						sendMouse('move',g.x,g.y);
					}
				}
			}
		}
		renderer.render(scene,camera);
	});
}
</script></body></html>
)HTML";

#endif
