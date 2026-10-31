#ifndef __ARVIEWPAGE_H__
#define __ARVIEWPAGE_H__

// The /ar page (ARView.cpp). Flat: the game arrives as an MJPEG stream read with fetch and drawn into a canvas,
// which is the flat view and the board's texture. 3D: /scene sends, while a lawn is in play, every object drawn
// alone into an atlas (colors and alpha side by side in one JPEG) with a json of where each one stands, and the
// lawn's backdrop now and then; the page builds a diorama with three.js: the backdrop on a wooden base, the
// fence standing at the back, the seed bank floating above it and every object as a card standing on its feet.
// In menus /scene sends the flat picture instead. With WebXR immersive-ar (Chrome on Android) the board or the
// diorama is put on a table found by hit-test; without it the diorama is shown in a canvas with an orbiting
// camera. Taps become mouse events through /click. {GAME_W} and {GAME_H} are filled in by the server.
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
#flat header{width:min(100%,960px);display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}
#flat h1{font-size:18px;margin:0;font-weight:650}
#status{color:var(--muted);font-size:13px}
.seg{display:inline-flex;background:#2a2521;border-radius:10px;padding:3px;gap:2px}
.seg button{font:inherit;font-size:14px;border:0;background:transparent;color:var(--muted);padding:0 14px;min-height:36px;border-radius:8px;cursor:pointer}
.seg button[aria-pressed=true]{background:var(--accent);color:var(--ink);font-weight:650}
#screen{width:min(100%,calc((100vh - 210px)*4/3),960px);aspect-ratio:4/3;background:#000;border-radius:10px;overflow:hidden;position:relative}
#game,#view3d{width:100%;height:100%;display:block;touch-action:none;cursor:pointer}
#view3d{display:none;cursor:grab}
#screen.d3 #game{display:none}
#screen.d3 #view3d{display:block}
#enterAr{font:inherit;font-weight:650;font-size:17px;padding:14px 30px;border-radius:12px;border:0;background:var(--accent);color:var(--ink);cursor:pointer}
#note{margin:0;max-width:640px;color:var(--muted);font-size:13px;text-align:center}
#overlay{display:none;position:fixed;inset:0;background:transparent;color:var(--text)}
#overlay.on{display:block}
.bar{position:absolute;left:0;right:0;display:flex;flex-wrap:wrap;gap:8px;justify-content:center;align-items:center;padding:10px 12px}
#top{top:0;padding-top:max(10px,env(safe-area-inset-top));justify-content:space-between;flex-wrap:nowrap}
#controls{bottom:0;padding-bottom:max(14px,env(safe-area-inset-bottom))}
#mode{background:var(--panel);border-radius:14px;padding:8px 14px;font-size:14px}
#top div{display:flex;gap:8px}
#overlay button{font:inherit;min-width:48px;min-height:48px;padding:0 14px;border-radius:12px;border:1px solid var(--line);background:var(--panel);color:var(--text);touch-action:manipulation}
#overlay button.main{background:var(--accent);color:var(--ink);border-color:transparent;font-weight:650}
#overlay button:active{filter:brightness(1.35)}
#overlay button:disabled{opacity:.45}
.group{display:flex;gap:4px;align-items:center;background:var(--panel);border-radius:14px;padding:4px}
.group[hidden]{display:none}
.group button{border:0!important;background:transparent!important}
.group span{min-width:74px;text-align:center;font-variant-numeric:tabular-nums;font-size:14px}
</style></head><body>
<div id="flat">
<header><h1>PvZ en AR</h1><div class="seg" role="group" aria-label="Vista"><button data-view="flat" aria-pressed="false">Plano</button><button data-view="3d" aria-pressed="false">3D</button></div><span id="status">Conectando...</span></header>
<div id="screen"><canvas id="game" aria-label="El juego en vivo"></canvas><canvas id="view3d" aria-label="El jardin en 3D: arrastra para girar, toca para jugar"></canvas></div>
<button id="enterAr" hidden>Entrar en AR</button>
<p id="note"></p>
</div>
<div id="overlay">
<div id="top" class="bar"><span id="mode"></span><div><button id="viewBtn">Plano</button><button id="exit">Salir</button></div></div>
<div id="controls" class="bar">
<div class="group" id="tiltGroup"><button data-act="tilt-" aria-label="Menos inclinado">&#9660;</button><span id="tiltv">0 grados</span><button data-act="tilt+" aria-label="Mas inclinado">&#9650;</button></div>
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
const params=new URLSearchParams(location.search);
const auth='c='+encodeURIComponent(params.get('c')||'');
const $=id=>document.getElementById(id);

// helpers: begin (no DOM, tested in node)
function uvToGame(u,v,w,h){
	return{x:Math.min(w-1,Math.max(0,Math.floor(u*w))),y:Math.min(h-1,Math.max(0,Math.floor((1-v)*h)))};
}
function findHeaderEnd(b){
	for(let i=0;i+3<b.length;i++)if(b[i]===13&&b[i+1]===10&&b[i+2]===13&&b[i+3]===10)return i;
	return -1;
}
// Splits multipart/x-mixed-replace bytes (each part with a Content-Length) into parts: {head, body}.
class PartSplitter{
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
			out.push({head:head,body:this.buf.slice(start,start+len)});
			this.buf=this.buf.slice(start+len);
		}
		if(this.buf.length>(16<<20))this.buf=new Uint8Array(0);
		return out;
	}
}
class MjpegSplitter{
	constructor(){this.parts=new PartSplitter();}
	push(chunk){return this.parts.push(chunk).map(p=>p.body);}
}
// A /scene part: X-Parts gives the lengths of the json, the picture (the atlas, or the screen when flat) and the
// backdrop (0 when it didn't change).
function sceneParts(head,body){
	const m=/x-parts:\s*(\d+),(\d+),(\d+)/i.exec(head);
	if(!m)return null;
	const j=+m[1],a=+m[2],b=+m[3];
	if(j+a+b>body.length)return null;
	let text='';
	for(let i=0;i<j;i++)text+=String.fromCharCode(body[i]);
	let json;
	try{json=JSON.parse(text);}catch(e){return null;}
	return{json:json,picture:a?body.subarray(j,j+a):null,backdrop:b?body.subarray(j+a,j+a+b):null};
}
// The ray (origin o, direction d) against the horizontal plane at height y, or null.
function rayOnPlaneY(o,d,y){
	if(d.y>-1e-3)return null;
	const t=(y-o.y)/d.y;
	if(t<0||t>20)return null;
	return{x:o.x+d.x*t,y:y,z:o.z+d.z*t};
}
// The board: a wooden slab with the lawn raised a little on it, tilting on its near edge (+z, toward the
// player). Sizes in meters for a 40 cm wide board; layout() scales, tilts and turns it. The diorama hangs from
// the scaler, so it follows the board without its tilt.
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
		const a=(st.flat===false?0:st.tilt)*Math.PI/180,c=Math.cos(a);
		scaler.scale.setScalar(st.width/W0);
		hinge.rotation.x=a;
		turn.rotation.y=st.yaw+st.rot*Math.PI/2;
		shadow.scale.set(1,Math.max(0.35,c),1);
		shadow.position.z=EDGE*(1-c);
		root.updateMatrixWorld(true);
	}
	return{root:root,screen:screen,hinge:hinge,scaler:scaler,layout:layout,width:W0};
}

