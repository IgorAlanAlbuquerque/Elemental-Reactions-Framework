class SMSO_Gauge {
  // referências dos elementos (por nome, já criados no template.xml)
  private var root:MovieClip;
  private var bg:MovieClip, ringF:MovieClip, ringR:MovieClip, ringS:MovieClip;
  private var iFire:MovieClip, iFrost:MovieClip, iShock:MovieClip;
  private var iFF:MovieClip, iFS:MovieClip, iRS:MovieClip, iFFF:MovieClip;

  // estilo
  private var rOut:Number = 12;   // menor para caber na barra (px)
  private var rIn:Number  = 8;
  private var iconPx:Number = 24; // ícone ~24px

  // cores dos segmentos do anel
  private var colFire:Number  = 0xE13B2D; // vermelho
  private var colFrost:Number = 0x38B6FF; // azul
  private var colShock:Number = 0xA56BFF; // roxo
  private var colBg:Number    = 0x444444; // fundo

  public function SMSO_Gauge(target:MovieClip) {
    root = target;

    // pega referências
    bg    = root.ring_bg_mc;
    ringF = root.ring_fire_mc;
    ringR = root.ring_frost_mc;
    ringS = root.ring_shock_mc;

    iFire = root.icon_fire_mc;
    iFrost= root.icon_frost_mc;
    iShock= root.icon_shock_mc;
    iFF   = root.icon_fire_frost_mc;
    iFS   = root.icon_fire_shock_mc;
    iRS   = root.icon_frost_shock_mc;
    iFFF  = root.icon_fire_frost_shock_mc;

    // centraliza + escala ícones (PNG 256->24 px)
    var icons:Array = [iFire,iFrost,iShock,iFF,iFS,iRS,iFFF];
    for (var n:Number=0; n<icons.length; n++) {
      var ic:MovieClip = icons[n];
      var s:Number = (iconPx / ic._width) * 100;
      ic._xscale = ic._yscale = s;
      ic._x = -iconPx*0.5;
      ic._y = -iconPx*0.5;
    }

    // desenha fundo (círculo completo)
    drawRing(bg, rOut, rIn, colBg, 40);

    setIcon(0, undefined);
    setTotals(0,0,0);
    setVisible(false);
  }

  // ===== util de desenho =====
  private function drawRing(mc:MovieClip, rOut:Number, rIn:Number, color:Number, alpha:Number):Void {
    mc.clear(); mc.beginFill(color, alpha);
    var seg:Number=24, step:Number=360/seg, k:Number, ang:Number, x:Number, y:Number;
    for (k=0;k<=seg;k++){ ang=(-90+step*k)*Math.PI/180; x=rOut*Math.cos(ang); y=rOut*Math.sin(ang); if(k==0) mc.moveTo(x,y); else mc.lineTo(x,y); }
    for (k=seg;k>=0;k--){ ang=(-90+step*k)*Math.PI/180; x=rIn*Math.cos(ang);  y=rIn*Math.sin(ang);  mc.lineTo(x,y); }
    mc.endFill();
  }

  private function drawArc(mc:MovieClip, rOut:Number, rIn:Number, startDeg:Number, lenDeg:Number, color:Number):Void {
    mc.clear(); if (lenDeg<=0) return;
    mc.beginFill(color, 100);
    var seg:Number = Math.max(4, Math.ceil(Math.abs(lenDeg)/10));
    var step:Number = lenDeg/seg;
    var k:Number, ang:Number;
    ang = startDeg*Math.PI/180;
    mc.moveTo(rOut*Math.cos(ang), rOut*Math.sin(ang));
    for (k=1;k<=seg;k++) { ang=(startDeg+step*k)*Math.PI/180; mc.lineTo(rOut*Math.cos(ang), rOut*Math.sin(ang)); }
    for (k=seg;k>=0;k--) { ang=(startDeg+step*k)*Math.PI/180; mc.lineTo(rIn*Math.cos(ang),  rIn*Math.sin(ang));  }
    mc.endFill();
  }

  // ===== API pública (chamada pelo seu C++) =====
  public function setTotals(f:Number, fr:Number, s:Number):Void {
    if (f<0) f=0; if (fr<0) fr=0; if (s<0) s=0;

    // “empurra” segmentos na ordem F (vermelho) -> Fr (azul) -> S (roxo)
    var left:Number = 100;
    var takeF:Number = Math.min(f, left); left -= takeF;
    var takeR:Number = Math.min(fr, left); left -= takeR;
    var takeS:Number = Math.min(s,  left);

    var degF:Number = takeF * 3.6;
    var degR:Number = takeR * 3.6;
    var degS:Number = takeS * 3.6;

    var start:Number = -90;
    drawArc(ringF, rOut, rIn, start,              degF, colFire);
    drawArc(ringR, rOut, rIn, start + degF,       degR, colFrost);
    drawArc(ringS, rOut, rIn, start + degF+degR,  degS, colShock);

    var any:Boolean = (f+fr+s) > 0;
    ringF._alpha = ringR._alpha = ringS._alpha = any ? 100 : 0;
  }

  // id: 0=fire,1=frost,2=shock,3=fire_frost,4=fire_shock,5=frost_shock,6=triple
  public function setIcon(id:Number, tintRGB:Number):Void {
    var list:Array = [iFire,iFrost,iShock,iFF,iFS,iRS,iFFF];
    for (var k:Number=0;k<list.length;k++) list[k]._visible = (k==id);

    if (id>=3 && id<=5 && (tintRGB != undefined)) {
      var c:Color = new Color(list[id]);
      c.setRGB(tintRGB); // PNG preto + alpha -> recebe a cor lindamente
    }
  }

  public function setVisible(v:Boolean):Void { root._visible = v ? true : false; }
  public function setScale(pct:Number):Void { root._xscale = root._yscale = pct; }
}
