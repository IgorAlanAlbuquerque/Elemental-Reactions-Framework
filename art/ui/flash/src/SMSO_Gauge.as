// Flash 8 / AS2 — classe do símbolo exportado "SMSO_Gauge"
class SMSO_Gauge extends MovieClip
{
  // ===== Clips do timeline (precisam existir no template.xml) =====
  // Rings (canvas de desenho)
  var ring_bg_mc:MovieClip;
  var ring_fire_mc:MovieClip;
  var ring_frost_mc:MovieClip;
  var ring_shock_mc:MovieClip;

  // Ícones (MovieClips wrappers dos bitmaps)
  var icon_fire_mc:MovieClip;               // 0
  var icon_frost_mc:MovieClip;              // 1
  var icon_shock_mc:MovieClip;              // 2
  var icon_fire_frost_mc:MovieClip;         // 3
  var icon_fire_shock_mc:MovieClip;         // 4
  var icon_frost_shock_mc:MovieClip;        // 5
  var icon_fire_frost_shock_mc:MovieClip;   // 6

  // ===== Parâmetros do anel =====
  private var rOut:Number    = 12;     // raio externo
  private var rIn:Number     = 8;      // raio interno (espessura = rOut - rIn)
  private var startDeg:Number = -90;   // -90 = topo; sentido horário

  // Cores dos segmentos (pode mudar via setRingColors)
  private var colFire:Number  = 0xE13B2D;
  private var colFrost:Number = 0x38B6FF;
  private var colShock:Number = 0xA56BFF;

  // Readiness (children resolvidos)
  private var _ready:Boolean = false;

  // ===== Ciclo de vida =====
  function SMSO_Gauge() {
    this._visible = true;        // temporário p/ validar
    this._alpha = 90;
    // desenha um quadradinho verde no bg:
    ring_bg_mc.beginFill(0x00FF00, 30);
    ring_bg_mc.moveTo(-16,-16); ring_bg_mc.lineTo(16,-16);
    ring_bg_mc.lineTo(16,16);   ring_bg_mc.lineTo(-16,16);
    ring_bg_mc.lineTo(-16,-16); ring_bg_mc.endFill();
    // this._visible = false;
    // clearRings();
    // hideAllIcons();
  }

  // chamado quando o frame 1 montar (children já existem)
  function onLoad():Void {
    _ready = (this["icon_fire_mc"] != undefined) &&
             (this["ring_fire_mc"]  != undefined) &&
             (this["ring_frost_mc"] != undefined) &&
             (this["ring_shock_mc"] != undefined);
  }

  function isReady():Boolean { return _ready; }

  // ===== ÍCONES =====
  function hideAllIcons():Void {
    if (icon_fire_mc)              icon_fire_mc._visible = false;
    if (icon_frost_mc)             icon_frost_mc._visible = false;
    if (icon_shock_mc)             icon_shock_mc._visible = false;
    if (icon_fire_frost_mc)        icon_fire_frost_mc._visible = false;
    if (icon_fire_shock_mc)        icon_fire_shock_mc._visible = false;
    if (icon_frost_shock_mc)       icon_frost_shock_mc._visible = false;
    if (icon_fire_frost_shock_mc)  icon_fire_frost_shock_mc._visible = false;
  }

  // 0..6 (0=Fire, 1=Frost, 2=Shock, 3=Fire+Frost, 4=Fire+Shock, 5=Frost+Shock, 6=Triple)
  // rgb (opcional) em 0xRRGGBB; se omitido/NaN, limpa o tint
  function setIcon(iconId:Number, rgb:Number):Void {
    hideAllIcons();

    var names:Array = [
      "icon_fire_mc",
      "icon_frost_mc",
      "icon_shock_mc",
      "icon_fire_frost_mc",
      "icon_fire_shock_mc",
      "icon_frost_shock_mc",
      "icon_fire_frost_shock_mc"
    ];

    var nm:String = names[iconId];
    if (!nm) return;

    var mc:MovieClip = this[nm];
    if (!mc) return;

    mc._visible = true;

    // Tint opcional
    var c:Color = new Color(mc);
    if (rgb != undefined && !isNaN(rgb)) {
      c.setRGB(rgb);
    } else {
      c.setTransform({ra:100, ga:100, ba:100, aa:100}); // remove tint
    }
  }