// The diorama, in game pixels: x across (0 is the game's x 400), z toward the player (0 is the game's y 300),
// y up from the table. The lawn lies at height BASE.
const DIO={BASE:32,RIM:10,LIP:46,HOUSE:-220};
// Where the lawn lies for each background (bt: 0 day, 1 night, 2 pool, 3 fog, 4 roof), in game coordinates:
// x0..x1 across (the porch to the sidewalk), from the back edge, where the fence or the sky stands up, to the
// bottom of the screen.
function lawnLayout(bt,gw,gh){
	return{x0:-40,x1:gw+60,back:bt===4?58:bt>=2?72:74,front:gh};
}
function gameToLocal(x,y,gw,gh){return{x:x-gw/2,z:y-gh/2};}
function localToGame(x,z,gw,gh){return{x:x+gw/2,y:z+gh/2};}
// The across direction of a card standing at (fx,fz) and turned toward a camera at (cx,cz) around the vertical
function cardRight(fx,fz,cx,cz){
	let dx=cx-fx,dz=cz-fz;
	const l=Math.hypot(dx,dz);
	if(l<1e-6){dx=0;dz=1;}else{dx/=l;dz/=l;}
	return{x:dz,z:-dx,nx:dx,nz:dz};
}
// A ray (local coordinates) against a figure's card: its feet f {x,z}, box {l,r,b,t} (pixels across from the
// feet, and up from the lawn). Returns the ray parameter, or -1.
function rayOnCard(o,d,f,box,cam,base){
	const c=cardRight(f.x,f.z,cam.x,cam.z);
	const den=d.x*c.nx+d.z*c.nz;
	if(Math.abs(den)<1e-9)return -1;
	const t=((f.x-o.x)*c.nx+(f.z-o.z)*c.nz)/den;
	if(t<=0)return -1;
	const px=o.x+d.x*t-f.x,py=o.y+d.y*t-base,pz=o.z+d.z*t-f.z;
	const s=px*c.x+pz*c.z;
	return s>=box.l&&s<=box.r&&py>=box.b&&py<=box.t?t:-1;
}
// One figure of a scene (the server's array) as the page keeps it: feet, box and atlas rectangle.
function figureOf(o){
	const[id,kind,row,fx,fy,ax,ay,aw,ah,ox,oy,face,order,cx,cy]=o;
	return{id:id,kind:kind,row:row,fx:fx,fy:fy,rect:[ax,ay,aw,ah],box:{l:-ox,r:aw-ox,b:oy-ah,t:oy},face:face,order:order,cx:cx,cy:cy};
}
// Positions between two scenes: each figure slides from where it was shown to its new place (and height) over
// the time between the scenes, so a pea or a falling sun moves smoothly at any update rate.
class Tracks{
	constructor(){this.map=new Map();this.lastT=0;}
	update(figs,serverT,now){
		const dur=this.lastT?Math.min(250,Math.max(40,serverT-this.lastT)):80;
		this.lastT=serverT;
		const next=new Map();
		for(const f of figs){
			const old=this.map.get(f.id);
			const tr={f:f,t0:now,dur:dur,x0:f.fx,y0:f.fy,h0:f.box.b};
			if(old){
				const p=this.at(old,now);
				if(Math.abs(p.x-f.fx)+Math.abs(p.y-f.fy)<160){tr.x0=p.x;tr.y0=p.y;tr.h0=p.h;}
			}
			next.set(f.id,tr);
		}
		this.map=next;
	}
	at(tr,now){
		const a=Math.min(1,Math.max(0,(now-tr.t0)/tr.dur));
		return{x:tr.x0+(tr.f.fx-tr.x0)*a,y:tr.y0+(tr.f.fy-tr.y0)*a,h:tr.h0+(tr.f.box.b-tr.h0)*a};
	}
	// What to draw now: feet in local coordinates and the box raised or lowered to the interpolated height
	shown(now,gw,gh){
		const out=[];
		for(const tr of this.map.values()){
			const p=this.at(tr,now),f=tr.f,dh=p.h-f.box.b,l=gameToLocal(p.x,p.y,gw,gh);
			out.push({f:f,x:l.x,z:l.z,box:{l:f.box.l,r:f.box.r,b:f.box.b+dh,t:f.box.t+dh}});
		}
		return out;
	}
}
// Far to near from the camera, so the cards blend over each other in order
function sortFar(list,cam){
	for(const s of list){const dx=s.x-cam.x,dz=s.z-cam.z,dy=(s.box.b+s.box.t)/2-cam.y;s.d=dx*dx+dy*dy+dz*dz;}
	return list.sort((a,b)=>b.d-a.d||a.f.order-b.f.order);
}
// Where a tap lands (local ray o,d): a sun or coin first, even behind a plant (in the game they are drawn over
// everything); otherwise the seed bank panel, a clickable card (plant, grave) or the lawn, whichever is nearest.
// Game coordinates or null.
function pickScene(o,d,shown,cam,panel,lawn,base,gw,gh){
	let best=-1,hit=null;
	for(const s of shown){
		if(s.f.kind!=='c'||s.f.cx<0)continue;
		const t=rayOnCard(o,d,{x:s.x,z:s.z},s.box,cam,base);
		if(t>0&&(best<0||t<best)){best=t;hit={x:s.f.cx,y:s.f.cy};}
	}
	if(hit)return hit;
	if(panel){
		const t=panel.ray(o,d);
		if(t>0){best=t;hit=panel.game(o,d,t);}
	}
	for(const s of shown){
		if(s.f.cx<0)continue;
		const t=rayOnCard(o,d,{x:s.x,z:s.z},s.box,cam,base);
		if(t>0&&(best<0||t<best)){best=t;hit={x:s.f.cx,y:s.f.cy};}
	}
	const p=rayOnPlaneY(o,d,base);
	if(p){
		const g=localToGame(p.x,p.z,gw,gh);
		const t=Math.abs(d.y)>1e-9?(base-o.y)/d.y:-1;
		if(g.x>=lawn.x0&&g.x<=lawn.x1&&g.y>=lawn.back&&g.y<=lawn.front&&t>0&&(best<0||t<best)){
			best=t;hit={x:Math.round(Math.min(gw-1,Math.max(0,g.x))),y:Math.round(Math.min(gh-1,Math.max(0,g.y)))};
		}
	}
	return hit;
}
const FIG_VS=`
uniform vec3 uCam;
attribute vec3 iFeet;
attribute vec4 iBox;
attribute vec4 iUv;
varying vec2 vUv;
void main(){
	vec2 d=uCam.xz-iFeet.xz;
	float l=length(d);
	d=l>1e-3?d/l:vec2(0.0,1.0);
	vec3 across=vec3(d.y,0.0,-d.x);
	vec3 p=iFeet+across*mix(iBox.x,iBox.y,position.x)+vec3(0.0,mix(iBox.z,iBox.w,position.y),0.0);
	vUv=vec2(mix(iUv.x,iUv.z,position.x),mix(iUv.w,iUv.y,position.y));
	gl_Position=projectionMatrix*modelViewMatrix*vec4(p,1.0);
}`;
const PANEL_VS=`
uniform vec4 uUv;
varying vec2 vUv;
void main(){
	vUv=vec2(mix(uUv.x,uUv.z,uv.x),mix(uUv.w,uUv.y,uv.y));
	gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);
}`;
// The atlas: colors (premultiplied) on the left half, alpha on the right; JPEG noise is cut at the bottom
const ATLAS_FS=`
uniform sampler2D uMap;
varying vec2 vUv;
void main(){
	vec3 c=texture2D(uMap,vUv).rgb;
	float a=texture2D(uMap,vUv+vec2(0.5,0.0)).r;
	a=clamp((a-0.04)/0.92,0.0,1.0);
	if(a<0.01&&max(c.r,max(c.g,c.b))<0.04)discard;
	gl_FragColor=vec4(min(c,vec3(1.0)),a);
}`;
const SHADOW_VS=`
attribute vec4 iShadow;
varying vec2 vP;
varying float vA;
uniform float uBase;
void main(){
	vP=position.xy;
	vA=iShadow.w;
	vec3 p=vec3(iShadow.x+position.x*iShadow.z,uBase+0.6,iShadow.y+position.y*iShadow.z*0.62);
	gl_Position=projectionMatrix*modelViewMatrix*vec4(p,1.0);
}`;
const SHADOW_FS=`
varying vec2 vP;
varying float vA;
void main(){
	float r=length(vP);
	gl_FragColor=vec4(0.0,0.0,0.0,vA*(1.0-smoothstep(0.25,1.0,r)));
}`;
function quadGeometry(T,p,uv){
	const g=new T.BufferGeometry();
	g.setAttribute('position',new T.Float32BufferAttribute(p.flat(),3));
	g.setAttribute('uv',new T.Float32BufferAttribute(uv.flat(),2));
	g.setIndex([0,1,2,2,1,3]);
	return g;
}
function premultipliedMaterial(T,opts){
	return new T.ShaderMaterial(Object.assign({transparent:true,depthWrite:false,side:T.DoubleSide,blending:T.CustomBlending,
		blendSrc:T.OneFactor,blendDst:T.OneMinusSrcAlphaFactor,blendSrcAlpha:T.OneFactor,blendDstAlpha:T.OneMinusSrcAlphaFactor},opts));
}
// The diorama of a scene. update() takes a scene's json and its decoded atlas (and backdrop), frame() moves the
// cards toward the camera before each render, pick() turns a world ray into a click in game coordinates.
function buildDiorama(T,gw,gh){
	const BASE=DIO.BASE,MAXI=600;
	const group=new T.Group();
	group.scale.setScalar(0.40/gw);
	group.visible=false;
	const bgTex=new T.Texture();
	bgTex.flipY=false;bgTex.colorSpace=T.SRGBColorSpace;bgTex.minFilter=T.LinearMipmapLinearFilter;bgTex.anisotropy=8;
	const atlas=new T.Texture();
	atlas.flipY=false;atlas.minFilter=T.LinearFilter;atlas.magFilter=T.LinearFilter;atlas.generateMipmaps=false;
	const wood=new T.MeshStandardMaterial({color:0x6e4526,roughness:0.75});
	const bgMat=new T.MeshBasicMaterial({map:bgTex,toneMapped:false,side:T.DoubleSide});
	const parts=new T.Group();group.add(parts);
	let lawn=lawnLayout(0,gw,gh),bt=-1,bgx=-220,bgw=1400,bgh=600,houseH=0;
	const L=(x,y)=>gameToLocal(x,y,gw,gh),uvAt=(x,y)=>[(x-bgx)/bgw,y/bgh];
	function rebuild(){
		for(const m of parts.children.slice()){parts.remove(m);m.geometry.dispose();}
		const a=L(lawn.x0,lawn.back),b=L(lawn.x1,lawn.front),R=DIO.RIM;
		const box=new T.Mesh(new T.BoxGeometry(b.x-a.x+2*R,BASE,b.z-a.z+2*R+DIO.LIP),wood);
		box.position.set((a.x+b.x)/2,BASE/2,(a.z+b.z+DIO.LIP)/2);parts.add(box);
		// the lawn, lying on the base
		parts.add(new T.Mesh(quadGeometry(T,[[a.x,BASE+0.4,b.z],[b.x,BASE+0.4,b.z],[a.x,BASE+0.4,a.z],[b.x,BASE+0.4,a.z]],
			[uvAt(lawn.x0,lawn.front),uvAt(lawn.x1,lawn.front),uvAt(lawn.x0,lawn.back),uvAt(lawn.x1,lawn.back)]),bgMat));
		// the fence or the sky, standing at the back
		const top=BASE+lawn.back;
		parts.add(new T.Mesh(quadGeometry(T,[[a.x,BASE,a.z],[b.x,BASE,a.z],[a.x,top,a.z],[b.x,top,a.z]],
			[uvAt(lawn.x0,lawn.back),uvAt(lawn.x1,lawn.back),uvAt(lawn.x0,0),uvAt(lawn.x1,0)]),bgMat));
		// the house: what the picture has left of the lawn, folded up along its left edge
		houseH=lawn.x0-DIO.HOUSE;
		parts.add(new T.Mesh(quadGeometry(T,[[a.x,BASE,b.z],[a.x,BASE,a.z],[a.x,BASE+houseH,b.z],[a.x,BASE+houseH,a.z]],
			[uvAt(lawn.x0,lawn.front),uvAt(lawn.x0,lawn.back),uvAt(DIO.HOUSE,lawn.front),uvAt(DIO.HOUSE,lawn.back)]),bgMat));
		panel.position.z=a.z-3;
		panel.position.y=top+6;
		meter.position.set(0,BASE+0.6,b.z+R+DIO.LIP-6);
	}

	// The seed bank and the menu, a live piece of the top of the screen standing above the back edge
	const unitQuad=new T.PlaneGeometry(1,1).translate(0.5,0.5,0);
	function panelMesh(order,geometry){
		const m=new T.Mesh(geometry||unitQuad,premultipliedMaterial(T,{uniforms:{uMap:{value:atlas},uUv:{value:new T.Vector4()}},vertexShader:PANEL_VS,fragmentShader:ATLAS_FS}));
		m.renderOrder=order;m.visible=false;m.frustumCulled=false;group.add(m);
		return m;
	}
	const panel=panelMesh(2),meter=panelMesh(2),message=panelMesh(4,new T.PlaneGeometry(1,1));
	panel.rotation.x=-0.3;
	meter.rotation.x=-Math.PI/2+0.35;
	let hud=null;
	const panelPick={
		ray(o,d){
			if(!hud||!panel.visible)return -1;
			const n=new T.Vector3(0,0,1).applyQuaternion(panel.quaternion),p=panel.position;
			const den=d.x*n.x+d.y*n.y+d.z*n.z;
			if(Math.abs(den)<1e-9)return -1;
			const t=((p.x-o.x)*n.x+(p.y-o.y)*n.y+(p.z-o.z)*n.z)/den;
			if(t<=0)return -1;
			const q=new T.Vector3(o.x+d.x*t-p.x,o.y+d.y*t-p.y,o.z+d.z*t-p.z).applyQuaternion(panel.quaternion.clone().invert());
			const u=q.x/panel.scale.x,v=q.y/panel.scale.y;
			return u>=0&&u<=1&&v>=0&&v<=1?t:-1;
		},
		game(o,d,t){
			const p=panel.position,q=new T.Vector3(o.x+d.x*t-p.x,o.y+d.y*t-p.y,o.z+d.z*t-p.z).applyQuaternion(panel.quaternion.clone().invert());
			return{x:Math.round(hud[4]+q.x),y:Math.round(hud[5]+hud[3]-q.y)};
		}
	};

	// Every figure in one draw: a card per instance, turned toward the camera in the vertex shader
	const fg=new T.InstancedBufferGeometry();
	fg.setAttribute('position',new T.Float32BufferAttribute([0,0,0,1,0,0,0,1,0,1,1,0],3));
	fg.setIndex([0,1,2,2,1,3]);
	const iFeet=new T.InstancedBufferAttribute(new Float32Array(MAXI*3),3),iBox=new T.InstancedBufferAttribute(new Float32Array(MAXI*4),4),iUv=new T.InstancedBufferAttribute(new Float32Array(MAXI*4),4);
	for(const a of [iFeet,iBox,iUv])a.setUsage(T.DynamicDrawUsage);
	fg.setAttribute('iFeet',iFeet);fg.setAttribute('iBox',iBox);fg.setAttribute('iUv',iUv);
	fg.instanceCount=0;
	const figMat=premultipliedMaterial(T,{uniforms:{uMap:{value:atlas},uCam:{value:new T.Vector3()}},vertexShader:FIG_VS,fragmentShader:ATLAS_FS});
	const figs=new T.Mesh(fg,figMat);figs.frustumCulled=false;figs.renderOrder=3;group.add(figs);
	// A soft shadow under each, lighter the higher it is
	const sg=new T.InstancedBufferGeometry();
	sg.setAttribute('position',new T.Float32BufferAttribute([-1,-1,0,1,-1,0,-1,1,0,1,1,0],3));
	sg.setIndex([0,1,2,2,1,3]);
	const iShadow=new T.InstancedBufferAttribute(new Float32Array(MAXI*4),4);iShadow.setUsage(T.DynamicDrawUsage);
	sg.setAttribute('iShadow',iShadow);sg.instanceCount=0;
	const shadows=new T.Mesh(sg,new T.ShaderMaterial({uniforms:{uBase:{value:BASE}},vertexShader:SHADOW_VS,fragmentShader:SHADOW_FS,transparent:true,depthWrite:false}));
	shadows.frustumCulled=false;shadows.renderOrder=1;group.add(shadows);

	const tracks=new Tracks();
	let aw=1024,ah=1024,shown=[],scene=null;
	const cam=new T.Vector3(),inv=new T.Matrix4(),v1=new T.Vector3(),v2=new T.Vector3(),q=new T.Quaternion(),qg=new T.Quaternion();
	function setUv(m,r){
		m.material.uniforms.uUv.value.set((r[0]+0.5)/(2*aw),(r[1]+0.5)/ah,(r[0]+r[2]-0.5)/(2*aw),(r[1]+r[3]-0.5)/ah);
		m.scale.set(r[2],r[3],1);
		m.visible=true;
	}
	// The pictures replaced at the previous update have been drawn with since: they can go
	let retired=[];
	function update(js,atlasImage,backdropImage,now){
		for(const b of retired)if(b.close)b.close();
		retired=[];
		if(atlas.image&&atlas.image!==atlasImage)retired.push(atlas.image);
		if(backdropImage){if(bgTex.image&&bgTex.image!==backdropImage)retired.push(bgTex.image);bgTex.image=backdropImage;bgTex.needsUpdate=true;}
		if(js.bt!==bt||js.bgx!==bgx){bt=js.bt;bgx=js.bgx;bgw=js.bgw;bgh=js.bgh;lawn=lawnLayout(bt,gw,gh);rebuild();}
		// three keeps a texture's first size (texStorage2D): a new size needs a new texture
		if(atlas.image&&(atlas.image.width!==atlasImage.width||atlas.image.height!==atlasImage.height))atlas.dispose();
		atlas.image=atlasImage;atlas.needsUpdate=true;
		aw=js.aw;ah=js.ah;scene=js;
		hud=js.hud;
		if(hud){setUv(panel,hud);panel.position.x=hud[4]-gw/2;}else panel.visible=false;
		if(js.meter){setUv(meter,js.meter);meter.position.x=js.meter[4]-gw/2;}else meter.visible=false;
		if(js.msg){setUv(message,js.msg);message.position.set(js.msg[4]+js.msg[2]/2-gw/2,BASE+190,0);}else message.visible=false;
		const list=[];
		for(const o of js.o)list.push(figureOf(o));
		tracks.update(list,js.t,now);
		group.visible=!!bgTex.image;
	}
	// Before each render: the camera in the diorama's space, the cards in order and their shadows
	function frame(now,camWorld,camQuat){
		group.updateWorldMatrix(true,false);
		inv.copy(group.matrixWorld).invert();
		cam.copy(camWorld).applyMatrix4(inv);
		figMat.uniforms.uCam.value.copy(cam);
		if(message.visible&&camQuat){
			group.matrixWorld.decompose(v1,qg,v2);
			message.quaternion.copy(q.copy(qg).invert().multiply(camQuat));
		}
		// only what stands on the base: zombies wait on the street out of sight, as off the game's screen
		shown=sortFar(tracks.shown(now,gw,gh).filter(s=>s.x+gw/2>=lawn.x0-30&&s.x+gw/2<=lawn.x1+10),cam);
		const n=Math.min(MAXI,shown.length),U=1/(2*aw),V=1/ah;
		const F=iFeet.array,Bx=iBox.array,Uv=iUv.array,Sh=iShadow.array;
		let ns=0;
		for(let i=0;i<n;i++){
			const s=shown[i],r=s.f.rect,b=s.box,k=s.f.kind;
			F[i*3]=s.x;F[i*3+1]=BASE;F[i*3+2]=s.z;
			Bx[i*4]=b.l;Bx[i*4+1]=b.r;Bx[i*4+2]=b.b;Bx[i*4+3]=b.t;
			Uv[i*4]=(r[0]+0.5)*U;Uv[i*4+1]=(r[1]+0.5)*V;Uv[i*4+2]=(r[0]+r[2]-0.5)*U;Uv[i*4+3]=(r[1]+r[3]-0.5)*V;
			const w=b.r-b.l,h=Math.max(0,b.b);
			const rad=k==='z'?Math.min(46,w*0.32):k==='p'?Math.min(40,w*0.42):k==='c'?22:k==='b'?Math.min(14,w*0.3):k==='m'?36:Math.min(36,w*0.4);
			const alpha=0.42*Math.max(0,1-h/260);
			if(alpha<0.02)continue;
			Sh[ns*4]=s.x;Sh[ns*4+1]=s.z;Sh[ns*4+2]=rad;Sh[ns*4+3]=alpha;ns++;
		}
		fg.instanceCount=n;sg.instanceCount=ns;
		iFeet.needsUpdate=iBox.needsUpdate=iUv.needsUpdate=iShadow.needsUpdate=true;
	}
	function pick(rayOrigin,rayDir){
		if(!group.visible||!scene)return null;
		const o=v1.copy(rayOrigin).applyMatrix4(inv),e=v2.copy(rayOrigin).add(rayDir).applyMatrix4(inv);
		const d={x:e.x-o.x,y:e.y-o.y,z:e.z-o.z};
		return pickScene({x:o.x,y:o.y,z:o.z},d,shown,cam,panelPick,lawn,BASE,gw,gh);
	}
	rebuild();
	return{group:group,update:update,frame:frame,pick:pick,tracks:tracks,get shown(){return shown;},get lawn(){return lawn;},atlas:atlas,backdrop:bgTex,panel:panel};
}
// helpers: end

