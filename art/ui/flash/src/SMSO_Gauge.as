import flash.display.BitmapData;

class SMSO_Gauge extends MovieClip
{
  private var gauge_mc:MovieClip = null;        
  private var ring_bg_mc:MovieClip;
  private var ring_fire_mc:MovieClip;
  private var ring_frost_mc:MovieClip;
  private var ring_shock_mc:MovieClip;

  private var icon_container_mc:MovieClip = null; 

  private var rOut:Number       = 7;
  private var strokePx:Number   = 2;
  private var iconPadPx:Number  = 0.5;
  private var iconNudgeX:Number = 0;
  private var iconNudgeY:Number = 0;

  private var _ready:Boolean = false;
  private var _tried:Boolean = false;

  private var _iconBitmapIds:Array = [
    "icon_fire",
    "icon_frost",
    "icon_shock",
    "icon_fire_frost",
    "icon_fire_shock",
    "icon_frost_shock",
    "icon_fire_frost_shock"
  ];

  function SMSO_Gauge() {}

  private function drawArc(mc:MovieClip, from:Number, to:Number, color:Number, alpha:Number):Void
  {
    if (!mc) return;
    var f:Number = Math.max(0, Math.min(1, from));
    var t:Number = Math.max(0, Math.min(1, to));
    if (t <= f) return;

    var a0:Number = -Math.PI/2 + f * 2*Math.PI;
    var a1:Number = -Math.PI/2 + t * 2*Math.PI;

    mc.lineStyle(strokePx, color, alpha);

    var steps:Number = Math.max(6, Math.round((t - f) * 48)); // mais liso
    var ang:Number = a0;
    mc.moveTo(Math.cos(ang)*rOut, Math.sin(ang)*rOut);

    for (var i:Number = 1; i <= steps; i++) {
      ang = a0 + (a1 - a0) * (i / steps);
      mc.lineTo(Math.cos(ang)*rOut, Math.sin(ang)*rOut);
    }
  }

  private function drawFilledCircle(mc:MovieClip, cx:Number, cy:Number, r:Number, rgb:Number, alpha:Number):Void {
    mc.clear();
    mc.lineStyle(0, 0x000000, 0);
    mc.beginFill(rgb, alpha);
    var steps:Number = 64;
    for (var i:Number = 0; i <= steps; i++) {
      var a:Number = (i/steps) * Math.PI * 2;
      var x:Number = cx + Math.cos(a) * r;
      var y:Number = cy + Math.sin(a) * r;
      if (i == 0) mc.moveTo(x, y); else mc.lineTo(x, y);
    }
    mc.endFill();
  }

  private function clearRings():Void
  {
    if (ring_bg_mc)    ring_bg_mc.clear();
    if (ring_fire_mc)  ring_fire_mc.clear();
    if (ring_frost_mc) ring_frost_mc.clear();
    if (ring_shock_mc) ring_shock_mc.clear();
  }

  private function hideAllIcons():Void
  {
    if (icon_container_mc) {
      icon_container_mc.removeMovieClip();
      icon_container_mc = null;
    }
  }

  // ================== Ciclo de vida ==================
  public function onLoad():Void
  {
    var off:Number = rOut + 2;

    if (gauge_mc) { gauge_mc.removeMovieClip(); gauge_mc = null; }

    gauge_mc = this.createEmptyMovieClip("gauge_mc", 200);
    gauge_mc._x = off;
    gauge_mc._y = off;

    var halo_mc:MovieClip  = gauge_mc.createEmptyMovieClip("halo_mc",   0);
    ring_bg_mc             = gauge_mc.createEmptyMovieClip("ring_bg_mc",   10);
    ring_fire_mc           = gauge_mc.createEmptyMovieClip("ring_fire_mc", 20);
    ring_frost_mc          = gauge_mc.createEmptyMovieClip("ring_frost_mc",30);
    ring_shock_mc          = gauge_mc.createEmptyMovieClip("ring_shock_mc",40);

    var haloMargin:Number = 2;
    var haloR:Number = rOut + (strokePx * 0.5) + haloMargin;
    drawFilledCircle(halo_mc, 0, 0, haloR, 0x000000, 100);

    clearRings();
    hideAllIcons();

    _ready = (ring_bg_mc    != undefined)
      && (ring_fire_mc  != undefined)
      && (ring_frost_mc != undefined)
      && (ring_shock_mc != undefined);
    _tried = true;
  }