  // Mantido: aplica tint por nome arbitrário
  function applyTint(targetName:String, rgb:Number):Void {
    var mc:MovieClip = this[targetName];
    if (!mc) return;
    var c:Color = new Color(mc);
    if (rgb == undefined || isNaN(rgb)) {
      c.setTransform({ra:100, ga:100, ba:100, aa:100});
    } else {
      c.setRGB(rgb);
    }
  }

  // ===== ANEL: parcial 0..360° segmentado por acumuladores =====
  // maxPoints opcional (default 100)
  function setAccumulators(fireAcc:Number, frostAcc:Number, shockAcc:Number, maxPoints:Number):Void {
    clearRings();

    if (isNaN(maxPoints) || maxPoints <= 0) maxPoints = 100;

    var f:Number = (fireAcc  || 0);
    var r:Number = (frostAcc || 0);
    var s:Number = (shockAcc || 0);
    var sum:Number = f + r + s;

    if (sum <= 0) { this._visible = false; return; }

    // arco total proporcional ao total (cap em maxPoints)
    var totalArcDeg:Number = 360 * Math.min(sum, maxPoints) / maxPoints;

    // distribui entre elementos (fecha no último p/ evitar fenda por arredondamento)
    var fireDeg:Number  = totalArcDeg * (f / sum);
    var frostDeg:Number = totalArcDeg * (r / sum);
    var shockDeg:Number = totalArcDeg - (fireDeg + frostDeg);

    var curStart:Number = startDeg;

    if (fireDeg  > 0.01) { drawRingSegment(ring_fire_mc,  curStart, fireDeg,  colFire,  100);  curStart += fireDeg;  }
    if (frostDeg > 0.01) { drawRingSegment(ring_frost_mc, curStart, frostDeg, colFrost, 100);  curStart += frostDeg; }
    if (shockDeg > 0.01) { drawRingSegment(ring_shock_mc, curStart, shockDeg, colShock, 100); }

    this._visible = true;
  }

  // ===== Utilidades para cor/ângulo (opcionais) =====
  function setRingColors(rgbFire:Number, rgbFrost:Number, rgbShock:Number):Void {
    if (rgbFire  != undefined && !isNaN(rgbFire))  colFire  = rgbFire;
    if (rgbFrost != undefined && !isNaN(rgbFrost)) colFrost = rgbFrost;
    if (rgbShock != undefined && !isNaN(rgbShock)) colShock = rgbShock;
  }

  function setStartAngle(deg:Number):Void {
    if (!isNaN(deg)) startDeg = deg;
  }

  // ===== Desenho =====
  function clearRings():Void {
    if (ring_bg_mc)    ring_bg_mc.clear();
    if (ring_fire_mc)  ring_fire_mc.clear();
    if (ring_frost_mc) ring_frost_mc.clear();
    if (ring_shock_mc) ring_shock_mc.clear();
  }

  // Desenha setor ANULAR de 'sweepDeg' iniciando em 'startDeg' (graus)
  function drawRingSegment(target:MovieClip, startDeg:Number, sweepDeg:Number, color:Number, alpha:Number):Void {
    if (!target || sweepDeg <= 0) return;

    var startRad:Number = startDeg * Math.PI / 180;
    var endRad:Number   = (startDeg + sweepDeg) * Math.PI / 180;

    // resolução ~6°
    var steps:Number = Math.max(4, Math.round(Math.abs(sweepDeg) / 6));

    var outer:Array = arcPoints(rOut, startRad, endRad, steps);   // horário
    var inner:Array = arcPoints(rIn,  endRad,   startRad, steps); // volta anti-horário

    target.clear();
    target.lineStyle(0, 0, 0);
    target.beginFill(color, alpha);

    var p:Object = outer[0];
    target.moveTo(p.x, p.y);
    for (var i:Number = 1; i < outer.length; i++) { p = outer[i]; target.lineTo(p.x, p.y); }
    for (var j:Number = 0; j < inner.length; j++) { p = inner[j]; target.lineTo(p.x, p.y); }

    target.endFill();
  }

  // Pontos ao longo de um arco de raio r entre a0..a1 (radianos)
  function arcPoints(r:Number, a0:Number, a1:Number, steps:Number):Array {
    var pts:Array = [];
    for (var i:Number = 0; i <= steps; i++) {
      var t:Number = a0 + (a1 - a0) * (i / steps);
      pts.push({ x: r * Math.cos(t), y: r * Math.sin(t) });
    }
    return pts;
  }
}