const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const canvas=$('game');
canvas.width=GW;canvas.height=GH;
const ctx=canvas.getContext('2d');
ctx.fillStyle='#000';ctx.fillRect(0,0,GW,GH);

// Flat or 3D, remembered on this phone
let view='3d';
try{const v=localStorage.getItem('pvzArView');if(v==='flat'||v==='3d')view=v;}catch(e){}
if(params.get('view')==='flat'||params.get('view')==='3d')view=params.get('view');

// The picture: a stream read with fetch (/stream flat, /scene in 3D), or single pictures if that fails
const frameListeners=[];
let pending=null,decoding=false,lastFrameAt=0,lastError=0,polling=false,received=0,sceneLive=false,lastSceneAt=0;
function decode(blob){
	if(self.createImageBitmap)return createImageBitmap(blob,{premultiplyAlpha:'none',colorSpaceConversion:'none'}).catch(()=>createImageBitmap(blob));
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
function setLive(on){
	if(on===sceneLive)return;
	sceneLive=on;
	updateViewUi();
}
// Scenes are decoded in order, dropping all but the latest (a dropped backdrop is kept for the next one)
const sceneState={seq:0,json:null,atlas:null,backdrop:null,backdropSeq:0};
let pendingScene=null,decodingScene=false;
function onScenePart(p){
	received++;
	const s=sceneParts(p.head,p.body);
	if(!s)return;
	if(s.json.flat){if(s.picture)onJpeg(s.picture);setLive(false);return;}
	if(pendingScene&&pendingScene.backdrop&&!s.backdrop)s.backdrop=pendingScene.backdrop;
	pendingScene=s;
	if(!decodingScene)decodeScenes();
}
async function decodeScenes(){
	decodingScene=true;
	while(pendingScene){
		const s=pendingScene;pendingScene=null;
		try{
			const[atlas,back]=await Promise.all([decode(new Blob([s.picture],{type:'image/jpeg'})),s.backdrop?decode(new Blob([s.backdrop],{type:'image/jpeg'})):null]);
			if(sceneState.atlas&&sceneState.atlas.close&&sceneState.atlasUsed!==sceneState.seq)sceneState.atlas.close();
			sceneState.atlas=atlas;sceneState.json=s.json;sceneState.seq++;
			if(back){
				if(sceneState.backdrop&&sceneState.backdrop.close&&sceneState.backdropUsed!==sceneState.backdropSeq)sceneState.backdrop.close();
				sceneState.backdrop=back;sceneState.backdropSeq++;
			}
			lastFrameAt=lastSceneAt=performance.now();
			if(sceneState.backdrop)setLive(true);
		}catch(e){}
	}
	decodingScene=false;
}
// A diorama takes the latest scene before it renders; returns true when there was a new one
function applyScene(dio,mine,now){
	if(!sceneState.json||mine.seq===sceneState.seq)return false;
	const back=mine.backdropSeq!==sceneState.backdropSeq?sceneState.backdrop:null;
	dio.update(sceneState.json,sceneState.atlas,back,now);
	mine.seq=sceneState.seq;mine.backdropSeq=sceneState.backdropSeq;
	sceneState.atlasUsed=sceneState.seq;sceneState.backdropUsed=sceneState.backdropSeq;
	return true;
}
let feedAbort=null,feedGen=0;
async function readParts(url,onPart,signal){
	const res=await fetch(url,{cache:'no-store',signal:signal});
	if(!res.ok||!res.body){lastError=res.status;throw new Error('stream '+res.status);}
	lastError=0;
	const reader=res.body.getReader(),split=new PartSplitter();
	for(;;){
		const r=await reader.read();
		if(r.done)return;
		for(const p of split.push(r.value))onPart(p);
	}
}
async function pollOnce(scene,signal){
	if(scene){
		const r=await fetch('/scene.bin?'+auth+'&t='+Date.now(),{cache:'no-store',signal:signal});
		if(r.ok){
			lastError=0;
			onScenePart({head:'X-Parts: '+r.headers.get('X-Parts'),body:new Uint8Array(await r.arrayBuffer())});
			return;
		}
		if(r.status!==503){lastError=r.status;return;}
		setLive(false);
	}
	const r=await fetch('/frame.jpg?'+auth+'&t='+Date.now(),{cache:'no-store',signal:signal});
	lastError=r.ok||r.status===503?0:r.status;
	if(r.ok)onJpeg(new Uint8Array(await r.arrayBuffer()));
}
async function feed(){
	for(;;){
		const gen=feedGen,ctl=new AbortController();
		feedAbort=ctl;polling=false;
		let fails=0;
		while(gen===feedGen){
			const scene=view==='3d';
			if(fails<3){
				const before=received;
				try{await readParts((scene?'/scene?':'/stream?')+auth+'&t='+Date.now(),scene?onScenePart:p=>{received++;onJpeg(p.body);},ctl.signal);}catch(e){}
				if(gen!==feedGen)break;
				fails=received>before?0:fails+1;
				await sleep(800);
			}else{
				polling=true;
				try{await pollOnce(scene,ctl.signal);}catch(e){}
				await sleep(scene?90:70);
			}
		}
	}
}
function setView(v){
	if(v===view)return;
	view=v;
	try{localStorage.setItem('pvzArView',v);}catch(e){}
	if(v==='flat')setLive(false);
	feedGen++;
	if(feedAbort)feedAbort.abort();
	updateViewUi();
}
setInterval(()=>{
	const now=performance.now(),live=now-lastFrameAt<1500;
	if(sceneLive&&now-lastSceneAt>3000)setLive(false);
	$('status').textContent=lastError===403?'El codigo no vale: abre la direccion que sale en el juego':
		live?(sceneLive?'En vivo, en 3D':'En vivo')+(polling?' (imagenes sueltas)':''):'Sin imagen, reconectando...';
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

let THREE=null,threeLoading=null,arOn=false,preview=null;
function loadThree(){
	if(!threeLoading)threeLoading=import(THREE_URL).then(m=>{THREE=m;return m;});
	return threeLoading;
}
function updateViewUi(){
	document.querySelectorAll('.seg button').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view===view)));
	$('viewBtn').textContent=view==='3d'?'Ver plano':'Ver en 3D';
	const show=view==='3d'&&sceneLive&&!arOn;
	$('screen').classList.toggle('d3',show);
	if(show&&!preview)startPreview();
	if(preview)preview.setActive(show);
	if(window.__ar&&window.__ar.onView)window.__ar.onView();
}
document.querySelectorAll('.seg button').forEach(b=>{b.onclick=()=>setView(b.dataset.view);});
$('viewBtn').onclick=()=>setView(view==='3d'?'flat':'3d');
updateViewUi();

