// Flash 8 / AS2
class SMSO_Gauge extends MovieClip
{
  // ===== Clips do timeline =====
  var ring_bg_mc:MovieClip;
  var ring_fire_mc:MovieClip;
  var ring_frost_mc:MovieClip;
  var ring_shock_mc:MovieClip;

  var icon_fire_mc:MovieClip;               // 0
  var icon_frost_mc:MovieClip;              // 1
  var icon_shock_mc:MovieClip;              // 2
  var icon_fire_frost_mc:MovieClip;         // 3
  var icon_fire_shock_mc:MovieClip;         // 4
  var icon_frost_shock_mc:MovieClip;        // 5
  var icon_fire_frost_shock_mc:MovieClip;   // 6

  // só para “reservar” topo de depth (não reparentamos ícones)
  var icons_mc:MovieClip;

  // ===== Parâmetros =====
  var rOut:Number     = 7;
  var strokePx:Number = 3;
  var iconPx:Number   = 14;

  var _ready:Boolean = false;

  var _iconNames:Array = [
    "icon_fire_mc",
    "icon_frost_mc",
    "icon_shock_mc",
    "icon_fire_frost_mc",
    "icon_fire_shock_mc",
    "icon_frost_shock_mc",
    "icon_fire_frost_shock_mc"
  ];

  function SMSO_Gauge() {}

  // ---------- helpers (no nível da classe!) ----------
  function placeRing(mc:MovieClip, off:Number, depth:Number):Void
  {
    if (!mc) return;
    mc._x = off; mc._y = off;
    mc.swapDepths(depth);
  }

  // centraliza pelo bounding box visível (robusto a PNG com padding/registro)
  function centerOn(mc:MovieClip, cx:Number, cy:Number):Void
  {
    var b:Object = mc.getBounds(this);   // bounds em coords do símbolo
    var mx:Number = (b.xMin + b.xMax) * 0.5;
    var my:Number = (b.yMin + b.yMax) * 0.5;
    mc._x += (cx - mx);
    mc._y += (cy - my);
  }

  // ---------- ciclo de vida ----------
  function onLoad():Void
  {
    icons_mc = this.createEmptyMovieClip("icons_mc", this.getNextHighestDepth());

    var off:Number = rOut + 2;
    var cx:Number = off, cy:Number = off;

    // 1) Rings sempre atrás (depths baixos e fixos)
    var dRing:Number = 1;
    placeRing(ring_bg_mc,    off, dRing++);  // 1
    placeRing(ring_fire_mc,  off, dRing++);  // 2
    placeRing(ring_frost_mc, off, dRing++);  // 3
    placeRing(ring_shock_mc, off, dRing++);  // 4

    // 2) Ícones: acima dos rings, centralizados
    var icons:Array = [
      icon_fire_mc, icon_frost_mc, icon_shock_mc,
      icon_fire_frost_mc, icon_fire_shock_mc,
      icon_frost_shock_mc, icon_fire_frost_shock_mc
    ];

    var dIcon:Number = 100; // bem acima dos rings
    for (var i:Number=0; i<icons.length; i++) {
      var mc:MovieClip = icons[i];
      if (!mc) continue;
      mc.swapDepths(dIcon++); // garante z-order acima dos rings
      mc._visible = false;
      mc._alpha = 100;
      mc._xscale = mc._yscale = 100;
      mc.cacheAsBitmap = true;
      centerOn(mc, cx, cy);
    }

    clearRings();
    hideAllIcons();

    _ready =
      ring_fire_mc  != undefined && ring_frost_mc != undefined && ring_shock_mc != undefined &&
      icon_fire_mc  != undefined && icon_frost_mc != undefined && icon_shock_mc != undefined &&
      icon_fire_frost_mc != undefined && icon_fire_shock_mc != undefined &&
      icon_frost_shock_mc != undefined && icon_fire_frost_shock_mc != undefined;
  }

  function isReady():Boolean { return _ready; }

  // ===== ÍCONES =====
  function hideAllIcons():Void
  {
    if (icon_fire_mc)              icon_fire_mc._visible = false;
    if (icon_frost_mc)             icon_frost_mc._visible = false;
    if (icon_shock_mc)             icon_shock_mc._visible = false;
    if (icon_fire_frost_mc)        icon_fire_frost_mc._visible = false;
    if (icon_fire_shock_mc)        icon_fire_shock_mc._visible = false;
    if (icon_frost_shock_mc)       icon_frost_shock_mc._visible = false;
    if (icon_fire_frost_shock_mc)  icon_fire_frost_shock_mc._visible = false;
  }