  private function tryInit():Void
  {
    if (_tried) return;
    _tried = true;
    onLoad();
  }

  public function isReady():Boolean
  {
    if (!_ready) tryInit();
    return _ready;
  }

  // ================== API ==================
  public function setIcon(iconId:Number, tintRGB:Number, scaleMul:Number):Boolean
  {
    var idx:Number = (isNaN(iconId)) ? -1 : (iconId | 0);
    if (idx < 0 || idx >= _iconBitmapIds.length) { hideAllIcons(); return false; }

    hideAllIcons();

    var d:Number = this.getNextHighestDepth();
    var container:MovieClip = gauge_mc.createEmptyMovieClip("icon_container_mc", 100);
    icon_container_mc = container;

    var bmpId:String = _iconBitmapIds[idx];
    var bd:BitmapData = BitmapData.loadBitmap(bmpId);
    if (!bd) return false;

    var holder:MovieClip = container.createEmptyMovieClip("icon_holder_mc", container.getNextHighestDepth());
    holder.cacheAsBitmap = true;
    holder.attachBitmap(bd, 1, "always", true);

    var targetPx:Number = Math.max(2, (rOut - (strokePx * 0.5) - iconPadPx) * 2);
    var baseScale:Number = (targetPx / Math.max(bd.width, bd.height)) * 100;
    var mul:Number = (!isNaN(scaleMul) && scaleMul > 0) ? scaleMul : 1.0;
    holder._xscale = holder._yscale = baseScale * mul;

    holder._x = -Math.round(holder._width  / 2) + iconNudgeX;
    holder._y = -Math.round(holder._height / 2) + iconNudgeY;

    if (!isNaN(tintRGB)) {
      var c:Color = new Color(holder);
      c.setRGB(tintRGB);
    }

    container._x = Math.round(container._x);
    container._y = Math.round(container._y);

    return true;
  }

  public function setComboFill(rem:Number, rgb:Number):Void
  {
    var frac:Number = (isNaN(rem)) ? 0 : Math.max(0, Math.min(1, rem));
    var ring:MovieClip = this["ring_combo_mc"] ? this["ring_combo_mc"] : ring_fire_mc;
    if (!ring) return;

    if (ring_bg_mc) {
      ring_bg_mc.clear();
      drawArc(ring_bg_mc, 0, 1, 0x000000, 30);
    }

    clearRings();

    if (frac <= 0) { this._visible = false; return; }

    var color:Number = (rgb != undefined && !isNaN(rgb) && rgb != 0) ? rgb : 0xFFFFFF;
    drawArc(ring, 0, frac, color, 100);

    this._visible = true;
  }

  public function setAccumulators(fire:Number, frost:Number, shock:Number):Void
  {
    clearRings();

    var f:Number = Math.max(0, fire  || 0);
    var r:Number = Math.max(0, frost || 0);
    var s:Number = Math.max(0, shock || 0);
    var rawSum:Number = f + r + s;

    if (rawSum <= 0) { this._visible = false; return; }

    var totalFrac:Number = Math.min(rawSum / 100.0, 1.0);

    var cur:Number = 0;
    var fireFrac:Number  = (f > 0) ? totalFrac * (f / rawSum) : 0;
    var frostFrac:Number = (r > 0) ? totalFrac * (r / rawSum) : 0;
    var shockFrac:Number = Math.max(0, totalFrac - (fireFrac + frostFrac));

    drawArc(ring_bg_mc, 0, 1, 0x000000, 30);
    if (fireFrac  > 0) { drawArc(ring_fire_mc,  cur, cur + fireFrac,  0xFF3A2A, 100); cur += fireFrac;  }
    if (frostFrac > 0) { drawArc(ring_frost_mc, cur, cur + frostFrac, 0x64C8FF, 100); cur += frostFrac; }
    if (shockFrac > 0) { drawArc(ring_shock_mc, cur, cur + shockFrac, 0xFFD034, 100); }

    this._visible = true;
  }
}