// Without AR: the diorama in a canvas, the camera orbiting it (drag to turn, wheel or pinch to zoom, tap to play)
async function startPreview(){
	preview={setActive(){}};
	let T;
	try{T=await loadThree();}catch(e){$('note').textContent='No se pudo cargar three.js para el 3D: se ve en plano.';preview=null;setView('flat');return;}
	const cv=$('view3d');
	const renderer=new T.WebGLRenderer({canvas:cv,antialias:true});
	renderer.setPixelRatio(Math.min(2,devicePixelRatio));
	const scene=new T.Scene();
	scene.background=new T.Color(0x1d1915);
	const camera=new T.PerspectiveCamera(45,4/3,0.01,20);
	scene.add(new T.HemisphereLight(0xffffff,0x7d8a99,2.4));
	const light=new T.DirectionalLight(0xffffff,1.6);light.position.set(0.4,1,0.3);scene.add(light);
	const table=new T.Mesh(new T.CircleGeometry(0.42,64).rotateX(-Math.PI/2),new T.MeshStandardMaterial({color:0x2b241f,roughness:0.95}));
	table.position.y=-0.0005;scene.add(table);
	const dio=buildDiorama(T,GW,GH);scene.add(dio.group);
	const mine={seq:0,backdropSeq:0};
	const cp=(params.get('cam')||'').split(',').map(Number);
	const orbit={yaw:cp.length===3&&isFinite(cp[0])?cp[0]*Math.PI/180:0,pitch:cp.length===3&&isFinite(cp[1])?cp[1]*Math.PI/180:0.72,dist:cp.length===3&&isFinite(cp[2])?cp[2]:0.5};
	const target=new T.Vector3(0,0.03,0.01);
	function place(){
		orbit.pitch=Math.min(1.5,Math.max(0.12,orbit.pitch));
		orbit.dist=Math.min(2.5,Math.max(0.15,orbit.dist));
		const c=Math.cos(orbit.pitch);
		camera.position.set(target.x+orbit.dist*c*Math.sin(orbit.yaw),target.y+orbit.dist*Math.sin(orbit.pitch),target.z+orbit.dist*c*Math.cos(orbit.yaw));
		camera.lookAt(target);
	}
	function resize(){
		const r=cv.getBoundingClientRect();
		if(r.width<2)return;
		renderer.setSize(r.width,r.height,false);
		camera.aspect=r.width/r.height;camera.updateProjectionMatrix();
	}
	window.addEventListener('resize',resize);
	let active=false;
	function loop(){
		const now=performance.now();
		applyScene(dio,mine,now);
		place();
		camera.updateMatrixWorld();
		dio.frame(now,camera.position,camera.quaternion);
		renderer.render(scene,camera);
	}
	const pointers=new Map();
	let down=null,pinch=0;
	const rc=new T.Raycaster();
	function tap(e){
		const r=cv.getBoundingClientRect();
		rc.setFromCamera(new T.Vector2((e.clientX-r.left)/r.width*2-1,-((e.clientY-r.top)/r.height)*2+1),camera);
		const g=dio.pick(rc.ray.origin,rc.ray.direction);
		if(g){sendMouse('down',g.x,g.y);sendMouse('up',g.x,g.y);}
		return g;
	}
	cv.addEventListener('pointerdown',e=>{
		cv.setPointerCapture(e.pointerId);
		pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});
		if(pointers.size===1)down={x:e.clientX,y:e.clientY,moved:false};
		else{down=null;const p=[...pointers.values()];pinch=Math.hypot(p[0].x-p[1].x,p[0].y-p[1].y);}
	});
	cv.addEventListener('pointermove',e=>{
		const old=pointers.get(e.pointerId);
		if(!old)return;
		const dx=e.clientX-old.x,dy=e.clientY-old.y;
		pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});
		if(pointers.size===2){
			const p=[...pointers.values()],d=Math.hypot(p[0].x-p[1].x,p[0].y-p[1].y);
			if(pinch>0)orbit.dist*=pinch/d;
			pinch=d;return;
		}
		if(!down)return;
		if(Math.abs(e.clientX-down.x)+Math.abs(e.clientY-down.y)>6)down.moved=true;
		if(down.moved){orbit.yaw-=dx*0.008;orbit.pitch+=dy*0.006;}
	});
	function up(e){
		if(!pointers.has(e.pointerId))return;
		pointers.delete(e.pointerId);
		if(down&&!down.moved&&e.type==='pointerup')tap(e);
		down=null;
	}
	cv.addEventListener('pointerup',up);
	cv.addEventListener('pointercancel',up);
	cv.addEventListener('wheel',e=>{e.preventDefault();orbit.dist*=Math.exp(e.deltaY*0.001);},{passive:false});
	cv.addEventListener('contextmenu',e=>e.preventDefault());
	preview={
		setActive(on){
			if(on===active)return;
			active=on;
			if(on)resize();
			renderer.setAnimationLoop(on?loop:null);
		},
	};
	window.__ar={preview:true,T:T,renderer:renderer,scene:scene,camera:camera,diorama:dio,orbit:orbit,tap:tap,sceneState:sceneState,get view(){return view;},get live(){return sceneLive;}};
	updateViewUi();
}