  // 0..6 (0=Fire, 1=Frost, 2=Shock, 3=Fire+Frost, 4=Fire+Shock, 5=Frost+Shock, 6=Triple)
  function setIcon(iconId:Number, rgb:Number):Boolean
  {
    var idx:Number = (iconId == undefined || isNaN(iconId)) ? -1 : (iconId | 0);
    if (idx < 0 || idx > 6) { hideAllIcons(); return false; }

    hideAllIcons();

    var nm:String = _iconNames[idx];
    var mc:MovieClip = this[nm];
    if (!mc) return false;

    // Subir pro topo
    mc.swapDepths(this.getNextHighestDepth());

    // Garantir bounds válidos
    var wasVis:Boolean = mc._visible;
    if (!wasVis) mc._visible = true;

    // Medir bounds
    var b:Object = mc.getBounds(this);
    var w:Number = b.xMax - b.xMin;
    var h:Number = b.yMax - b.yMin;

    // Escala para caber no anel
    var target:Number = iconPx;
    var s:Number = 100 * Math.min(target / w, target / h);
    mc._xscale = mc._yscale = s;

    // Re-medida após escalar e CENTRALIZAÇÃO AGORA
    b = mc.getBounds(this);
    var mx:Number = (b.xMin + b.xMax) * 0.5;
    var my:Number = (b.yMin + b.yMax) * 0.5;
    var off:Number = rOut + 2;
    mc._x += (off - mx);
    mc._y += (off - my);

    // Visível e tint opcional
    mc._visible = true;
    var c:Color = new Color(mc);
    if (rgb != undefined && !isNaN(rgb)) c.setRGB(rgb);
    else c.setTransform({ra:100,ga:100,ba:100,aa:100});

    return true;
  }

  function setComboFill(rem:Number, rgb:Number):Boolean
  {
    // clamp e checagens
    var frac:Number = (isNaN(rem)) ? 0 : Math.max(0, Math.min(1, rem));

    // escolha do "ring" a desenhar (use ring_combo_mc se existir; senão, caia no ring_fire_mc)
    var ring:MovieClip = this["ring_combo_mc"] ? this["ring_combo_mc"] : ring_fire_mc;
    if (!ring) return false;

    // fundo discreto
    if (ring_bg_mc) {
      ring_bg_mc.clear();
      drawArc(ring_bg_mc, 0, 1, 0x000000, 30);
    }

    // se for usar o ring_fire_mc como canal do combo, limpe os outros para não sobrepor
    if (ring == ring_fire_mc) {
      if (ring_fire_mc)  ring_fire_mc.clear();
      if (ring_frost_mc) ring_frost_mc.clear();
      if (ring_shock_mc) ring_shock_mc.clear();
    } else {
      ring.clear(); // combo ring dedicado
    }

    if (frac <= 0) {
      this._visible = false;  // esconde o container quando acabar
      return false;
    }

    var color:Number = (rgb != undefined && !isNaN(rgb)) ? rgb : 0xFFFFFF;
    drawArc(ring, 0, frac, color, 100);

    this._visible = true;     // garante visível enquanto tiver combo
    return true;
  }

  // ===== ANEL =====
  function clearRings():Void
  {
    if (ring_bg_mc)    ring_bg_mc.clear();
    if (ring_fire_mc)  ring_fire_mc.clear();
    if (ring_frost_mc) ring_frost_mc.clear();
    if (ring_shock_mc) ring_shock_mc.clear();

    drawArc(ring_bg_mc, 0, 1, 0x000000, 30);
  }

  // desenha frações em sequência; 360° só se soma >= 100
  function setAccumulators(fire:Number, frost:Number, shock:Number):Boolean
  {
    if (ring_fire_mc)  ring_fire_mc.clear();
    if (ring_frost_mc) ring_frost_mc.clear();
    if (ring_shock_mc) ring_shock_mc.clear();

    var f:Number = Math.max(0, fire  || 0);
    var r:Number = Math.max(0, frost || 0);
    var s:Number = Math.max(0, shock || 0);
    var rawSum:Number = f + r + s;

    if (rawSum <= 0) { this._visible = false; return false; }

    var totalFrac:Number = Math.min(rawSum / 100.0, 1.0);

    var cur:Number = 0;
    var fireFrac:Number  = (f > 0) ? totalFrac * (f / rawSum) : 0;
    var frostFrac:Number = (r > 0) ? totalFrac * (r / rawSum) : 0;
    var shockFrac:Number = Math.max(0, totalFrac - (fireFrac + frostFrac));

    if (fireFrac  > 0) { drawArc(ring_fire_mc,  cur, cur + fireFrac,  0xFF3A2A, 100); cur += fireFrac;  }
    if (frostFrac > 0) { drawArc(ring_frost_mc, cur, cur + frostFrac, 0x64C8FF, 100); cur += frostFrac; }
    if (shockFrac > 0) { drawArc(ring_shock_mc, cur, cur + shockFrac, 0xFFD034, 100); }

    this._visible = true;
    return true;
  }

  private function drawArc(mc:MovieClip, from:Number, to:Number, color:Number, alpha:Number):Void
  {
    if (!mc) return;
    var f:Number = Math.max(0, Math.min(1, from));
    var t:Number = Math.max(0, Math.min(1, to));
    if (t <= f) return;

    var off:Number = rOut + 2;
    var a0:Number = -Math.PI/2 + f * 2*Math.PI;
    var a1:Number = -Math.PI/2 + t * 2*Math.PI;

    mc.lineStyle(strokePx, color, alpha);

    var steps:Number = Math.max(6, Math.round((t - f) * 36));
    var ang:Number = a0;
    mc.moveTo(off + Math.cos(ang)*rOut, off + Math.sin(ang)*rOut);

    for (var i:Number = 1; i <= steps; i++) {
      ang = a0 + (a1 - a0) * (i / steps);
      mc.lineTo(off + Math.cos(ang)*rOut, off + Math.sin(ang)*rOut);
    }
  }
}