// AR
async function checkAR(){
	const note=$('note');
	const port=location.port||'80';
	if(!window.isSecureContext){
		note.textContent='La AR necesita https o localhost. Por cable: adb reverse tcp:'+port+' tcp:'+port+' y abre http://localhost:'+port+'/ar'+location.search+' . Mientras, aqui se juega en plano o en 3D: toca el juego.';
		return;
	}
	let ok=false;
	try{ok=!!navigator.xr&&await navigator.xr.isSessionSupported('immersive-ar');}catch(e){}
	if(!ok){
		note.textContent='Este navegador no tiene WebXR AR (hace falta Chrome en Android con ARCore). Se juega aqui: en 3D arrastra para girar el jardin y toca para jugar.';
		return;
	}
	note.textContent='Cargando three.js...';
	try{await loadThree();}catch(e){
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
	updateViewUi();
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
		updateViewUi();
	});

	// A soft shadow under the board, so it sits on the table
	const sc=document.createElement('canvas');sc.width=sc.height=128;
	const sg=sc.getContext('2d');
	sg.filter='blur(9px)';sg.fillStyle='rgba(0,0,0,0.55)';sg.fillRect(22,22,84,84);
	const board=buildBoard(T,tex,new T.CanvasTexture(sc),GW,GH),root=board.root,screen=board.screen;
	root.visible=false;scene.add(root);
	// The diorama stands on the same spot as the board, without its tilt
	const dio=buildDiorama(T,GW,GH),mine={seq:0,backdropSeq:0};
	board.scaler.add(dio.group);

	const reticle=new T.Mesh(new T.RingGeometry(0.045,0.058,40).rotateX(-Math.PI/2),new T.MeshBasicMaterial({color:0xffffff}));
	reticle.add(new T.Mesh(new T.CircleGeometry(0.008,16).rotateX(-Math.PI/2),new T.MeshBasicMaterial({color:0x93d46b})));
	reticle.matrixAutoUpdate=false;reticle.visible=false;scene.add(reticle);

	const st={mode:'place',tilt:0,rot:0,yaw:0,width:board.width,placed:false,flat:true};
	window.__ar={T,root,screen,reticle,scene,camera,renderer,st,diorama:dio,sceneState:sceneState,get ref(){return ref;},get eye(){return eye;},get anchor(){return anchor;},get view(){return view;},get live(){return sceneLive;},onView:()=>showWhich()};	// for remote debugging
	// The diorama while a lawn is in play in 3D, the flat board otherwise
	function showWhich(){
		st.flat=!(view==='3d'&&sceneLive&&dio.group.visible);
		board.hinge.visible=st.flat;
		$('tiltGroup').hidden=!st.flat;
		layout();
	}
	function layout(){
		board.layout(st);
		$('tiltv').textContent=st.tilt+' grados';
		$('sizev').textContent=Math.round(st.width*100)+' cm';
	}
	let shownHint='';
	function showHint(t){if(t!==shownHint){shownHint=t;$('mode').textContent=t;}}
	function setMode(m){
		st.mode=m;
		showHint(m==='place'?'Apunta a la mesa hasta ver el circulo y toca para poner el jardin':
			m==='adjust'?'Ajustar: arrastra el jardin por la mesa, giralo o cambia el tamano. Luego Fijar':
			'Jugar: toca el jardin para hacer clic');
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
	showWhich();setMode('place');

	// Chrome on Android has WebXR Layers now, and three r160 then draws into a projection layer that the
	// AR compositor never shows. Hiding them makes three use the classic XRWebGLLayer, like Google's samples.
	try{Object.defineProperty(XRRenderState.prototype,'layers',{get(){return undefined;},configurable:true});}catch(e){}
	await renderer.xr.setSession(session);
	ref=renderer.xr.getReferenceSpace();
	hitSource=await session.requestHitTestSource({space:await session.requestReferenceSpace('viewer')});

	const m4=new T.Matrix4(),ray=new T.Ray(),rc=new T.Raycaster();
	const reticlePos=new T.Vector3(),eye=new T.Vector3(),eyeQuat=new T.Quaternion();
	function rayFrom(pose){
		m4.fromArray(pose.transform.matrix);
		ray.origin.setFromMatrixPosition(m4);
		ray.direction.set(0,0,-1).transformDirection(m4);
		return ray;
	}
	function gameAt(r){
		if(!st.flat)return dio.pick(r.origin,r.direction);
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
		const now=performance.now();
		const vp=frame.getViewerPose(ref);
		if(vp){
			eye.set(vp.transform.position.x,vp.transform.position.y,vp.transform.position.z);
			const o=vp.transform.orientation;if(o)eyeQuat.set(o.x,o.y,o.z,o.w);
		}
		if(applyScene(dio,mine,now)&&st.flat)showWhich();
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
		else if(st.mode==='place')showHint('Toca para poner el jardin donde esta el circulo');

		if(anchor&&!press){
			const ap=frame.getPose(anchor.anchorSpace,ref);
			if(ap){const q=ap.transform.position;root.position.set(q.x,q.y,q.z);root.updateMatrixWorld(true);}
		}
		if(!st.flat)dio.frame(now,eye,eyeQuat);
		if(press){
			const pose=frame.getPose(press.src.targetRaySpace,ref);
			if(pose){
				const r=rayFrom(pose);
				if(press.kind==='drag'){
					const p=rayOnPlaneY(r.origin,r.direction,root.position.y);
					if(p){root.position.x=p.x+press.dx;root.position.z=p.z+press.dz;root.updateMatrixWorld(true);}
				}else{
					const g=gameAt(r);
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
